#include "lora_manager.h"
#include "globals.h"

#include <RadioLib.h>

// Heltec V4 / SX1262 pinleri
#define NSS_PIN   8
#define DIO1_PIN  14
#define NRST_PIN  12
#define BUSY_PIN  13

SX1262 radio = new Module(NSS_PIN, DIO1_PIN, NRST_PIN, BUSY_PIN);

// LBT parametreleri
#define RSSI_LBT_THRESHOLD -85.0
#define MAX_LBT_RETRIES 5

namespace LoraManager {

    void init() {
        Serial.println("[CORE 1] LoRa SX1262 başlatılıyor...");

        // Frekans: 868 MHz
        // BW: 125 kHz
        // SF: 9
        // CR: 7
        // Output power: 18 dBm
        // Preamble: 10
        int state = radio.begin(868.0, 125.0, 9, 7, 18, 10, 8, 1.7);

        if (state == RADIOLIB_ERR_NONE) {
            Serial.println("✅ LoRa modülü başarıyla başlatıldı.");
        } else {
            Serial.print("❌ LoRa başlatma hatası, kod: ");
            Serial.println(state);
        }
    }

    bool listenBeforeTalk() {
        for (int attempt = 0; attempt < MAX_LBT_RETRIES; attempt++) {
            radio.standby();
            radio.startReceive();
            delay(5);

            float currentRssi = radio.getRSSI();

            if (currentRssi < RSSI_LBT_THRESHOLD) {
                Serial.println("📡 [LBT] Kanal boş. Gönderime geçiliyor.");
                radio.standby();
                return true;
            }

            int backoffMs = random(50, 250);

            Serial.printf(
                "⚠️ [LBT] Kanal dolu. RSSI: %.1f dBm. %d ms bekleniyor. Deneme: %d/%d\n",
                currentRssi,
                backoffMs,
                attempt + 1,
                MAX_LBT_RETRIES
            );

            delay(backoffMs);
        }

        Serial.println("❌ [LBT] Kanal sürekli meşgul. LoRa paketi iptal edildi.");
        return false;
    }

    String buildAlarmPayload(const SensorDataPacket &data) {
        String payload = "";

        payload += "OG;";
        payload += myTowerID;
        payload += ";L=" + String(data.alert_level);
        payload += ";T=" + String(data.max_temp, 1);
        payload += ";G=" + String((int)data.gas_res);
        payload += ";B=" + String(data.battery_pct);
        payload += ";BMV=" + String((int)data.battery_mv);
        payload += ";A=" + String(data.servo_angle);
        payload += ";U=" + String(data.uptime_ms / 1000);
        payload += ";E=" + String(data.error_counter);
        payload += ";LAT=" + String(data.lat, 6);
        payload += ";LNG=" + String(data.lng, 6);

        return payload;
    }

    void taskLoop(void *pvParameters) {
        SensorDataPacket loraData;

        while (true) {
            if (loraAlertQueue != NULL &&
                xQueueReceive(loraAlertQueue, &loraData, pdMS_TO_TICKS(500)) == pdPASS) {

                if (loraData.alert_level < ALERT_WARNING) {
                    continue;
                }

                if (listenBeforeTalk()) {
                    String payload = buildAlarmPayload(loraData);

                    int state = radio.transmit(payload);

                    if (state == RADIOLIB_ERR_NONE) {
                        Serial.print("✅ LoRa alarm paketi gönderildi: ");
                        Serial.println(payload);
                    } else {
                        Serial.print("❌ LoRa gönderim hatası, kod: ");
                        Serial.println(state);
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
