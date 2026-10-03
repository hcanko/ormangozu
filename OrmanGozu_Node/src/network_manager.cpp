#include "network_manager.h"
#include "globals.h"
#include "storage_manager.h"
#include "lora_manager.h"
#include "ota_manager.h"
#include "sensor_manager.h"
#include <cstdio>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

namespace {
WebServer server(80);
bool serverConfigured = false;
bool otaActive = false;
uint32_t localSignedOtaActivityMs = 0;

bool authorized() {
    if (server.authenticate(MAINTENANCE_HTTP_USER, OG_MAINT_HTTP_PASSWORD)) return true;
    server.requestAuthentication();
    return false;
}

void logDownload(const char *path, const char *contentType) {
    if (!authorized()) return;
    if (!StorageManager::isReady()) {
        server.send(503, "text/plain", "storage not mounted");
        return;
    }
    File file = LittleFS.open(path, FILE_READ);
    if (!file) {
        server.send(404, "text/plain", "file not found");
        return;
    }
    server.sendHeader("Content-Disposition", String("attachment; filename=\"") + (path + 1) + "\"");
    server.streamFile(file, contentType);
    file.close();
}

void configureRoutes() {
    if (serverConfigured) return;
    server.on("/", HTTP_GET, []() {
        if (!authorized()) return;
        server.send(200, "text/html", "<h3>Orman Gozu local maintenance</h3>"
            "<p><a href='/status'>Status</a> | <a href='/logs/telemetry0'>Telemetry</a> "
            "| <a href='/logs/telemetry1'>Older telemetry</a></p>"
            "<p><a href='/logs/lora0'>LoRa</a> | <a href='/logs/lora1'>Older LoRa</a> "
            "| <a href='/logs/fusion0'>Fusion</a> | <a href='/logs/fusion1'>Older fusion</a></p>"
            "<p><a href='/logs/frames0'>Thermal frames 0</a> "
            "| <a href='/logs/frames1'>Thermal frames 1</a></p>"
            "<p>Secure signed local OTA: POST /ota/step with OGOTA1 fields. Legacy ArduinoOTA disabled.</p>");
    });
    server.on("/status", HTTP_GET, []() {
        if (!authorized()) return;
        server.send(200, "application/json", StorageManager::statusJson());
    });
    server.on("/logs/telemetry0", HTTP_GET, []() { logDownload("/telemetry_0.csv", "text/csv"); });
    server.on("/logs/telemetry1", HTTP_GET, []() { logDownload("/telemetry_1.csv", "text/csv"); });
    server.on("/logs/lora0", HTTP_GET, []() { logDownload("/lora_0.csv", "text/csv"); });
    server.on("/logs/lora1", HTTP_GET, []() { logDownload("/lora_1.csv", "text/csv"); });
    server.on("/logs/fusion0", HTTP_GET, []() { logDownload("/fusion_0.csv", "text/csv"); });
    server.on("/logs/fusion1", HTTP_GET, []() { logDownload("/fusion_1.csv", "text/csv"); });
    server.on("/logs/frames0", HTTP_GET, []() { logDownload("/frames_0.bin", "application/octet-stream"); });
    server.on("/logs/frames1", HTTP_GET, []() { logDownload("/frames_1.bin", "application/octet-stream"); });
    // Signed local update via WPA2 maintenance AP + HTTP Basic. The image still
    // needs the OFFLINE ECDSA signature; ArduinoOTA is disabled in secure mode.
    server.on("/ota/step", HTTP_POST, []() {
        if (!authorized()) return;
        if (!server.hasArg("kind") || !server.hasArg("tid") ||
            !server.hasArg("param") || !server.hasArg("data")) {
            server.send(400,"text/plain","missing signed OTA fields");return;
        }
        String f[8]={"OGU1",server.arg("kind"),String(OG_CONTROLLER_ID),myTowerID,
                     server.arg("tid"),server.arg("param"),server.arg("data"),"0"};
        if(f[6].length()>134 || f[4].length()!=8) {
           server.send(413,"text/plain","OTA step too large");return;
        }
        String status;uint32_t offset=0;
        if(!OtaManager::handle(f,status,offset)) {
           server.send(400,"text/plain","invalid signed OTA step");return;
        }
        localSignedOtaActivityMs=millis();
        server.send(200,"application/json",String("{\"status\":\"")+status+
            "\",\"offset\":"+String(offset)+"}");
    });
    // Deliberately no remote erase or arbitrary actuator endpoint in the pilot.
    serverConfigured = true;
}

void runMaintenance() {
    static_assert(sizeof(OG_MAINT_AP_PASSWORD) >= 13, "Use a unique 12+ character AP password");
    static_assert(sizeof(OG_MAINT_HTTP_PASSWORD) >= 13, "Use a unique 12+ character HTTP password");
    static_assert(sizeof(OG_OTA_PASSWORD) >= 13, "Use a unique 12+ character OTA password");
    xEventGroupSetBits(systemEvents, EVENT_NETWORK_BUSY | EVENT_MAINTENANCE_ACTIVE);
    const String ssid = String(MAINTENANCE_AP_SSID_PREFIX) + myTowerID.substring(myTowerID.length()-6);
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid.c_str(), OG_MAINT_AP_PASSWORD)) {
        Serial.println("[MAINT] AP start failed");
        WiFi.mode(WIFI_OFF);
        xEventGroupClearBits(systemEvents, EVENT_NETWORK_BUSY | EVENT_MAINTENANCE_ACTIVE);
        return;
    }
    configureRoutes();
    server.begin();
#if OG_ENABLE_LEGACY_ARDUINO_OTA
    // LAB-ONLY UNSIGNED UPDATER. NEVER enable on unattended devices.
    ArduinoOTA.setHostname(myTowerID.c_str());
    ArduinoOTA.setPort(3232);
    ArduinoOTA.setPassword(OG_OTA_PASSWORD);
    ArduinoOTA.onStart([]() { otaActive = true; Serial.println("[OTA] start"); });
    ArduinoOTA.onEnd([]() { otaActive = false; Serial.println("[OTA] complete"); });
    ArduinoOTA.onError([](ota_error_t err) {
        otaActive = false;
        Serial.printf("[OTA] error=%u\n", static_cast<unsigned>(err));
    });
    ArduinoOTA.begin();
#endif
    Serial.printf("[MAINT] SSID=%s URL=http://%s/ 10 minute window\n",
                  ssid.c_str(), WiFi.softAPIP().toString().c_str());
    const uint32_t deadline = millis() + MAINTENANCE_WINDOW_MS;
    while (static_cast<int32_t>(deadline - millis()) > 0 || otaActive ||
           (localSignedOtaActivityMs && uint32_t(millis()-localSignedOtaActivityMs)<MAINTENANCE_WINDOW_MS)) {
        server.handleClient();
#if OG_ENABLE_LEGACY_ARDUINO_OTA
        ArduinoOTA.handle();
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#if OG_ENABLE_LEGACY_ARDUINO_OTA
    ArduinoOTA.end();
#endif
    localSignedOtaActivityMs=0;
    server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    xEventGroupClearBits(systemEvents, EVENT_NETWORK_BUSY | EVENT_MAINTENANCE_ACTIVE);
    Serial.println("[MAINT] closed; normal offline operation");
}
} // namespace

namespace NetworkManager {
void init() {
    WiFi.mode(WIFI_OFF);
    Serial.setTimeout(300);
    Serial.println("[MAINT] physical USB: type MAINT ON and press Enter to enable local OTA/log download");
}
void taskLoop(void *pvParameters) {
    while (true) {
        if (Serial.available()) {
            String cmd = Serial.readStringUntil('\n');
            cmd.trim();
            if (cmd == "WHOAMI") {
                Serial.printf(
                    "OGIDENT:{\"device_id\":\"%s\",\"firmware\":\"%s\","
                    "\"hardware_revision\":\"%s\",\"product_model\":\"%s\","
                    "\"network_role\":\"%s\",\"backhaul\":\"%s\",\"capabilities\":{"
                    "\"thermal\":%s,\"environmental\":%s,\"smoke\":%s,"
                    "\"optical\":%s,\"ptz\":%s,\"gnss\":%s,\"solar\":%s,"
                    "\"battery\":%s,\"relay\":%s,\"lte\":%s,\"satellite\":%s}}\n",
                    myTowerID.c_str(), FIRMWARE_VERSION, OG_HARDWARE_REVISION,
                    OG_PRODUCT_MODEL, OG_NETWORK_ROLE, OG_BACKHAUL,
                    OG_CAP_THERMAL ? "true" : "false",
                    OG_CAP_ENVIRONMENTAL ? "true" : "false",
                    OG_CAP_SMOKE ? "true" : "false",
                    OG_CAP_OPTICAL ? "true" : "false",
                    OG_CAP_PTZ ? "true" : "false",
                    OG_CAP_GNSS ? "true" : "false",
                    OG_CAP_SOLAR ? "true" : "false",
                    OG_CAP_BATTERY ? "true" : "false",
                    OG_CAP_RELAY ? "true" : "false",
                    OG_CAP_LTE ? "true" : "false",
                    OG_CAP_SATELLITE ? "true" : "false");
            }
            else if (cmd == "SELFTEST") {
                Serial.printf("OGSELFTEST:%s\n", SensorManager::statusJson().c_str());
            }
            else if (cmd == "MAINT ON") runMaintenance();
            else if (cmd.startsWith("OTA ")) {
                if (LoraManager::submitOtaProxy(cmd.substring(4))) Serial.println("[OTA] queued signed transport frame");
                else Serial.println("[OTA] rejected invalid/busy frame");
            }
            else if (cmd.startsWith("CTRL ")) {
                unsigned long id = 0;
                char target[24] = {}, opcode[16] = {}, extra = 0;
                int arg = 0;
                if (sscanf(cmd.c_str(), "CTRL %lu %23s %15s %d %c", &id, target, opcode, &arg, &extra) == 4 &&
                    id > 0 && LoraManager::submitPcCommand(static_cast<uint32_t>(id), String(target),
                                                               String(opcode), arg)) {
                    Serial.printf("[CTRL] queued job=%lu target=%s opcode=%s\n", id, target, opcode);
                } else Serial.println("[CTRL] invalid or full (CTRL <id> <NEST-id> <OP> <arg>)");
            }
            else if (cmd.length() > 0) Serial.println("[MAINT] unknown command");
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
} // namespace NetworkManager
