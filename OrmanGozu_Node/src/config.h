#ifndef ORMAN_GOZU_CONFIG_H
#define ORMAN_GOZU_CONFIG_H

// =============================
// PILOT SAHA AYARLARI
// =============================
#define PROTOCOL_VERSION 3
#define FIRMWARE_VERSION "0.3.0-thermal-mesh"

// Sabit direk konumu. Her cihaza yüklemeden önce gerekirse değiştir.
#define TOWER_LATITUDE 38.423700f
#define TOWER_LONGITUDE 27.142800f

// Sensör örnekleme
#define NORMAL_SAMPLE_INTERVAL_MS 300000UL   // 5 dakika
#define FAST_SAMPLE_INTERVAL_MS 2000UL       // eş/yerel WATCH sonrası 2 saniye
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
#define NETWORK_CONFIRM_WINDOW_MS 30000UL
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
#define LORA_ALERT_COOLDOWN_MS 30000UL
#define LORA_RETRY_BACKOFF_MIN_MS 120
#define LORA_RETRY_BACKOFF_MAX_MS 420

// Light sleep: SX1262 sürekli RX'te kalır, DIO1 CPU'yu uyandırır.
#define ENABLE_LIGHT_SLEEP 1
#define MIN_LIGHT_SLEEP_MS 80UL

// Dahili flash kara kutu
#define ENABLE_LITTLEFS_LOGGING 1
#define TELEMETRY_ROTATE_BYTES (768UL * 1024UL)
#define LORA_LOG_ROTATE_BYTES (256UL * 1024UL)
#define FUSION_LOG_ROTATE_BYTES (256UL * 1024UL)
#define FRAME_ROTATE_BYTES (2UL * 1024UL * 1024UL)
#define FRAME_RING_FILE_COUNT 3

// Bakım ağı. Telefon/laptop hotspot'u bu adla açılır.
#define ENABLE_MAINTENANCE_WIFI 1
#define MAINTENANCE_WIFI_SSID "ORMAN_BAKIM"
#define MAINTENANCE_WIFI_PASSWORD "orman-test-2026"
#define MAINTENANCE_SCAN_EVERY_SAMPLES 12UL
#define MAINTENANCE_CONNECT_TIMEOUT_MS 12000UL
#define MAINTENANCE_WINDOW_MS 600000UL

// Pil ölçümü
#define BATTERY_PIN 1
#define ADC_CTRL_PIN 37
#define BATTERY_DIVIDER_FACTOR 4.9f

// I2C sensör pinleri
#define SENSOR_I2C_SDA 41
#define SENSOR_I2C_SCL 42

#endif
