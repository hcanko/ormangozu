#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// LoRa ve backend için ortak alarm seviyesi
enum AlertLevel : uint8_t {
    ALERT_NORMAL = 0,
    ALERT_WARNING = 1,
    ALERT_CRITICAL = 2
};

// Core 0'ın okuyup Core 1'e göndereceği ana veri paketi
struct SensorDataPacket {
    // Ana sensör verileri
    float max_temp;
    float gas_res;
    float lat;
    float lng;

    // Enerji telemetrisi
    int battery_pct;
    float battery_mv;

    // Termal kamera frame'i
    float pixels[768];

    // Alarm / karar telemetrisi
    uint8_t alert_level;

    // Sensör sağlık bilgileri
    bool mlx_ok;
    bool gas_ok;
    bool gps_fix;

    // Mekanik / sistem telemetrisi
    int servo_angle;
    uint32_t uptime_ms;
    uint32_t read_counter;
    uint32_t error_counter;
};

// Wi-Fi / backend hattı için tam veri kuyruğu
extern QueueHandle_t networkDataQueue;

// LoRa için sadece UYARI / KRİTİK alarm kuyruğu
extern QueueHandle_t loraAlertQueue;

// Global ayarlar
extern int sleep_interval;
extern String myTowerID;
extern String localIP;

#endif
