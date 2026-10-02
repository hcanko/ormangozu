# Orman Gözü Offline Mesh v0.2

Bu sürüm iki direkli internetsiz saha testi için hazırlanmıştır.

## Mimari

- Core 0: MLX90640 + BME680 okuma, yerel algoritma, LittleFS kayıt.
- Core 1: SX1262 sürekli LoRa RX/TX ve isteğe bağlı bakım Wi-Fi.
- CPU beklerken light sleep'e girer; SX1262 sürekli alıcıda kalır.
- DIO1 paket geldiğinde ESP32-S3'ü uyandırır.
- Bir direk alarm verdiğinde `ALERT` paketi yollar.
- Diğer direk ACK verir, anında ölçüm yapar, 2 dakika hızlı moda geçer ve `REPORT` yollar.
- Wi-Fi/internet normal operasyonda kullanılmaz.

## Önemli enerji gerçeği

SX1262 sürekli RX yaklaşık 4.2 mA tüketir. Bu nedenle sistem mikroamper deep-sleep sistemi değildir. İletişim önceliği için bilinçli olarak light sleep + sürekli radyo seçilmiştir. Güneş paneli ve batarya testi sırasında gece tüketimini mutlaka ölçün.

## Yüklemeden önce

1. `config.h` içinde konum ve aralıkları ayarlayın.
2. İki cihazda LoRa frekans/SF/BW/CR/sync word tamamen aynı olmalı.
3. Kartın flash'ı 16 MB olmalı.
4. LittleFS için `partitions.csv` kullanılmalı. PlatformIO'da `board_build.partitions = partitions.csv`; Arduino IDE'de özel partition desteği ayrıca ayarlanmalıdır.
5. RadioLib, Adafruit MLX90640, Adafruit BME680, AsyncTCP ve ESPAsyncWebServer kütüphaneleri gerekir.

## Bakım ve log indirme

Telefon/laptop hotspot'unu şu bilgilerle açın:

- SSID: `ORMAN_BAKIM`
- Parola: `orman-test-2026`

Cihaz açılışta ve yaklaşık saatte bir bağlanmayı dener. Bağlanınca seri ekranda IP görünür:

- `/status`
- `/logs/telemetry`
- `/logs/lora`
- `/logs/frames0`, `/logs/frames1`, `/logs/frames2`
- `POST /logs/erase`

ArduinoOTA da 10 dakikalık bakım penceresinde aktiftir.

## İlk test sırası

1. İki kartı masada 5-10 metre ayırın.
2. Bir kartta alarm üretin; diğerinin `PEER ALERT` yazdığını doğrulayın.
3. İkinci kartın hemen ölçüm yaptığını ve `PEER REPORT` döndürdüğünü doğrulayın.
4. `lora_0.csv` içinde ALERT/ACK/REPORT kayıtlarını kontrol edin.
5. 24 saat pil testi, ardından 72 saat güneş paneli testi yapın.
6. Bu üç test geçmeden bir aylık sahaya bırakmayın.

## Bilinen sınırlar

- Tek SX1262 aynı anda RX ve TX yapamaz. LoRaTask tüm işlemleri sıraya koyar; ayrı çekirdeklere RX/TX bölünmemiştir.
- Light sleep + DIO1 uyandırma kart üzerinde doğrulanmalıdır. Sorun olursa `ENABLE_LIGHT_SLEEP 0` yaparak iletişimi önce sürekli açık CPU ile test edin.
- LittleFS pilot kara kutudur; nihai ürün için microSD veya endüstriyel flash düşünülmelidir.
- Güneş paneli voltaj/akımı henüz ayrı ADC/INA sensörü olmadığı için 0 kaydedilir.
