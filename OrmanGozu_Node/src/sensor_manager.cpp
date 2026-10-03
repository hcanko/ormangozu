#include "sensor_manager.h"
#include "algorithm_manager.h"
#include "lora_manager.h"
#include "storage_manager.h"
#include "network_manager.h"
#include "control_manager.h"
#include "ota_manager.h"

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
uint32_t peerTriggerBoot = 0;
// One ongoing scan per two-Nest pilot; report the strongest sample after 30s.
struct PeerScan { bool active = false; uint32_t started_ms = 0; uint32_t origin_boot = 0;
                  uint32_t sequence = 0; String source; SensorDataPacket best = {}; bool has_best = false; };
PeerScan peerScan;
uint32_t peerTriggerSequence = 0;
RTC_DATA_ATTR uint32_t persistentSequence = 0;
uint32_t errorCounter = 0;
uint32_t lastAlertTxMs = 0;
uint8_t lastAlertTxLevel = FIRE_NORMAL;

struct LocalEventRecord {
    bool valid;
    bool confirmed;
    uint32_t event_sequence;
    uint32_t origin_boot;
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

LocalEventRecord *findLocalEvent(uint32_t eventSequence, uint32_t originBoot) {
    for (uint8_t i = 0; i < LOCAL_EVENT_RING_SIZE; ++i) {
        if (localEvents[i].valid && localEvents[i].event_sequence == eventSequence && localEvents[i].origin_boot == originBoot) {
            return &localEvents[i];
        }
    }
    return nullptr;
}

void rememberLocalEvent(const SensorDataPacket &packet, uint32_t eventSequence, uint32_t originBoot) {
    LocalEventRecord *existing = findLocalEvent(eventSequence, originBoot);
    LocalEventRecord *slot = existing;
    if (slot == nullptr) {
        slot = &localEvents[nextLocalEventSlot];
        nextLocalEventSlot = (nextLocalEventSlot + 1) % LOCAL_EVENT_RING_SIZE;
    }
    slot->valid = true;
    slot->confirmed = false;
    slot->event_sequence = eventSequence;
    slot->origin_boot = originBoot;
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
    // No solar current/voltage sensor is wired in the baseline: -1 = NOT MEASURED.
    packet.solar_voltage_mv = -1.0f;
    packet.solar_current_ma = -1.0f;

    AlgorithmManager::analyze(packet, elapsedSeconds);
    packet.error_counter = errorCounter;
    return packet.mlx_ok || packet.gas_ok;
}

void processPeerReport(const PeerEvent &event) {
    if (event.origin_boot != myBootCounter) return;
    LocalEventRecord *local = findLocalEvent(event.sequence, event.origin_boot);
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
            local->fire_level = FIRE_NETWORK_CORROBORATED;
            localPacket.fire_level = FIRE_NETWORK_CORROBORATED;
            localPacket.network_confirmed = true;
            StorageManager::logFusionEvent("NETWORK_CORROBORATED", event.sequence, &localPacket, &event,
                                           "ağ destekli şüphe; kesin yangın tanısı değildir");
            LoraManager::queueConfirmation(localPacket, String(event.source), event.sequence);
            Serial.printf("✅ NETWORK CORROBORATED event=%lu peer=%s\n",
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
            ControlManager::forceAuto("peer fire watch");
            if (peerScan.active) {
                StorageManager::logFusionEvent("PEER_SCAN_BUSY", event.sequence, nullptr, &event,
                                                "ongoing 30s scan has priority");
                continue;
            }
            peerScan.active = true;
            peerScan.has_best = false;
            peerScan.started_ms = millis();
            peerScan.origin_boot = event.origin_boot;
            peerScan.sequence = event.sequence;
            peerScan.source = String(event.source);
            peerTriggerSource = String(event.source);
            peerTriggerSequence = event.sequence;
            peerTriggerBoot = event.origin_boot;
            fastModeUntilMs = millis() + FAST_MODE_DURATION_MS;
            immediateSampleRequested = true;
            StorageManager::logFusionEvent("PEER_WAKE", event.sequence, nullptr, &event,
                                           "eş alarmı hızlı örneklemeyi tetikledi");
        } else if (event.type == PEER_EVENT_REPORT) {
            processPeerReport(event);
        } else if (event.type == PEER_EVENT_CONFIRMED) {
            LocalEventRecord *local = findLocalEvent(event.sequence, event.origin_boot);
            if (local != nullptr) {
                local->confirmed = true;
                local->fire_level = FIRE_NETWORK_CORROBORATED;
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

String statusJson() {
    return String("{\"storage\":") + (StorageManager::isReady() ? "true" : "false") +
           ",\"lora\":" + (LoraManager::isReady() ? "true" : "false") +
           ",\"thermal\":" + (mlxInitialized ? "true" : "false") +
           ",\"environmental\":" + (bmeInitialized ? "true" : "false") + "}";
}

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

void requestImmediateSample() {
    immediateSampleRequested = true;
    if (sensorTaskHandle) xTaskNotifyGive(sensorTaskHandle);
}
void requestFastMode() {
    fastModeUntilMs = millis() + FAST_MODE_DURATION_MS;
}
bool enqueueControlRequest(const ControlRequest &request) {
    if (!remoteControlQueue) return false;
    const bool queued = xQueueSend(remoteControlQueue, &request, 0) == pdPASS;
    if (queued && sensorTaskHandle) xTaskNotifyGive(sensorTaskHandle);
    return queued;
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
    uint32_t lastTelemetrySaved = 0;
    uint32_t lastFrameSaved = 0;

    while (true) {
        processPeerEvents();
        ControlManager::processPending();
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
            ControlManager::onSample(packet);
            const char *trigger = peerTriggered ? "PEER_ALERT" : (isFastMode() ? "FAST" : "TIMER");

            const uint32_t eventSequence = peerTriggered ? triggerSequence : packet.sequence;
            if (peerScan.active) {
                if (!peerScan.has_best || packet.local_fire_score > peerScan.best.local_fire_score) {
                    peerScan.best = packet;
                    peerScan.has_best = true;
                    rememberLocalEvent(packet, peerScan.sequence, peerScan.origin_boot);
                }
            }

            const uint32_t savedNow = millis();
            // Volatile safety snapshot for remote OTA commit gating; avoid flashing
            // during alarms or from a depleted battery.
            // Confirm a freshly booted OTA partition only after the local thermal
            // loop and radio are actually usable, not merely after setup().
            if (packet.mlx_ok && packet.gas_ok && LoraManager::isReady())
                OtaManager::confirmBootIfHealthy();
            gCurrentFireLevel = packet.fire_level;
            gBatteryPct = packet.battery_pct;
            if (packet.fire_level >= FIRE_WATCH) gLastFireWatchMs = millis();
            const bool noteworthy = packet.fire_level >= FIRE_WATCH || peerTriggered || isFastMode();
            if (noteworthy || lastTelemetrySaved == 0 ||
                savedNow - lastTelemetrySaved >= TELEMETRY_BASELINE_LOG_MS) {
                StorageManager::logTelemetry(packet, trigger);
                lastTelemetrySaved = savedNow;
            }
            if (noteworthy || lastFrameSaved == 0 ||
                savedNow - lastFrameSaved >= FRAME_BASELINE_LOG_MS) {
                StorageManager::logThermalFrame(packet);
                lastFrameSaved = savedNow;
            }
            // Lightweight structured sample for the PC. Raw pixels stay in
            // LittleFS, NOT in the LoRa/USB live telemetry event.
            Serial.printf(
                "OGTELEM:{\"device_id\":\"%s\",\"boot\":%lu,\"sequence\":%lu,"
                "\"uptime_ms\":%lu,\"firmware\":\"%s\",\"max_temp\":%.2f,"
                "\"ambient_temp\":%.2f,\"top5_temp\":%.2f,\"hotspot_threshold\":%.2f,"
                "\"cluster\":%u,\"persistence\":%u,\"gas_res\":%.2f,"
                "\"gas_drop_pct\":%.2f,\"score\":%.1f,\"level\":%u,\"health\":%u,"
                "\"battery_mv\":%.1f,\"battery_pct\":%d,\"mlx_ok\":%s,\"gas_ok\":%s}\n",
                myTowerID.c_str(), static_cast<unsigned long>(myBootCounter),
                static_cast<unsigned long>(packet.sequence), static_cast<unsigned long>(packet.uptime_ms),
                FIRMWARE_VERSION, packet.max_temp, packet.ambient_temp, packet.top5_temp,
                packet.hotspot_threshold, static_cast<unsigned>(packet.largest_hotspot_cluster),
                static_cast<unsigned>(packet.persistence_count), packet.gas_res,
                packet.gas_drop_pct, packet.local_fire_score, static_cast<unsigned>(packet.fire_level),
                static_cast<unsigned>(packet.health_level), packet.battery_mv, packet.battery_pct,
                packet.mlx_ok ? "true" : "false", packet.gas_ok ? "true" : "false");

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
            if (!peerScan.active && !peerTriggered && packet.fire_level >= FIRE_WATCH && packet.fire_level < FIRE_NETWORK_CORROBORATED) {
                const uint32_t alertNow = millis();
                const bool escalation = packet.fire_level > lastAlertTxLevel;
                const bool cooldownExpired = (alertNow - lastAlertTxMs) >= LORA_ALERT_COOLDOWN_MS;
                if (lastAlertTxMs == 0 || escalation || cooldownExpired) {
                    if (LoraManager::queueLocalAlert(packet)) {
                        // Retain ONLY emitted alert events, not every 2s fast-mode
                        // sample. Otherwise a 6-slot ring overwrites the event
                        // before the peer's 30-second REPORT arrives.
                        rememberLocalEvent(packet, packet.sequence, myBootCounter);
                        lastAlertTxMs = alertNow;
                        lastAlertTxLevel = packet.fire_level;
                    }
                }
                fastModeUntilMs = millis() + FAST_MODE_DURATION_MS;
            } else if (!isFastMode() && packet.fire_level == FIRE_NORMAL) {
                lastAlertTxLevel = FIRE_NORMAL;
            }

            if (peerScan.active && peerScan.has_best &&
                millis() - peerScan.started_ms >= PEER_SCAN_WINDOW_MS) {
                LoraManager::queuePeerReport(peerScan.best, peerScan.source,
                                             peerScan.origin_boot, peerScan.sequence);
                StorageManager::logFusionEvent("PEER_SCAN_COMPLETE", peerScan.sequence,
                    &peerScan.best, nullptr, "30s rapid scan report queued");
                peerScan.active = false;
                peerTriggerSource = "";
                peerTriggerSequence = 0;
                peerTriggerBoot = 0;
            }

// Wi-Fi is deliberately NOT scanned periodically; maintenance needs a physical USB command.

            const uint32_t interval = isFastMode() ? FAST_SAMPLE_INTERVAL_MS : NORMAL_SAMPLE_INTERVAL_MS;
            nextSampleMs = millis() + interval;
            xEventGroupClearBits(systemEvents, EVENT_SENSOR_BUSY);
        }

        const uint32_t current = millis();
        uint32_t waitMs = 10;
        if (static_cast<int32_t>(nextSampleMs - current) > 0) waitMs = nextSampleMs - current;
        // Queue notifications wake SensorTask immediately. Only manual mode
        // needs a frequent deadline check; preserve normal pilot power budget.
        waitLowPower(ControlManager::manualActive() && waitMs > 1000UL ? 1000UL : waitMs);
    }
}

} // namespace SensorManager
