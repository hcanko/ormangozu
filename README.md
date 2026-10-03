# Orman Gözü v0.7.0 — LoRa Whisper, PC Remote Management ve İmzalı LoRa OTA

**Durum:** Kaynak-kod / fiziksel test adayı. `22/22` Python testinin geçmesi, ESP firmware'inin Heltec'te derlendiği veya yangın tespitinin sahada doğrulandığı anlamına gelmez. **İki direğe de aynı v0.7 OG4/OGC2/OGU1 radyo sürümü yüklenmelidir. Eski OG4/OGC1/v0.6.1 karışımı uyumlu değildir.**

Önce şu dosyaları okuyun:

- `KURULUM_TR_v0.7.md` — Windows/PC, anahtar oluşturma, iki Nest, güvenli OTA
- `LORA_OTA_SECURITY_v0.7.md` — kablosuz güncelleme protokolü, sınırları ve riskleri
- `CHANGELOG_v0.7.0.md` — v0.6.1'den farklar
- `TEST_REPORT_v0.7.0.md` — doğrulanan/doğrulanmayan kontroller ve geçiş kriterleri
- `KALAN_ISLER_v0.7.0.md` — masa/saha ve üretim-güvenliği kontrol listesi

| Parça | Klasör | Görev |
|---|---|---|
| ESP32-S3 / SX1262 | `OrmanGozu_Node/` | MLX90640, BME680, yerel karar, LittleFS, iki atlamaya kadar sınırlı radyo rölesi, kontrol, imzalı OTA |
| Bilgisayar backend'i | `backend/` | Eski dashboard, kalibrasyon, CSV import + Whisper olayları + token ile korunan hedefli kontrol API |
| PC arayüzü | `frontend/` | Harita, Whisper Pilot, Nest Kontrol; servo takılmadan PAN/TILT kapalıdır |
| USB ve OTA araçları | `tools/` | JSONL collector, yeniden iletim, P-256 imzalama, kesintiden devam eden LoRa OTA, RF hesap |
| Otomatik test | `tests/`, `backend/tests/`, `.github/workflows/verify-v070.yml` | Python/TypeScript/ESP build CI; varsayılan CI `.bin` test amaçlı anahtarla oluşur, dağıtıma UYGUN DEĞİLDİR |

## Pilot mimarisi

`Bilgisayar (API + UI + LoRa OTA CLI) ⇄ USB ile bağlı Nest A (radyo köprüsü) ⇄ LoRa ⇄ Nest B ⇄ [gerekirse 1–2 aracı Nest] ⇄ Hedef Nest`.

**Bilgisayar ormanın dışında bulunuyorsa, USB köprüsü ile bilgisayar arasında fiilen çalışan bir IP/uplink yolu gerekir.** Üçüncü bir radyo kartını ilk iki direklik masa pilotunda zorunlu tutmuyoruz. Röle yalnız radyo sinyali gerçekten komşusuna ulaşabiliyorsa işe yarar; tüm ormana koşulsuz erişim garantisi değildir. Bu sürümde dinamik rota keşfi, gerçek gateway daemon'u ve 7/24 uzaktan otomatik firmware dağıtımı yoktur.

## Siber güvenlik kapsamı

OG4 alarm anahtarı ≠ OGC2/OGU1 kontrol aktarım anahtarı; HMAC-SHA256/96-bit paket kimlik doğrulaması; doğrulanan hedef cihaz ID'si ve tekrar koruması; 2-hop TTL; operatör ve USB veri-toplayıcı API token'larının ayrılması; backend varsayılan `127.0.0.1`; eski imzasız ArduinoOTA ve eski HTTP sensör alımı varsayılan kapalı; imzalı OTA'da ECDSA-P256, tam görüntü SHA-256, hedefe ve artan build numarasına bağlama, güç/alarm kilidi ve flash boş-alan rezervi. **Radyo verileri HMAC ile doğrulanır ama şifreli değildir.** Tam fiziksel saldırı direnci için ayrı üretim anahtarları, Secure Boot v2, flash/NVS encryption, kontrollü ilk-provizyon ve bağımsız güvenlik testi hâlâ gereklidir.

## Bir önemli kapasite gerçekliği

8 MB karttaki iki OTA uygulama bölümü ve yaklaşık 2.94 MiB LittleFS alanı arasında veri saklama/OTA dengesi kurulmuştur. Saha logları otomatik döngüde yaklaşık 1152 KiB üst sınırla sınırlandırıldı; 1.5 MiB OTA sahneleme + en az 128 KiB rezerv hedeflendi. Normal arka plan CSV ölçüm kaydı 30 dakikada bir, tam termal kare 2 saatte bir tutulur; her alarmda/uyandırmada ilave kayıt yapılır. Bu nedenle yoğun anomali dönemlerinde bir aylık geçmiş garantisi yoktur: kayıtları periyodik indir. LoRa'da 1 MiB dosya 96 baytlık yaklaşık 10,923 blok gerektirir; 180 saniye muhafazakâr verici aralığı tek başına yaklaşık 23 **gün**, hedef ACK'leri ve röleler daha fazlasını gerektirebilir. RF düzenlemeleri ve gerçek airtime ölçülmeden süre/yayın hızı artırılmamalıdır. Küçük parametre/model ve delta paketleri ileride bu ihtiyacı azaltacak bir optimizasyon aşamasıdır; mevcut sürüm bunlar için genel bir indirilebilir dosya yöneticisi sunmaz.

**İHA, canlı LoRa termal video ve uzaktan her Nest'e ayrı hücresel modem bu sürümün kapsamında değildir.**
