#include "lora_manager.h"
#include "sensor_manager.h"
#include "storage_manager.h"

#include <RadioLib.h>
#include <cstring>

namespace {
SX1262 radio = new Module(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_NRST_PIN, LORA_BUSY_PIN);
volatile bool radioIrqFlag = false;
volatile bool radioBusy = false;
bool radioReady = false;
TaskHandle_t loraTaskHandle = nullptr;
uint32_t lastReceivedSequence = 0;
String lastReceivedSource = "";
volatile uint32_t waitingAckSequence = 0;
String waitingAckSource = "";
volatile bool waitingAckReceived = false;

void IRAM_ATTR onDio1() {
    radioIrqFlag = true;
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    if (loraTaskHandle != nullptr) {
        vTaskNotifyGiveFromISR(loraTaskHandle, &higherPriorityTaskWoken);
    }
    if (higherPriorityTaskWoken == pdTRUE) portYIELD_FROM_ISR();
}

int splitMessage(const String &message, String fields[], int maxFields) {
    int count = 0;
    int start = 0;
    while (count < maxFields) {
        const int sep = message.indexOf('|', start);
        if (sep < 0) {
            fields[count++] = message.substring(start);
            break;
        }
        fields[count++] = message.substring(start, sep);
        start = sep + 1;
    }
    return count;
}

String buildMessage(const char *type, const String &dst, uint32_t seq,
                    uint8_t fireLevel, uint8_t healthLevel, float score,
                    float maxTemp, uint16_t largestCluster,
                    uint8_t persistence, float gasDrop, int battery) {
    String p;
    p.reserve(220);
    p += "OG3|";
    p += type;
    p += "|" + myTowerID;
    p += "|" + dst;
    p += "|" + String(seq);
    p += "|" + String(fireLevel);
    p += "|" + String(healthLevel);
    p += "|" + String(score, 1);
    p += "|" + String(maxTemp, 1);
    p += "|" + String(largestCluster);
    p += "|" + String(persistence);
    p += "|" + String(gasDrop, 1);
    p += "|" + String(battery);
    return p;
}

bool startContinuousReceive() {
    if (!radioReady) return false;
    radioIrqFlag = false;
    const int16_t state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("❌ LoRa RX başlatma hatası: %d\n", state);
        return false;
    }
    return true;
}

bool transmitOnce(const String &payload) {
    if (!radioReady) return false;
    radioBusy = true;
    xEventGroupSetBits(systemEvents, EVENT_LORA_BUSY);
    radio.standby();
    radioIrqFlag = false;
    const int16_t state = radio.transmit(payload);
    const bool ok = state == RADIOLIB_ERR_NONE;
    if (!ok) Serial.printf("❌ LoRa TX hatası: %d\n", state);
    radioIrqFlag = false;  // TX-done IRQ'sini RX paketi sanma.
    startContinuousReceive();
    radioBusy = false;
    xEventGroupClearBits(systemEvents, EVENT_LORA_BUSY);
    return ok;
}

void fillTxMessage(LoraTxMessage &msg, const char *type, const String &destination,
                   uint32_t sequence, bool requireAck, const String &payload) {
    memset(&msg, 0, sizeof(msg));
    msg.sequence = sequence;
    msg.require_ack = requireAck;
    payload.toCharArray(msg.payload, sizeof(msg.payload));
    strncpy(msg.type, type, sizeof(msg.type) - 1);
    destination.toCharArray(msg.destination, sizeof(msg.destination));
}

void sendAck(const String &destination, uint32_t seq, int battery = 0) {
    delay(random(25, 110));
    const String ack = buildMessage("ACK", destination, seq, FIRE_NORMAL, HEALTH_OK,
                                    0.0f, 0.0f, 0, 0, 0.0f, battery);
    const bool ok = transmitOnce(ack);
    StorageManager::logLoraEvent("TX", "ACK", destination, seq, 0, 0, ok ? "ok" : "tx_error");
}

PeerEvent makePeerEvent(uint8_t type, const String &source, uint32_t seq,
                        uint8_t fireLevel, uint8_t healthLevel, float score,
                        float maxTemp, uint16_t cluster, uint8_t persistence,
                        float gasDrop, float rssi, float snr) {
    PeerEvent event = {};
    event.type = type;
    source.toCharArray(event.source, sizeof(event.source));
    event.sequence = seq;
    event.fire_level = fireLevel;
    event.health_level = healthLevel;
    event.fire_score = score;
    event.max_temp = maxTemp;
    event.largest_cluster = cluster;
    event.persistence_count = persistence;
    event.gas_drop_pct = gasDrop;
    event.rssi = rssi;
    event.snr = snr;
    return event;
}

void processIncoming() {
    if (!radioIrqFlag || !radioReady || radioBusy) return;
    radioIrqFlag = false;

    String incoming;
    const int16_t state = radio.readData(incoming);
    const float rssi = radio.getRSSI();
    const float snr = radio.getSNR();

    if (state != RADIOLIB_ERR_NONE) {
        StorageManager::logLoraEvent("RX", "ERROR", "?", 0, rssi, snr, String(state));
        startContinuousReceive();
        return;
    }

    String f[13];
    const int count = splitMessage(incoming, f, 13);
    if (count != 13 || f[0] != "OG3") {
        StorageManager::logLoraEvent("RX", "INVALID", "?", 0, rssi, snr, incoming);
        startContinuousReceive();
        return;
    }

    const String type = f[1];
    const String src = f[2];
    const String dst = f[3];
    const uint32_t seq = static_cast<uint32_t>(strtoul(f[4].c_str(), nullptr, 10));
    const uint8_t fireLevel = static_cast<uint8_t>(f[5].toInt());
    const uint8_t healthLevel = static_cast<uint8_t>(f[6].toInt());
    const float score = f[7].toFloat();
    const float maxTemp = f[8].toFloat();
    const uint16_t largestCluster = static_cast<uint16_t>(f[9].toInt());
    const uint8_t persistence = static_cast<uint8_t>(f[10].toInt());
    const float gasDrop = f[11].toFloat();

    if (src == myTowerID || (dst != "*" && dst != myTowerID)) {
        startContinuousReceive();
        return;
    }

    StorageManager::logLoraEvent("RX", type, src, seq, rssi, snr, incoming);

    if (type == "ACK") {
        const bool sourceMatches = waitingAckSource == "*" || waitingAckSource == src;
        if (waitingAckSequence == seq && sourceMatches) waitingAckReceived = true;
    } else if (type == "ALERT") {
        const bool duplicate = (src == lastReceivedSource && seq == lastReceivedSequence);
        sendAck(src, seq);
        if (!duplicate) {
            lastReceivedSource = src;
            lastReceivedSequence = seq;
            Serial.printf("🚨 PEER ALERT %s | level=%u score=%.1f max=%.1f cluster=%u persist=%u\n",
                          src.c_str(), fireLevel, score, maxTemp, largestCluster, persistence);
            const PeerEvent event = makePeerEvent(
                PEER_EVENT_ALERT, src, seq, fireLevel, healthLevel, score,
                maxTemp, largestCluster, persistence, gasDrop, rssi, snr
            );
            SensorManager::enqueuePeerEvent(event);
        }
    } else if (type == "REPORT") {
        sendAck(src, seq);
        const PeerEvent event = makePeerEvent(
            PEER_EVENT_REPORT, src, seq, fireLevel, healthLevel, score,
            maxTemp, largestCluster, persistence, gasDrop, rssi, snr
        );
        SensorManager::enqueuePeerEvent(event);
    } else if (type == "CONFIRM") {
        sendAck(src, seq);
        const PeerEvent event = makePeerEvent(
            PEER_EVENT_CONFIRMED, src, seq, FIRE_CONFIRMED, healthLevel, score,
            maxTemp, largestCluster, persistence, gasDrop, rssi, snr
        );
        SensorManager::enqueuePeerEvent(event);
    } else if (type == "PING") {
        sendAck(src, seq);
    }

    startContinuousReceive();
}

bool sendWithRetries(const LoraTxMessage &msg) {
    waitingAckSequence = msg.require_ack ? msg.sequence : 0;
    waitingAckSource = msg.require_ack ? String(msg.destination) : "";
    waitingAckReceived = false;

    for (uint8_t attempt = 0; attempt < LORA_TX_RETRIES; ++attempt) {
        if (attempt > 0) delay(random(LORA_RETRY_BACKOFF_MIN_MS, LORA_RETRY_BACKOFF_MAX_MS));
        const bool sent = transmitOnce(String(msg.payload));
        StorageManager::logLoraEvent("TX", String(msg.type), String(msg.destination), msg.sequence, 0, 0,
                                     sent ? String("attempt=") + String(attempt + 1)
                                          : String("tx_error"));
        if (!sent) continue;
        if (!msg.require_ack) return true;

        const uint32_t deadline = millis() + LORA_ACK_TIMEOUT_MS;
        while (static_cast<int32_t>(deadline - millis()) > 0) {
            if (radioIrqFlag) processIncoming();
            if (waitingAckReceived) {
                waitingAckSequence = 0;
                waitingAckSource = "";
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    waitingAckSequence = 0;
    waitingAckSource = "";
    StorageManager::logLoraEvent("TX", "NO_ACK", String(msg.destination), msg.sequence, 0, 0,
                                 String(msg.type) + ":retries_exhausted");
    return false;
}
} // namespace

namespace LoraManager {

bool init() {
    Serial.println("[CORE 1] LoRa sürekli dinleme başlatılıyor (OG3)...");
    const int16_t state = radio.begin(
        LORA_FREQUENCY_MHZ,
        LORA_BANDWIDTH_KHZ,
        LORA_SPREADING_FACTOR,
        LORA_CODING_RATE,
        LORA_SYNC_WORD,
        LORA_TX_POWER_DBM,
        LORA_PREAMBLE_SYMBOLS,
        LORA_TCXO_VOLTAGE
    );
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("❌ LoRa başlatma hatası: %d\n", state);
        return false;
    }

    radio.setCRC(2);
    radio.setDio1Action(onDio1);
    radioReady = true;
    Serial.println("✅ LoRa hazır; sürekli RX Core 1 üzerinde çalışacak.");
    return true;
}

void taskLoop(void *pvParameters) {
    loraTaskHandle = xTaskGetCurrentTaskHandle();
    if (radioReady) startContinuousReceive();

    LoraTxMessage outgoing = {};
    while (true) {
        if (radioIrqFlag) processIncoming();

        if (loraTxQueue != nullptr && xQueueReceive(loraTxQueue, &outgoing, pdMS_TO_TICKS(20)) == pdPASS) {
            sendWithRetries(outgoing);
        }

        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30));
    }
}

bool queueLocalAlert(const SensorDataPacket &p) {
    if (!radioReady || loraTxQueue == nullptr) return false;
    LoraTxMessage msg = {};
    const String payload = buildMessage(
        "ALERT", "*", p.sequence, p.fire_level, p.health_level,
        p.local_fire_score, p.max_temp, p.largest_hotspot_cluster,
        p.persistence_count, p.gas_drop_pct, p.battery_pct
    );
    fillTxMessage(msg, "ALERT", "*", p.sequence, true, payload);
    return xQueueSend(loraTxQueue, &msg, pdMS_TO_TICKS(100)) == pdPASS;
}

bool queuePeerReport(const SensorDataPacket &p, const String &triggerSource, uint32_t triggerSequence) {
    if (!radioReady || loraTxQueue == nullptr) return false;
    LoraTxMessage msg = {};
    const String payload = buildMessage(
        "REPORT", triggerSource, triggerSequence, p.fire_level, p.health_level,
        p.local_fire_score, p.max_temp, p.largest_hotspot_cluster,
        p.persistence_count, p.gas_drop_pct, p.battery_pct
    );
    fillTxMessage(msg, "REPORT", triggerSource, triggerSequence, true, payload);
    return xQueueSend(loraTxQueue, &msg, pdMS_TO_TICKS(100)) == pdPASS;
}

bool queueConfirmation(const SensorDataPacket &p, const String &peer, uint32_t eventSequence) {
    if (!radioReady || loraTxQueue == nullptr) return false;
    LoraTxMessage msg = {};
    const String payload = buildMessage(
        "CONFIRM", peer, eventSequence, FIRE_CONFIRMED, p.health_level,
        p.local_fire_score, p.max_temp, p.largest_hotspot_cluster,
        p.persistence_count, p.gas_drop_pct, p.battery_pct
    );
    fillTxMessage(msg, "CONFIRM", peer, eventSequence, true, payload);
    return xQueueSend(loraTxQueue, &msg, pdMS_TO_TICKS(100)) == pdPASS;
}

bool isReady() { return radioReady; }

bool isIdleForLightSleep() {
    if (!radioReady || radioBusy || radioIrqFlag) return false;
    if (loraTxQueue != nullptr && uxQueueMessagesWaiting(loraTxQueue) > 0) return false;
    return digitalRead(LORA_DIO1_PIN) == LOW;
}

void notifyIfIrqLineActive() {
    if (digitalRead(LORA_DIO1_PIN) == HIGH) {
        radioIrqFlag = true;
        if (loraTaskHandle != nullptr) xTaskNotifyGive(loraTaskHandle);
    }
}

} // namespace LoraManager
