#include "network_manager.h"
#include "globals.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>

// Güvenlik için gerçek Wi-Fi şifresini koda açık yazmamak daha doğru.
// Testte kendi değerlerinle değiştir.
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Backend IP adresini kendi FastAPI sunucuna göre güncelle.
const char* BACKEND_URL = "http://192.168.1.16:8000/api/sensor-data";

AsyncWebServer server(80);

static bool routesConfigured = false;
static bool otaStarted = false;
static unsigned long lastReconnectAttempt = 0;

namespace NetworkManager {

    void configureRoutesOnce() {
        if (routesConfigured) return;

        server.on("/sleep", HTTP_POST, [](AsyncWebServerRequest *request) {
            if (request->hasParam("value")) {
                sleep_interval = request->getParam("value")->value().toInt();
                request->send(200, "text/plain", "Hiz guncellendi");
            } else {
                request->send(400, "text/plain", "value parametresi eksik");
            }
        });

        server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        });

        server.begin();
        routesConfigured = true;
        Serial.println("🌐 Web server aktif.");
    }

    void startOTAOnce() {
        if (otaStarted || WiFi.status() != WL_CONNECTED) return;

        ArduinoOTA.setHostname(myTowerID.c_str());

        ArduinoOTA.onStart([]() {
            String type = (ArduinoOTA.getCommand() == U_FLASH) ? "Sketch" : "Filesystem";
            Serial.println("OTA Güncellemesi Başladı: " + type);
        });

        ArduinoOTA.onEnd([]() {
            Serial.println("\n✅ OTA Güncellemesi Başarıyla Tamamlandı!");
        });

        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            Serial.printf("OTA İlerlemesi: %u%%\r", (progress / (total / 100)));
        });

        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("OTA Hatası [%u]\n", error);
        });

        ArduinoOTA.begin();
        otaStarted = true;

        Serial.println("☁️ OTA sistemi aktif.");
    }

    void init() {
        Serial.println("[CORE 1] Ağ başlatılıyor...");

        WiFi.mode(WIFI_STA);
        WiFi.setTxPower(WIFI_POWER_8_5dBm);
        WiFi.begin(ssid, password);

        // Cihaz ID'sini Wi-Fi bağlantısı olmasa bile MAC üzerinden üret.
        String mac = WiFi.macAddress();
        mac.replace(":", "");
        myTowerID = "TOWER-" + mac.substring(mac.length() - 4);

        Serial.print("🤖 Cihaz Kimliği: ");
        Serial.println(myTowerID);

        // Wi-Fi için sonsuza kadar bekleme. LoRa ve sensörler Wi-Fi yokken de çalışabilsin.
        unsigned long startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            localIP = WiFi.localIP().toString();
            Serial.println("\n✅ WI-FI BAĞLANDI");
            Serial.print("🌐 IP Adresi: ");
            Serial.println(localIP);
        } else {
            localIP = "0.0.0.0";
            Serial.println("\n⚠️ Wi-Fi bağlantısı kurulamadı. Sistem LoRa/sensör modunda devam ediyor.");
        }

        configureRoutesOnce();
        startOTAOnce();
    }

    void taskLoop(void *pvParameters) {
        SensorDataPacket incomingData;

        while (true) {
            // Wi-Fi koparsa periyodik reconnect dene
            if (WiFi.status() != WL_CONNECTED) {
                if (millis() - lastReconnectAttempt > 10000) {
                    lastReconnectAttempt = millis();
                    Serial.println("🔄 Wi-Fi yeniden bağlanmayı deniyor...");
                    WiFi.reconnect();
                }
            } else {
                localIP = WiFi.localIP().toString();
                startOTAOnce();

                if (otaStarted) {
                    ArduinoOTA.handle();
                }
            }

            if (xQueueReceive(networkDataQueue, &incomingData, pdMS_TO_TICKS(50)) == pdPASS) {
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("[CORE 1] Kuyruktan veri alındı, JSON Wi-Fi ile iletiliyor...");

                    String json;
                    json.reserve(6500);

                    json += "{";
                    json += "\"device_id\":\"" + myTowerID + "\",";
                    json += "\"ip\":\"" + localIP + "\",";

                    json += "\"max_temp\":" + String(incomingData.max_temp, 2) + ",";
                    json += "\"gas_raw_resistance\":" + String(incomingData.gas_res, 2) + ",";

                    json += "\"battery_level\":" + String(incomingData.battery_pct) + ",";
                    json += "\"battery_mv\":" + String(incomingData.battery_mv, 1) + ",";

                    json += "\"lat\":" + String(incomingData.lat, 6) + ",";
                    json += "\"lng\":" + String(incomingData.lng, 6) + ",";

                    json += "\"alert_level\":" + String(incomingData.alert_level) + ",";

                    json += "\"mlx_ok\":";
                    json += incomingData.mlx_ok ? "true," : "false,";

                    json += "\"gas_ok\":";
                    json += incomingData.gas_ok ? "true," : "false,";

                    json += "\"gps_fix\":";
                    json += incomingData.gps_fix ? "true," : "false,";

                    json += "\"servo_angle\":" + String(incomingData.servo_angle) + ",";
                    json += "\"uptime_ms\":" + String(incomingData.uptime_ms) + ",";
                    json += "\"read_counter\":" + String(incomingData.read_counter) + ",";
                    json += "\"error_counter\":" + String(incomingData.error_counter) + ",";

                    json += "\"pixels\":[";

                    for (int i = 0; i < 768; i++) {
                        // Backend ve frontend için veri hacmini azaltmak adına int gönderiyoruz.
                        int pVal = constrain((int)incomingData.pixels[i], 0, 255);
                        json += String(pVal);
                        if (i < 767) json += ",";
                    }

                    json += "]}";

                    HTTPClient http;
                    http.begin(BACKEND_URL);
                    http.addHeader("Content-Type", "application/json");

                    int httpResponseCode = http.POST(json);

                    if (httpResponseCode == 200) {
                        Serial.println("✅ Veri Wi-Fi üzerinden iletildi.");
                    } else {
                        Serial.print("❌ FastAPI bağlantı hatası! Kod: ");
                        Serial.println(httpResponseCode);
                    }

                    http.end();
                } else {
                    Serial.println("⚠️ Wi-Fi yok. Tam telemetri paketi backend'e gönderilemedi.");
                }
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
