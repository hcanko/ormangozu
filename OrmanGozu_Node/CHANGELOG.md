# v0.2 değişiklikleri

- GNSS, pusula ve servo bağımlılıkları saha firmware'inden kaldırıldı.
- Wi-Fi/backend telemetri bağımlılığı kaldırıldı.
- SX1262 kesme tabanlı sürekli RX eklendi.
- ALERT → ACK → anlık ölçüm → REPORT protokolü eklendi.
- Komşu alarmında 2 dakikalık hızlı örnekleme modu eklendi.
- Core0 sensör/algoritma, Core1 iletişim ayrımı korundu.
- DIO1 veya örnekleme zamanlayıcısıyla light sleep uyanışı eklendi.
- LittleFS telemetri, termal frame ve LoRa olay kara kutusu eklendi.
- Bakım hotspot'u algılandığında log indirme ve ArduinoOTA eklendi.
- RadioLib `begin()` parametre sırası düzeltilerek gerçek 18 dBm güç ve 12 sembol preamble ayarlandı.
