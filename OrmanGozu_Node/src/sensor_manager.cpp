#include "sensor_manager.h"
#include "globals.h"

#include <Wire.h>
#include <Adafruit_MLX90640.h>
#include <Adafruit_BME680.h>
#include <TinyGPSPlus.h>

Adafruit_MLX90640 mlx;
Adafruit_BME680 bme;

TinyGPSPlus gps;
HardwareSerial GPS_Serial(1);

// GNSS sonraki aşamada debug edilecek.
// Şimdilik koordinatlar sabit fallback değerleriyle gönderiliyor.
#define GPS_RX_PIN 38
#define GPS_TX_PIN 39
#define GPS_CTRL_PIN 34

#define BATTERY_PIN 1
#define ADC_CTRL_PIN 37

// Faz-1 saha testi için geçici/sabit koordinat.
// GNSS düzgün çalışınca currentLat/currentLng otomatik güncellenecek.
float currentLat = 38.4237;
float currentLng = 27.1428;

static bool mlxInitialized = false;
static bool bmeInitialized = false;

static uint32_t readCounter = 0;
static uint32_t errorCounter = 0;

// Servo entegrasyonu yapılınca burası gerçek açıyla güncellenecek.
static int currentServoAngle = 0;

float getBatteryMilliVolts() {
    pinMode(ADC_CTRL_PIN, OUTPUT);
    digitalWrite(ADC_CTRL_PIN, HIGH);
    delay(50);

    int adcMilliVolts = analogReadMilliVolts(BATTERY_PIN);

    digitalWrite(ADC_CTRL_PIN, LOW);

    // Heltec/ölçüm devresindeki bölücü katsayısı.
    // Kendi kartındaki gerçek bölücüye göre kalibre edilebilir.
    float batteryMilliVolts = adcMilliVolts * 4.9f;

    return batteryMilliVolts;
}

int getBatteryPercentageFromMilliVolts(float batteryMilliVolts) {
    // 1S Li-ion için yaklaşık değer.
    // LiFePO4 veya farklı batarya kimyasında bu eğri değiştirilmeli.
    int percentage = map((int)batteryMilliVolts, 3300, 4200, 0, 100);
    return constrain(percentage, 0, 100);
}

uint8_t calculateLocalAlertLevel(float maxTemp, float gasRes) {
    // Yerel LoRa acil alarmı için basit ve güvenli eşikler.
    // Asıl gelişmiş karar backend'de fire_score ile veriliyor.
    if (maxTemp >= 90.0f) {
        return ALERT_CRITICAL;
    }

    if (maxTemp >= 70.0f) {
        return ALERT_WARNING;
    }

    return ALERT_NORMAL;
}

void fillPixelsWithZero(float *pixels) {
    for (int i = 0; i < 768; i++) {
        pixels[i] = 0.0f;
    }
}

namespace SensorManager {

    void init() {
        Serial.println("[CORE 0] Sensörler başlatılıyor...");

        // GNSS şimdilik kritik değil. Güç pini sonraki aşamada ayrıca debug edilecek.
        pinMode(GPS_CTRL_PIN, OUTPUT);
        digitalWrite(GPS_CTRL_PIN, LOW);
        delay(500);

        GPS_Serial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

        Wire.begin(41, 42);
        Wire.setClock(400000);

        mlxInitialized = mlx.begin(MLX90640_I2CADDR_DEFAULT, &Wire);
        if (mlxInitialized) {
            mlx.setMode(MLX90640_CHESS);
            mlx.setRefreshRate(MLX90640_4_HZ);
            Serial.println("Termal Kamera: ✅ BAĞLANDI");
        } else {
            Serial.println("Termal Kamera: ❌ BULUNAMADI");
        }

        bmeInitialized = bme.begin(0x76, &Wire);
        if (bmeInitialized) {
            bme.setGasHeater(320, 150);
            Serial.println("Gaz Sensörü/BME680: ✅ BAĞLANDI");
        } else {
            Serial.println("Gaz Sensörü/BME680: ❌ BULUNAMADI");
        }
    }

    void taskLoop(void *pvParameters) {
        unsigned long lastReadTime = 0;

        while (true) {
            // GNSS şimdilik opsiyonel. Veri gelirse koordinatı günceller.
            while (GPS_Serial.available() > 0) {
                char c = GPS_Serial.read();
                if (gps.encode(c)) {
                    if (gps.location.isValid()) {
                        currentLat = gps.location.lat();
                        currentLng = gps.location.lng();
                    }
                }
            }

            if (millis() - lastReadTime >= (unsigned long)sleep_interval) {
                lastReadTime = millis();

                SensorDataPacket packet = {};
                readCounter++;

                packet.uptime_ms = millis();
                packet.read_counter = readCounter;
                packet.error_counter = errorCounter;

                packet.lat = currentLat;
                packet.lng = currentLng;
                packet.gps_fix = gps.location.isValid();

                packet.servo_angle = currentServoAngle;

                // ------------------------------
                // Termal kamera okuma
                // ------------------------------
                float maxT = 0.0f;
                bool mlxOkThisRead = false;

                if (mlxInitialized) {
                    int mlxState = mlx.getFrame(packet.pixels);

                    if (mlxState == 0) {
                        mlxOkThisRead = true;

                        for (int i = 0; i < 768; i++) {
                            if (packet.pixels[i] > maxT) {
                                maxT = packet.pixels[i];
                            }
                        }
                    } else {
                        errorCounter++;
                        fillPixelsWithZero(packet.pixels);
                        Serial.print("❌ MLX90640 frame okuma hatası: ");
                        Serial.println(mlxState);
                    }
                } else {
                    errorCounter++;
                    fillPixelsWithZero(packet.pixels);
                }

                packet.max_temp = maxT;
                packet.mlx_ok = mlxOkThisRead;

                // ------------------------------
                // Gaz / BME680 okuma
                // ------------------------------
                bool gasOkThisRead = false;
                float gasResistance = 0.0f;

                if (bmeInitialized && bme.performReading()) {
                    gasResistance = bme.gas_resistance;
                    gasOkThisRead = gasResistance > 0.0f;
                } else {
                    errorCounter++;
                    gasResistance = 0.0f;
                }

                packet.gas_res = gasResistance;
                packet.gas_ok = gasOkThisRead;

                // ------------------------------
                // Batarya telemetrisi
                // ------------------------------
                packet.battery_mv = getBatteryMilliVolts();
                packet.battery_pct = getBatteryPercentageFromMilliVolts(packet.battery_mv);

                // ------------------------------
                // Yerel alarm seviyesi
                // ------------------------------
                packet.alert_level = calculateLocalAlertLevel(packet.max_temp, packet.gas_res);

                // ------------------------------
                // Wi-Fi/backend kuyruğuna tam veri gönder
                // ------------------------------
                if (networkDataQueue != NULL) {
                    if (xQueueSend(networkDataQueue, &packet, pdMS_TO_TICKS(100)) == pdPASS) {
                        Serial.println("[CORE 0] Veri network kuyruğuna atıldı.");
                    } else {
                        Serial.println("⚠️ Network kuyruğu dolu, paket düşürüldü.");
                    }
                }

                // ------------------------------
                // LoRa kuyruğuna sadece uyarı/kritik alarm gönder
                // ------------------------------
                if (packet.alert_level >= ALERT_WARNING && loraAlertQueue != NULL) {
                    if (xQueueSend(loraAlertQueue, &packet, pdMS_TO_TICKS(100)) == pdPASS) {
                        Serial.println("📡 Alarm LoRa kuyruğuna atıldı.");
                    } else {
                        Serial.println("⚠️ LoRa kuyruğu dolu, alarm paketi düşürüldü.");
                    }
                }

                Serial.printf(
                    "[TELEMETRI] T: %.1f C | Gas: %.0f ohm | Bat: %.0f mV (%d%%) | Alert: %d | MLX:%d | GAS:%d | Err:%lu\n",
                    packet.max_temp,
                    packet.gas_res,
                    packet.battery_mv,
                    packet.battery_pct,
                    packet.alert_level,
                    packet.mlx_ok,
                    packet.gas_ok,
                    packet.error_counter
                );
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
