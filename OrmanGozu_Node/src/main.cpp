#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <Preferences.h>

#include "globals.h"
#include "algorithm_manager.h"
#include "storage_manager.h"
#include "lora_manager.h"
#include "sensor_manager.h"
#include "network_manager.h"
#include "control_manager.h"
#include "ota_manager.h"

QueueHandle_t loraTxQueue = nullptr;
QueueHandle_t remoteControlQueue = nullptr;
QueueHandle_t pcControlQueue = nullptr;
QueueHandle_t peerEventQueue = nullptr;
EventGroupHandle_t systemEvents = nullptr;
String myTowerID = "";
String myBootID = "";
uint32_t myBootCounter = 0;
volatile uint8_t gCurrentFireLevel = FIRE_NORMAL;
volatile uint32_t gLastFireWatchMs = 0;
volatile int gBatteryPct = -1;

void setup() {
    Serial.begin(115200);
    delay(1200);
    Serial.println("\n=== ORMAN GÖZÜ WHISPER PILOT V0.7.0 ===");

    const uint64_t mac = ESP.getEfuseMac();
    char idBuffer[24];
    snprintf(idBuffer, sizeof(idBuffer), "NEST-%012llX", static_cast<unsigned long long>(mac));
    myTowerID = String(idBuffer);
    Preferences prefs;
    prefs.begin("ogboot", false);
    myBootCounter = prefs.getUInt("counter", 0) + 1;
    // Never silently wrap a persistent boot counter; provision a replacement unit.
    if (myBootCounter == 0) { Serial.println("BOOT COUNTER EXHAUSTED"); while(true) delay(1000); }
    prefs.putUInt("counter", myBootCounter);
    prefs.end();
    myBootID = String(myBootCounter);
    Serial.printf("Kimlik: %s | Boot: %s | FW: %s\n",
                  myTowerID.c_str(), myBootID.c_str(), FIRMWARE_VERSION);

    systemEvents = xEventGroupCreate();
    loraTxQueue = xQueueCreate(10, sizeof(LoraTxMessage));
    peerEventQueue = xQueueCreate(10, sizeof(PeerEvent));
    remoteControlQueue = xQueueCreate(6, sizeof(ControlRequest));
    pcControlQueue = xQueueCreate(4, sizeof(PcControlCommand));
    if (systemEvents == nullptr || loraTxQueue == nullptr || peerEventQueue == nullptr ||
        remoteControlQueue == nullptr || pcControlQueue == nullptr) {
        Serial.println("❌ RTOS kaynakları oluşturulamadı.");
        while (true) delay(1000);
    }

    if (!StorageManager::init()) {
        Serial.println("[FATAL] LittleFS unavailable; cannot perform a data-preserving pilot.");
        while (true) delay(1000);
    }
    AlgorithmManager::init();
    NetworkManager::init();
    if (!LoraManager::init()) {
        Serial.println("[FATAL] LoRa unavailable; verify actual PCB revision/pins.");
        while (true) delay(1000);
    }
    SensorManager::init();
    ControlManager::init();

    xTaskCreatePinnedToCore(
        LoraManager::taskLoop, "LoraTask", 14336, nullptr, 4, nullptr, 1
    );
    xTaskCreatePinnedToCore(
        SensorManager::taskLoop, "SensorTask", 22528, nullptr, 3, nullptr, 0
    );
    xTaskCreatePinnedToCore(
        NetworkManager::taskLoop, "NetworkTask", 12288, nullptr, 1, nullptr, 1
    );

    Serial.println("✅ Core0 sensör+termal füzyon, Core1 sürekli LoRa RX/TX + bakım aktif.");
}

void loop() {
    vTaskDelete(nullptr);
}
