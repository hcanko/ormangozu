#include "sensor_manager.h"
#include "algorithm_manager.h"
#include "lora_manager.h"
#include "storage_manager.h"
#include "network_manager.h"

#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include <Adafruit_BME680.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <cstring>

namespace {
Adafruit_MLX90640 mlx;
Adafruit_BME680 bme;
bool mlxInitialized = false;
bool bmeInitialized = false;
TaskHandle_t sensorTaskHandle = nullptr;
volatile bool immediateSampleRequested = false;
volatile uint32_t fastModeUntilMs = 0;
String peerTriggerSource = "";
uint32_t peerTriggerSequence = 0;
RTC_DATA_ATTR uint32_t persistentSequence = 0;
uint32_t errorCounter = 0;
uint32_t lastAlertTxMs = 0;
uint8_t lastAlertTxLevel = FIRE_NORMAL;

struct LocalEventRecord {
    bool valid;
    bool confirmed;
    uint32_t event_sequence;
    uint32_t created_ms;
    uint8_t fire_level;
    uint8_t health_level;
    float fire_score;
    float max_temp;
    uint16_t largest_cluster;
    uint8_t persistence_count;
    float gas_drop_pct;
    int battery_pct;
};

LocalEventRecord localEvents[LOCAL_EVENT_RING_SIZE] = {};
uint8_t nextLocalEventSlot = 0;

float getBatteryMilliVolts() {
    pinMode(ADC_CTRL_PIN, OUTPUT);
    digitalWrite(ADC_CTRL_PIN, HIGH);
    delay(40);
    const int adcMilliVolts = analogReadMilliVolts(BATTERY_PIN);
    digitalWrite(ADC_CTRL_PIN, LOW);
    return adcMilliVolts * BATTERY_DIVIDER_FACTOR;
}

int getBatteryPercentage(float mv) {
    return constrain(map(static_cast<int>(mv), 3300, 4200, 0, 100), 0, 100);
}

void fillPixelsWithZero(float *pixels) {
    for (int i = 0; i < 768; ++i) pixels[i] = 0.0f;
}

SensorDataPacket packetFromRecord(const LocalEventRecord &record) {
    SensorDataPacket packet = {};
    packet.sequence = record.event_sequence;
    packet.fire_level = record.fire_level;
    packet.health_level = record.health_level;
    packet.local_fire_score = record.fire_score;
    packet.max_temp = record.max_temp;
    packet.largest_hotspot_cluster = record.largest_cluster;
    packet.persistence_count = record.persistence_count;
    packet.gas_drop_pct = record.gas_drop_pct;
    packet.battery_pct = record.battery_pct;
    packet.network_confirmed = record.confirmed;
    return packet;
}

LocalEventRecord *findLocalEvent(uint32_t eventSequence) {
    for (uint8_t i = 0; i < LOCAL_EVENT_RING_SIZE; ++i) {
        if (localEvents[i].valid && localEvents[i].event_sequence == eventSequence) {
            return &localEvents[i];
        }
    }
    return nullptr;
}

void rememberLocalEvent(const SensorDataPacket &packet, uint32_t eventSequence) {
    LocalEventRecord *existing = findLocalEvent(eventSequence);
    LocalEventRecord *slot = existing;
    if (slot == nullptr) {
        slot = &localEvents[nextLocalEventSlot];
        nextLocalEventSlot = (nextLocalEventSlot + 1) % LOCAL_EVENT_RING_SIZE;
    }
    slot->valid = true;
    slot->confirmed = false;
    slot->event_sequence = eventSequence;
    slot->created_ms = millis();
    slot->fire_level = packet.fire_level;
    slot->health_level = packet.health_level;
    slot->fire_score = packet.local_fire_score;
    slot->max_temp = packet.max_temp;
    slot->largest_cluster = packet.largest_hotspot_cluster;
    slot->persistence_count = packet.persistence_count;
    slot->gas_drop_pct = packet.gas_drop_pct;
    slot->battery_pct = packet.battery_pct;
}

bool hasThermalEvidence(uint8_t level, float maxTemp, uint16_t cluster) {
    return level >= FIRE_WATCH &&
           (maxTemp >= THERMAL_WATCH_MAX_C || cluster >= THERMAL_MIN_CLUSTER_PIXELS);
}

bool shouldNetworkConfirm(const LocalEventRecord &local, const PeerEvent &peer) {
    const bool localThermal = hasThermalEvidence(local.fire_level, local.max_temp, local.largest_cluster);
    const bool peerThermal = hasThermalEvidence(peer.fire_level, peer.max_temp, peer.largest_cluster);

    if (local.fire_level >= FIRE_WARNING && peer.fire_level >= FIRE_WARNING) return true;
    if (local.fire_level >= FIRE_CRITICAL && peerThermal) return true;
    if (peer.fire_level >= FIRE_CRITICAL && localThermal) return true;
    return false;
}

bool collectSample(SensorDataPacket &packet, float elapsedSeconds) {
    packet = {};
    packet.sequence = ++persistentSequence;
    packet.uptime_ms = millis();
    packet.lat = TOWER_LATITUDE;
    packet.lng = TOWER_LONGITUDE;
    packet.free_heap = ESP.getFreeHeap();

    float maxT = -273.15f;
    if (mlxInitialized) {
        const int state = mlx.getFrame(packet.pixels);
        if (state == 0) {
            packet.mlx_ok = true;
            for (int i = 0; i < 768; ++i) maxT = max(maxT, packet.pixels[i]);
        } else {
            errorCounter++;
            fillPixelsWithZero(packet.pixels);
            Serial.printf("❌ MLX frame hatası: %d\n", state);
        }
    } else {
        errorCounter++;
        fillPixelsWithZero(packet.pixels);
    }
    packet.max_temp = packet.mlx_ok ? maxT : 0.0f;

    if (bmeInitialized && bme.performReading()) {
        packet.gas_res = bme.gas_resistance;
        packet.gas_ok = packet.gas_res > 0.0f;
    } else {
        errorCounter++;
        packet.gas_res = 0.0f;
    }

    packet.battery_mv = getBatteryMilliVolts();
    packet.battery_pct = getBatteryPercentage(packet.battery_mv);
    packet.solar_voltage_mv = 0.0f;
    packet.solar_current_ma = 0.0f;

    AlgorithmManager::analyze(packet, elapsedSeconds);
    packet.error_counter = errorCounter;
    return packet.mlx_ok || packet.gas_ok;
}

void processPeerReport(const PeerEvent &event) {
    LocalEventRecord *local = findLocalEvent(event.sequence);
    if (local == nullptr) {
        StorageManager::logFusionEvent("UNMATCHED_REPORT", event.sequence, nullptr, &event,
                                       "yerel olay kaydı bulunamadı");
        return;
    }

    const uint32_t age = millis() - local->created_ms;
    if (age > NETWORK_CONFIRM_WINDOW_MS) {
        SensorDataPacket localPacket = packetFromRecord(*local);
        StorageManager::logFusionEvent("LATE_REPORT", event.sequence, &localPacket, &event,
                                       String("age_ms=") + String(age));
        return;
    }

    SensorDataPacket localPacket = packetFromRecord(*local);
    if (shouldNetworkConfirm(*local, event)) {
        if (!local->confirmed) {
            local->confirmed = true;
            local->fire_level = FIRE_CONFIRMED;
            localPacket.fire_level = FIRE_CONFIRMED;
            localPacket.network_confirmed = true;
            StorageManager::logFusionEvent("CONFIRMED", event.sequence, &localPacket, &event,
                                           "iki direk termal kanıtla teyit etti");
            LoraManager::queueConfirmation(localPacket, String(event.source), event.sequence);
            Serial.printf("✅ NETWORK CONFIRMED event=%lu peer=%s\n",
                          static_cast<unsigned long>(event.sequence), event.source);
        }
    } else {
        StorageManager::logFusionEvent("NOT_CONFIRMED", event.sequence, &localPacket, &event,
                                       "eşik kombinasyonu yeterli değil");
    }
}

void processPeerEvents() {
    if (peerEventQueue == nullptr) return;
    PeerEvent event = {};
    while (xQueueReceive(peerEventQueue, &event, 0) == pdPASS) {
        if (event.type == PEER_EVENT_ALERT) {
            peerTriggerSource = String(event.source);
            peerTriggerSequence = event.sequence;
            fastModeUntilMs = millis() + FAST_MODE_DURATION_MS;
            immediateSampleRequested = true;
            StorageManager::logFusionEvent("PEER_WAKE", event.sequence, nullptr, &event,
                                           "eş alarmı hızlı örneklemeyi tetikledi");
        } else if (event.type == PEER_EVENT_REPORT) {
            processPeerReport(event);
        } else if (event.type == PEER_EVENT_CONFIRMED) {
            LocalEventRecord *local = findLocalEvent(event.sequence);
            if (local != nullptr) {
                local->confirmed = true;
                local->fire_level = FIRE_CONFIRMED;
                SensorDataPacket localPacket = packetFromRecord(*local);
                localPacket.network_confirmed = true;
                StorageManager::logFusionEvent("CONFIRMED_BY_PEER", event.sequence,
                                               &localPacket, &event,
                                               "karşı direk ağ teyidini bildirdi");
            } else {
                StorageManager::logFusionEvent("CONFIRM_WITHOUT_LOCAL", event.sequence,
                                               nullptr, &event,
                                               "yerel olay kaydı bulunamadı");
            }
        }
    }
}

void waitLowPower(uint32_t waitMs) {
    if (waitMs < MIN_LIGHT_SLEEP_MS) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
        return;
    }

#if ENABLE_LIGHT_SLEEP
    const uint32_t graceDeadline = millis() + 1500;
    while (static_cast<int32_t>(graceDeadline - millis()) > 0) {
        if (immediateSampleRequested ||
            (peerEventQueue != nullptr && uxQueueMessagesWaiting(peerEventQueue) > 0)) return;
        const EventBits_t bits = xEventGroupGetBits(systemEvents);
        if ((bits & (EVENT_LORA_BUSY | EVENT_NETWORK_BUSY)) == 0 && LoraManager::isIdleForLightSleep()) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    const EventBits_t finalBits = xEventGroupGetBits(systemEvents);
    if (immediateSampleRequested || !LoraManager::isIdleForLightSleep()) return;
    if ((finalBits & (EVENT_NETWORK_BUSY | EVENT_MAINTENANCE_ACTIVE)) != 0) {
        vTaskDelay(pdMS_TO_TICKS(waitMs < 100UL ? waitMs : 100UL));
        return;
    }

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    gpio_wakeup_enable(static_cast<gpio_num_t>(LORA_DIO1_PIN), GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(waitMs) * 1000ULL);
    esp_light_sleep_start();
    LoraManager::notifyIfIrqLineActive();
#else
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
#endif
}
} // namespace

namespace SensorManager {

void init() {
    Serial.println("[CORE 0] Sensörler başlatılıyor...");
    Wire.begin(SENSOR_I2C_SDA, SENSOR_I2C_SCL);
    Wire.setClock(400000);

    mlxInitialized = mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire);
    if (mlxInitialized) {
        mlx.setMode(MLX90640_CHESS);
        mlx.setRefreshRate(MLX90640_4_HZ);
        Serial.println("Termal Kamera: ✅");
    } else {
        Serial.println("Termal Kamera: ❌");
    }

    bmeInitialized = bme.begin(0x76, &Wire);
    if (bmeInitialized) {
        bme.setGasHeater(320, 150);
        Serial.println("BME680: ✅");
    } else {
        Serial.println("BME680: ❌");
    }
}

bool enqueuePeerEvent(const PeerEvent &event) {
    if (peerEventQueue == nullptr) return false;
    const bool queued = xQueueSend(peerEventQueue, &event, pdMS_TO_TICKS(50)) == pdPASS;
    if (queued && sensorTaskHandle != nullptr) xTaskNotifyGive(sensorTaskHandle);
    return queued;
}

bool isFastMode() {
    return static_cast<int32_t>(fastModeUntilMs - millis()) > 0;
}

void taskLoop(void *pvParameters) {
    sensorTaskHandle = xTaskGetCurrentTaskHandle();
    uint32_t lastSampleMs = 0;
    uint32_t nextSampleMs = millis() + 1000;
    uint32_t normalSampleCounter = 0;

    while (true) {
        processPeerEvents();
        const uint32_t now = millis();
        const bool due = static_cast<int32_t>(now - nextSampleMs) >= 0;
        if (due || immediateSampleRequested) {
            xEventGroupSetBits(systemEvents, EVENT_SENSOR_BUSY);
            const bool peerTriggered = immediateSampleRequested && peerTriggerSource.length() > 0;
            const String triggerSource = peerTriggerSource;
            const uint32_t triggerSequence = peerTriggerSequence;
            immediateSampleRequested = false;

            const float elapsedSeconds = lastSampleMs == 0
                ? 1.0f
                : max(0.001f, (now - lastSampleMs) / 1000.0f);
            lastSampleMs = now;

            SensorDataPacket packet;
            collectSample(packet, elapsedSeconds);
            const char *trigger = peerTriggered ? "PEER_ALERT" : (isFastMode() ? "FAST" : "TIMER");

            const uint32_t eventSequence = peerTriggered ? triggerSequence : packet.sequence;
            if (packet.fire_level >= FIRE_WATCH || peerTriggered) {
                rememberLocalEvent(packet, eventSequence);
            }

            StorageManager::logTelemetry(packet, trigger);
            StorageManager::logThermalFrame(packet);

            Serial.printf(
                "[SAMPLE] seq=%lu max=%.1f top5=%.1f amb=%.1f cluster=%u persist=%u "
                "dT=%.3f gasDrop=%.1f score=%.1f fire=%s health=%s bat=%d%% mode=%s\n",
                static_cast<unsigned long>(packet.sequence), packet.max_temp, packet.top5_temp,
                packet.ambient_temp, packet.largest_hotspot_cluster, packet.persistence_count,
                packet.local_delta_t, packet.gas_drop_pct, packet.local_fire_score,
                AlgorithmManager::fireLevelName(packet.fire_level),
                AlgorithmManager::healthLevelName(packet.health_level), packet.battery_pct,
                isFastMode() ? "FAST" : "NORMAL"
            );

            // WATCH ilk şüphede diğer direği hemen uyandırır.
            if (!peerTriggered && packet.fire_level >= FIRE_WATCH && packet.fire_level < FIRE_CONFIRMED) {
                const uint32_t alertNow = millis();
                const bool escalation = packet.fire_level > lastAlertTxLevel;
                const bool cooldownExpired = (alertNow - lastAlertTxMs) >= LORA_ALERT_COOLDOWN_MS;
                if (lastAlertTxMs == 0 || escalation || cooldownExpired) {
                    if (LoraManager::queueLocalAlert(packet)) {
                        lastAlertTxMs = alertNow;
                        lastAlertTxLevel = packet.fire_level;
                    }
                }
                fastModeUntilMs = millis() + FAST_MODE_DURATION_MS;
            } else if (!isFastMode() && packet.fire_level == FIRE_NORMAL) {
                lastAlertTxLevel = FIRE_NORMAL;
            }

            if (peerTriggered) {
                LoraManager::queuePeerReport(packet, triggerSource, triggerSequence);
                peerTriggerSource = "";
                peerTriggerSequence = 0;
            }

#if ENABLE_MAINTENANCE_WIFI
            // Alarm/hızlı doğrulama sırasında Wi-Fi taraması iletişimi ve ölçümü bölmesin.
            if (!peerTriggered && !isFastMode()) {
                normalSampleCounter++;
                if (normalSampleCounter == 1 ||
                    normalSampleCounter % MAINTENANCE_SCAN_EVERY_SAMPLES == 0) {
                    NetworkManager::requestMaintenanceScan();
                }
            }
#endif

            const uint32_t interval = isFastMode() ? FAST_SAMPLE_INTERVAL_MS : NORMAL_SAMPLE_INTERVAL_MS;
            nextSampleMs = millis() + interval;
            xEventGroupClearBits(systemEvents, EVENT_SENSOR_BUSY);
        }

        const uint32_t current = millis();
        uint32_t waitMs = 10;
        if (static_cast<int32_t>(nextSampleMs - current) > 0) waitMs = nextSampleMs - current;
        waitLowPower(waitMs);
    }
}

} // namespace SensorManager
