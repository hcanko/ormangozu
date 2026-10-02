#include "network_manager.h"
#include "globals.h"
#include "storage_manager.h"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

namespace {
AsyncWebServer server(80);
TaskHandle_t networkTaskHandle = nullptr;
bool routesConfigured = false;
bool serverStarted = false;

void configureRoutes() {
    if (routesConfigured) return;

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html",
            "<h2>Orman Gozu Bakim</h2>"
            "<p><a href='/status'>Durum</a></p>"
            "<p><a href='/logs/telemetry'>Telemetri CSV</a></p>"
            "<p><a href='/logs/lora'>LoRa CSV</a></p>"
            "<p><a href='/logs/fusion'>Fuzyon CSV</a></p>"
            "<p><a href='/logs/frames0'>Termal Frames 0</a></p>"
            "<p><a href='/logs/frames1'>Termal Frames 1</a></p>"
            "<p><a href='/logs/frames2'>Termal Frames 2</a></p>");
    });
    server.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "application/json", StorageManager::statusJson());
    });
    server.on("/logs/telemetry", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, StorageManager::telemetryPath(), "text/csv", true);
    });
    server.on("/logs/lora", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, StorageManager::loraLogPath(), "text/csv", true);
    });
    server.on("/logs/fusion", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, StorageManager::fusionLogPath(), "text/csv", true);
    });
    server.on("/logs/frames0", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, StorageManager::framePath(0), "application/octet-stream", true);
    });
    server.on("/logs/frames1", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, StorageManager::framePath(1), "application/octet-stream", true);
    });
    server.on("/logs/frames2", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(LittleFS, StorageManager::framePath(2), "application/octet-stream", true);
    });
    server.on("/logs/erase", HTTP_POST, [](AsyncWebServerRequest *request) {
        StorageManager::eraseAllLogs();
        request->send(200, "application/json", "{\"status\":\"erased\"}");
    });
    routesConfigured = true;
}

bool connectMaintenanceWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.begin(MAINTENANCE_WIFI_SSID, MAINTENANCE_WIFI_PASSWORD);
    const uint32_t deadline = millis() + MAINTENANCE_CONNECT_TIMEOUT_MS;
    while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(deadline - millis()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return WiFi.status() == WL_CONNECTED;
}
} // namespace

namespace NetworkManager {

void init() {
    WiFi.mode(WIFI_OFF);
    configureRoutes();
    Serial.println("📴 Wi-Fi normal operasyonda kapalı; yalnız bakım ağı görülürse açılacak.");
}

void requestMaintenanceScan() {
    if (networkTaskHandle != nullptr) xTaskNotifyGive(networkTaskHandle);
}

void taskLoop(void *pvParameters) {
    networkTaskHandle = xTaskGetCurrentTaskHandle();
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#if ENABLE_MAINTENANCE_WIFI
        xEventGroupSetBits(systemEvents, EVENT_NETWORK_BUSY);
        Serial.println("🔎 ORMAN_BAKIM ağı kontrol ediliyor...");
        if (!connectMaintenanceWifi()) {
            Serial.println("Bakım ağı yok; Wi-Fi kapatılıyor.");
            WiFi.disconnect(true, true);
            WiFi.mode(WIFI_OFF);
            xEventGroupClearBits(systemEvents, EVENT_NETWORK_BUSY);
            continue;
        }

        xEventGroupSetBits(systemEvents, EVENT_MAINTENANCE_ACTIVE);
        if (!serverStarted) {
            server.begin();
            serverStarted = true;
        }

        ArduinoOTA.setHostname(myTowerID.c_str());
        ArduinoOTA.begin();
        Serial.printf("🛠 Bakım aktif: http://%s/ | %lu saniye\n",
                      WiFi.localIP().toString().c_str(),
                      static_cast<unsigned long>(MAINTENANCE_WINDOW_MS / 1000));

        const uint32_t deadline = millis() + MAINTENANCE_WINDOW_MS;
        while (WiFi.status() == WL_CONNECTED && static_cast<int32_t>(deadline - millis()) > 0) {
            ArduinoOTA.handle();
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_OFF);
        xEventGroupClearBits(systemEvents, EVENT_MAINTENANCE_ACTIVE | EVENT_NETWORK_BUSY);
        Serial.println("📴 Bakım penceresi kapandı.");
#endif
    }
}

} // namespace NetworkManager
