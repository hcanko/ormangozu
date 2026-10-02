#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>

#include "globals.h"
#include "algorithm_manager.h"
#include "storage_manager.h"
#include "lora_manager.h"
#include "sensor_manager.h"
#include "network_manager.h"

QueueHandle_t loraTxQueue = nullptr;
QueueHandle_t peerEventQueue = nullptr;
EventGroupHandle_t systemEvents = nullptr;
String myTowerID = "";
String myBootID = "";

void setup() {
    Serial.begin(115200);
    delay(1200);
    Serial.println("\n=== ORMAN GÖZÜ THERMAL MESH V0.3 ===");

    const uint64_t mac = ESP.getEfuseMac();
    char idBuffer[24];
    snprintf(idBuffer, sizeof(idBuffer), "TOWER-%04X", static_cast<uint16_t>(mac & 0xFFFF));
    myTowerID = String(idBuffer);
    myBootID = String(static_cast<uint32_t>(esp_random()), HEX);
    Serial.printf("Kimlik: %s | Boot: %s | FW: %s\n",
                  myTowerID.c_str(), myBootID.c_str(), FIRMWARE_VERSION);

    systemEvents = xEventGroupCreate();
    loraTxQueue = xQueueCreate(10, sizeof(LoraTxMessage));
    peerEventQueue = xQueueCreate(10, sizeof(PeerEvent));
    if (systemEvents == nullptr || loraTxQueue == nullptr || peerEventQueue == nullptr) {
        Serial.println("❌ RTOS kaynakları oluşturulamadı.");
        while (true) delay(1000);
    }

    StorageManager::init();
    AlgorithmManager::init();
    NetworkManager::init();
    LoraManager::init();
    SensorManager::init();

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
