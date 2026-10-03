#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include "config.h"

// Yangın seviyesi ile cihaz sağlığı bilinçli olarak ayrıdır.
enum FireLevel : uint8_t {
    FIRE_NORMAL = 0,
    FIRE_WATCH = 1,
    FIRE_WARNING = 2,
    FIRE_CRITICAL = 3,
    FIRE_NETWORK_CORROBORATED = 4  // peer evidence, NEVER verified ground-truth fire
};

enum HealthLevel : uint8_t {
    HEALTH_OK = 0,
    HEALTH_DEGRADED = 1,
    HEALTH_FAULT = 2
};

struct SensorDataPacket {
    float max_temp;
    float gas_res;
    float lat;
    float lng;

    int battery_pct;
    float battery_mv;
    float solar_voltage_mv;
    float solar_current_ma;

    float pixels[768];

    // Ham/ara termal metrikler
    float ambient_temp;
    float hotspot_threshold;
    float top5_temp;
    uint16_t hot_pixels_45;
    uint16_t hot_pixels_50;
    uint16_t hot_pixels_60;
    uint16_t largest_hotspot_cluster;
    int8_t hotspot_x;
    int8_t hotspot_y;
    uint8_t persistence_count;

    // Gaz ve füzyon metrikleri
    float local_delta_t;
    float gas_ema;
    float gas_drop_pct;
    float score_temp;
    float score_cluster;
    float score_persistence;
    float score_delta_t;
    float score_gas;
    float local_fire_score;
    uint8_t fire_level;
    uint8_t health_level;

    bool mlx_ok;
    bool gas_ok;
    bool network_confirmed;

    uint32_t uptime_ms;
    uint32_t sequence;
    uint32_t error_counter;
    uint32_t free_heap;
};

struct LoraTxMessage {
    char payload[240];
    char type[12];
    char destination[24];
    uint32_t sequence;
    uint32_t origin_boot;
    bool require_ack;
};

enum PeerEventType : uint8_t {
    PEER_EVENT_ALERT = 1,
    PEER_EVENT_REPORT = 2,
    PEER_EVENT_CONFIRMED = 3
};

struct PeerEvent {
    uint8_t type;
    char source[24];
    uint32_t sequence;
    uint32_t origin_boot;
    uint8_t fire_level;
    uint8_t health_level;
    float fire_score;
    float max_temp;
    uint16_t largest_cluster;
    uint8_t persistence_count;
    float gas_drop_pct;
    float rssi;
    float snr;
};

// OGC2: pilot command messages are HMAC authenticated; no arbitrary shell/OTA over LoRa.
struct ControlRequest {
    char source[24];
    uint32_t source_boot;
    uint32_t sequence;
    uint32_t command_id; // local PC job ID; 0 for a remote radio request
    char opcode[16];
    int16_t argument;
};
struct PcControlCommand {
    uint32_t command_id;
    char target[24];
    char opcode[16];
    int16_t argument;
};
extern QueueHandle_t remoteControlQueue;
extern QueueHandle_t pcControlQueue;
extern QueueHandle_t loraTxQueue;
extern QueueHandle_t peerEventQueue;
extern EventGroupHandle_t systemEvents;
extern volatile uint8_t gCurrentFireLevel;
extern volatile uint32_t gLastFireWatchMs;
extern volatile int gBatteryPct;

extern String myTowerID;
extern String myBootID;
extern uint32_t myBootCounter;

constexpr EventBits_t EVENT_SENSOR_BUSY = BIT0;
constexpr EventBits_t EVENT_LORA_BUSY = BIT1;
constexpr EventBits_t EVENT_NETWORK_BUSY = BIT2;
constexpr EventBits_t EVENT_MAINTENANCE_ACTIVE = BIT3;

#endif
