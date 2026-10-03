#ifndef ORMAN_GOZU_CONFIG_H
#define ORMAN_GOZU_CONFIG_H

// =============================
// PILOT SAHA AYARLARI
// =============================
#define PROTOCOL_VERSION 4
#define FIRMWARE_VERSION "0.7.0-signed-lora-ota"
#define OG_FIRMWARE_BUILD 700

// Sabit direk konumu. Her cihaza yüklemeden önce gerekirse değiştir.
#define TOWER_LATITUDE 0.0f // MUST SET per device to measured site; 0,0 = unknown
#define TOWER_LONGITUDE 0.0f // GNSS integration intentionally not claimed as complete

// Sensör örnekleme
#define NORMAL_SAMPLE_INTERVAL_MS 30000UL   // 30 seconds (not deep sleep)
#define FAST_SAMPLE_INTERVAL_MS 2000UL       // eş/yerel WATCH sonrası 2 saniye
#define PEER_SCAN_WINDOW_MS 30000UL
#define FAST_MODE_DURATION_MS 120000UL        // 2 dakika hızlı doğrulama

// Termal öncelikli füzyon eşikleri. Bir aylık veriyle yeniden kalibre edilecek.
#define THERMAL_HOTSPOT_MIN_C 45.0f
#define THERMAL_HOTSPOT_DELTA_ABOVE_AMBIENT_C 15.0f
#define THERMAL_WATCH_MAX_C 50.0f
#define THERMAL_STRONG_MAX_C 70.0f
#define THERMAL_OVERRIDE_CRITICAL_C 90.0f
#define THERMAL_MIN_CLUSTER_PIXELS 3
#define THERMAL_STRONG_CLUSTER_PIXELS 6
#define THERMAL_CLUSTER_FULL_SCORE_PIXELS 12
#define THERMAL_PERSISTENCE_DISTANCE_PX 5.0f
#define THERMAL_REQUIRED_PERSISTENCE 2
#define GAS_SUPPORT_DROP_PCT 15.0f

#define FUSION_WEIGHT_TEMP 0.40f
#define FUSION_WEIGHT_CLUSTER 0.25f
#define FUSION_WEIGHT_PERSISTENCE 0.15f
#define FUSION_WEIGHT_DELTA_T 0.10f
#define FUSION_WEIGHT_GAS 0.10f
#define FUSION_WATCH_SCORE 35.0f
#define FUSION_WARNING_SCORE 60.0f
#define FUSION_CRITICAL_SCORE 85.0f

// İki direk arasındaki ağ teyidi
#define NETWORK_CONFIRM_WINDOW_MS 75000UL // 30s peer scan + airtime and retries
#define LOCAL_EVENT_RING_SIZE 6

// LoRa - iki düğüm de birebir aynı ayarları kullanmalı.
#define LORA_FREQUENCY_MHZ 868.0f
#define LORA_BANDWIDTH_KHZ 125.0f
#define LORA_SPREADING_FACTOR 9
#define LORA_CODING_RATE 7
#define LORA_SYNC_WORD 0x12
#define LORA_TX_POWER_DBM 18
#define LORA_PREAMBLE_SYMBOLS 12
#define LORA_TCXO_VOLTAGE 1.7f
#define LORA_TX_RETRIES 3
#define LORA_ACK_TIMEOUT_MS 1400UL
// OTA is lower priority than alerts and manual intervention; RF airtime must
// be rate-limited on the PC for the exact authorised frequency sub-band.
#define OG_OTA_MAX_IMAGE_BYTES (1536UL*1024UL)
#define OG_OTA_FREE_RESERVE_BYTES (128UL*1024UL)
#define OG_OTA_MIN_BATTERY_PCT 40
#define OG_OTA_QUIET_MS 120000UL
#define OG_MAX_RELAY_HOPS 2
// Conservative extra pilot throttle; verify legal RF duty cycle for real payload airtime.
#define OG_OTA_NODE_TX_GAP_MS 180000UL // SF9 pilot throttle; verify actual per-packet airtime/band plan
#define LORA_ALERT_COOLDOWN_MS 60000UL
#define LORA_RETRY_BACKOFF_MIN_MS 120
#define LORA_RETRY_BACKOFF_MAX_MS 420

// Remote intervention controls; fail closed unless physical servo installation is verified.
#define OG_ENABLE_PAN_TILT 0
#define OG_PAN_SERVO_PIN (-1)
#define OG_TILT_SERVO_PIN (-1)
#define OG_SERVO_MIN_DEGREES 20
#define OG_SERVO_MAX_DEGREES 160
#define OG_MANUAL_TIMEOUT_MS 120000UL
#define OG_CONTROL_RESULT_TIMEOUT_MS 45000UL

// Light sleep disabled until SX1262 DIO1/ESP wake verified on actual board.
#define ENABLE_LIGHT_SLEEP 0
#define MIN_LIGHT_SLEEP_MS 80UL

// 8MB partition: 2.94MiB LittleFS; normal 2-file rings capped near 1152KiB
// to leave >=1.5MiB staged signed OTA + 128KiB safety reserve.
// Download field logs periodically; richer history needs confirmed 16MB board.
// Dahili flash kara kutu
#define ENABLE_LITTLEFS_LOGGING 1
#define TELEMETRY_ROTATE_BYTES (192UL * 1024UL)
#define LORA_LOG_ROTATE_BYTES (32UL * 1024UL)
#define FUSION_LOG_ROTATE_BYTES (32UL * 1024UL)
#define FRAME_ROTATE_BYTES (320UL * 1024UL)
#define FRAME_RING_FILE_COUNT 2

// The RF pin map was taken from original GitHub main. VERIFY V3/V4 PCB before flashing.
#define LORA_NSS_PIN 8
#define LORA_DIO1_PIN 14
#define LORA_NRST_PIN 12
#define LORA_BUSY_PIN 13

// Long-term samples are recorded at a lower rate; alarms are recorded immediately.
#define TELEMETRY_BASELINE_LOG_MS 1800000UL  // 30 minutes; event sampling stays immediate
#define FRAME_BASELINE_LOG_MS 7200000UL     // 2 hours, and every anomaly/fast frame

// Alert mesh key and remote-control/OTA transport key MUST be independent.
// Firmware authenticity is separately enforced with an offline ECDSA public key.
// Pilot keying is shared between its registered nodes; a deployed fleet must
// move to per-node provisioning and hardware key protection.
#include "secrets.h"
#if !defined(OG_PROVISIONED) || OG_PROVISIONED != 1
#error "Copy secrets.example.h to secrets.h, replace passwords/key, set OG_PROVISIONED=1"
#endif

// Physical USB-serial command MAINT ON opens an AP for 10 minutes.
#define MAINTENANCE_WINDOW_MS 600000UL
#define OG_ENABLE_LEGACY_ARDUINO_OTA 0 // unsigned ArduinoOTA is NOT permitted in secure mode
#define MAINTENANCE_AP_SSID_PREFIX "OG-MAINT-"
#define MAINTENANCE_HTTP_USER "nest"
// Pil ölçümü
#define BATTERY_PIN 1
#define ADC_CTRL_PIN 37
#define BATTERY_DIVIDER_FACTOR 4.9f

// I2C sensör pinleri
#define SENSOR_I2C_SDA 41
#define SENSOR_I2C_SCL 42

#endif
