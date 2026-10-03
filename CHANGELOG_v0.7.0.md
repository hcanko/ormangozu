# v0.7.0 — v0.6.1'den değişiklikler

## Yeni

- Bilgisayarın USB köprüsü üzerinden hedef Nest'e LoRa `OGU1` firmware taşıması: `B` (bildirim), `S` (P-256 imza), `D` (96-byte blok), `Q` (offset/build sorgusu), `E` (SHA + OTA slot uygulama), `X` (yeniden başlat), `R` (durum cevabı).
- Yarım aktarıma LittleFS'te devam: `.meta` ve `.part`; paket yalnız sıradaki offset'e eklenir, eski blok ancak birebir aynıysa tekrar ACK edilir. Yeniden başlama ve E/X arası kesintiye göre tekrar denenebilir.
- PC aracı `tools/lora_ota.py`: offline parolalı özel anahtar üretimi, hedef/build/transfer/imza manifesti, doğrulama, USB ile LoRa üzerinden güncelleme ve bakım hotspot'undan **aynı imza doğrulamasına tabi** yerel güncelleme.
- `OG4`, `OGC2`, `OGU1`: HMAC kapsamına giren atlama sayısı (0..2); 2 defa sınırlı ileri iletim, kısa tekrar-yayın baskılama; OTA düşük öncelikli ayrı radyo kuyruğu.
- Kontrol/OTA anahtarını uyarı anahtarından ayırma; kontrol mesajını belirli yetkili USB köprü kimliği ve hedef ID'sine bağlama.
- Eski şifresiz/imzasız ArduinoOTA **varsayılan kapalı**. Bakım HTTP'si yalnız kısa süreli şifreli Wi-Fi hotspot'unda, HTTP Basic + imza denetimiyle kullanılabilir.
- Firmware sahnelemeye SHA-256 ve P-256 doğrulaması, firmware `build` ileri sürüm denetimi, batarya >= %40 ve yangın şüphesi yokken pasif OTA slotuna yazma; başarılı sensör+LoRa başlangıcında mümkün olduğunda rollback onayı.
- HTTP/LoRa aynı anda OTA alanına yazmaya çalışmasın diye eşzamanlı giriş denetimi.
- Backend'de `OG_OPERATOR_TOKEN` ile `OG_CLIENT_TOKEN` rol ayrımı; anonim eski güncelleme/test uçları korunmasız değil; `OG_ENABLE_LEGACY_SENSOR_HTTP=0` ve API belgeleri varsayılan kapalı, localhost varsayılan.
- 8 MB LittleFS'te signed OTA için kayıt halkalarının boyutunu yaklaşık 1152 KiB toplamına ve normal baseline CSV 30 dakika/termal 2 saate ayarlama.
- Python protokol referanslarını fiili `OG4/OGC2` formatına güncelleme; imza manipülasyonu, yanlış hedef/anahtar, rol ve röle-hop testleri.
- Kaynak `VERSION=0.7.0`; Python/TypeScript/PlatformIO doğrulama CI tarifi.

## Bilerek dışarıda / sınırlı

- Full LoRa OTA bir pilot doğrulama akışıdır, kısa süre garantisi yok: varsayılan 96 byte stop-and-wait + 180 sn PC/node pacing, 1 MiB dosyada günler/haftalar.
- Yalnızca 2 sınırlı röle; otomatik ağ topolojisi keşfi, çok sayıda eşzamanlı OTA, hız optimize edilmiş pencere/delta-transfer yok.
- Firmware imzası OTA kodunun içinde doğrulanır; chip-level Secure Boot / flash encryption ayrı **fiziksel** üretim/provizyon aşamasıdır. Eski UART/JTAG yolları devre dışı bırakılmış olarak iddia edilmez.
- RTOS/radyo/donanım OTA sırasında fiziksel olarak sınanmadı; kod değişiklikleri hazır ama güvenli dağıtım onayı için test raporundaki geçiş kapıları gereklidir.
