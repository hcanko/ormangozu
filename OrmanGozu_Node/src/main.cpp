#include <Arduino.h>
#include "globals.h"
#include "sensor_manager.h"
#include "network_manager.h"
#include "lora_manager.h"

// Global değişkenlerin tanımlanması
QueueHandle_t networkDataQueue = NULL;
QueueHandle_t loraAlertQueue = NULL;

int sleep_interval = 2000;
String myTowerID = "";
String localIP = "";

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n=== ORMAN GÖZÜ - DUAL CORE + LORA ALARM MODU BAŞLIYOR ===");

    // Core 0 -> Core 1 Wi-Fi/backend veri kuyruğu
    networkDataQueue = xQueueCreate(3, sizeof(SensorDataPacket));

    // Core 0 -> Core 1 LoRa alarm kuyruğu
    loraAlertQueue = xQueueCreate(5, sizeof(SensorDataPacket));

    if (networkDataQueue == NULL || loraAlertQueue == NULL) {
        Serial.println("❌ Kuyruk oluşturma hatası! Sistem durduruluyor.");
        while (true) {
            delay(1000);
        }
    }

    // LoRa'yı Wi-Fi'den bağımsız başlatıyoruz.
    // Böylece Wi-Fi olmasa bile kritik alarm hattı hazırlanmış olur.
    LoraManager::init();

    // Ağ ve web sunucu / OTA servisleri
    NetworkManager::init();

    // Sensörleri başlat
    SensorManager::init();

    // ==========================================
    // CORE 0 - Donanım okuma görevi
    // ==========================================
    xTaskCreatePinnedToCore(
        SensorManager::taskLoop,
        "SensorTask",
        16384,
        NULL,
        2,
        NULL,
        0
    );

    // ==========================================
    // CORE 1 - Wi-Fi / HTTP / OTA görevi
    // ==========================================
    xTaskCreatePinnedToCore(
        NetworkManager::taskLoop,
        "NetworkTask",
        16384,
        NULL,
        1,
        NULL,
        1
    );

    // ==========================================
    // CORE 1 - LoRa acil alarm görevi
    // ==========================================
    xTaskCreatePinnedToCore(
        LoraManager::taskLoop,
        "LoraTask",
        8192,
        NULL,
        1,
        NULL,
        1
    );

    Serial.println("✅ Tüm görevler başlatıldı.");
}

void loop() {
    // FreeRTOS görevleri sistemi yönettiği için loop kullanılmıyor.
    vTaskDelete(NULL);
}
