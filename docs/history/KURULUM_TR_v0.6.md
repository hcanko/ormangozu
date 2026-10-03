# Orman Gözü v0.6.1 — yerel iki-Nest pilotu

Bu bir **kaynak kodu / test adayıdır**, doğrulanmış yangın tespit ürünü veya karta doğrudan yüklenecek onaylı `.bin` değildir. Eski `.db`, CSV, LittleFS kayıtlarını ayrı yedekleyin. İHA entegrasyonu bilerek kapsam dışında bırakılmıştır.

## Mimari

- Nest A/B yerel MLX90640/BME680 okur ve yerel termal ağırlıklı **pilot** Fire Score oluşturur. Fire Score yangın ihtimali olarak kalibre edilmiş bir yüzde değildir.
- OG4 Whisper: HMAC doğrulamalı küçük LoRa `ALERT → ACK → komşu 30 sn hızlı tarama → REPORT`. Uygun ayrı kanıt varsa `NETWORK_CORROBORATED`; bu *kesin yangın teşhisi değildir*.
- Her Nest kendi normal dönem ve anomali örneklerini LittleFS döner kayıtlarıyla saklar. 768 piksellik OGFR v3 ham kareler LoRa üzerinden gitmez.
- Bir Nest bilgisayara **USB ile bağlandığında** USB collector `OGTELEM`, `OGMESH`, `OGFUSION`, `OGCTRL` satırlarını `pilot_usb.jsonl` dosyasına kaydeder ve yerel FastAPI'ye gönderir. Yalnızca bilgisayarın olması LoRa sinyali almasına yetmez; uzaktaki iki direği sürekli izlemek için ileride LoRa–USB/IP gateway gerekir.
- `Whisper Pilot` sayfası (yeni `/control` Nest Kontrol panelinden de adresli komut verilebilir) USB olaylarını gösterir. Eski ana harita/ısı haritası, orijinal HTTP telemetriyi ve ayrı CSV içe aktarmayı desteklemeye devam eder. **USB ile yalnız özet olaylar alınırken ana haritada 768 piksel termal görüntü beklemeyin.**

## Bilgisayarda başlatma

Python 3.11+ önerilir. `backend/.env.example` yalnız örnektir; uygulama `.env` dosyasını otomatik yüklemez. Aynı token'ı **çalıştırılan kabuğun ortam değişkeninde** backend, USB collector ve web arayüzü için kullanın.

```powershell
# Windows PowerShell, depo ana dizininden:
cd backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
$env:OG_CLIENT_TOKEN = "BURAYA_UZUN_RASTGELE_YEREL_TOKEN"
$env:ENABLE_HTTP_WHISPER = "0"
python -m uvicorn main:app --host 127.0.0.1 --port 8000
```

Yeni terminal (depo ana dizini):

```powershell
python -m pip install -r tools/requirements.txt
$env:OG_CLIENT_TOKEN = "AYNI_TOKEN"
python tools/collect_usb.py --port COM4 --output pilot_usb.jsonl --backend http://127.0.0.1:8000
# COM4 yerine Aygıt Yöneticisi'ndeki gerçek USB COM portunu yazın.
```

Bilgisayar/backend kapanıp açıldıysa, JSONL silinmeden **collector kapalıyken** eski olayları tekrar gönderin:

```powershell
python tools/collect_usb.py --replay-only --output pilot_usb.jsonl --backend http://127.0.0.1:8000
```

Frontend (üçüncü terminal):

```powershell
cd frontend
npm ci
npm run dev
```

Tarayıcı: `http://localhost:5173/whisper` veya `http://localhost:5173/control`; token'ı ekrandaki alana girin. Bağlantı başka PC/port üzerinden kurulursa `VITE_API_URL` değerini, **sonunda `/api` olacak şekilde** ayarlayın (ör. `http://127.0.0.1:8000/api`); `src/lib/api.ts` ana adres kaynağıdır. Erişimi halka açık internete açmayın.

## ESP32'ye firmware derlemek / yüklemek

**ÖNCE** kart revizyonu (V3/V4), SPI/RF pinleri, frekans ve gerçek flash 8/16 MB kapasitesini kontrol edin. Varsayılan hedef `heltec_wifi_lora_32_V3`, **8 MB** bölümleme varsayımıdır; sadece fiziksel olarak doğrulanmış 16 MB için `heltec_wifi_lora_32_V3_16mb` hedefi vardır. Bölümleme değişikliği eski LittleFS/NVS verilerini silebilir. Cihazlar sahadaysa önce logları indirin.

Her iki kartta ayrı `OrmanGozu_Node/src/secrets.example.h` → `secrets.h` kopyalayın. Tüm örnek değerleri değiştirin, `OG_PROVISIONED 1` yapın; aynı iki-Nest pilotunda `OG_MESH_KEY` ortak, bakım AP/HTTP/OTA şifreleri **cihaz başına farklı** olmalıdır. `secrets.h` Git tarafından yok sayılır. Cihaza özel gerçek anahtarları **sohbete, ZIP'e veya GitHub'a yüklemeyin**.

```powershell
python -m pip install platformio
cd OrmanGozu_Node
pio run -e heltec_wifi_lora_32_V3
# Cihaz kart/flash/revizyonu doğrulandıktan sonra:
# pio run -e heltec_wifi_lora_32_V3 -t upload
pio device monitor -b 115200
```

**Her iki Nest OG4 çalıştırmalıdır; OG3 ile OG4 konuşamaz.** RF/anten düzenini, kart pinlerini ve güç bağlantılarını önceden doğrulayın. `ENABLE_LIGHT_SLEEP=0` olarak bırakıldı: DIO1 ile uyanma donanım testinden sonra açılabilir.

## OTA (pilot)

Cihaz normalde Wi-Fi kapalı çalışır. USB serial üzerinden `MAINT ON` + Enter verince 10 dakikalık, parola korumalı Nest bakım AP'si açılır. `http://192.168.4.1/` üzerindeki Basic auth ile loglar indirilir. ArduinoOTA, bakım bağlantısında cihaz IP'sine (`192.168.4.1`, port 3232) ve ayrı OTA parolasına firmware gönderir. İlk OTA'yı **yedek kartta** deneyin. Bu, henüz üretim seviyesi imzalı OTA, garantili rollback veya otomatik filo güncellemesi değildir. Firmware dosyasını LoRa üzerinden göndermeyin.

## Saha ve veri notları

- Eski CSV içe aktarma: `POST /api/import/device-log`. OG4 `telemetry_0.csv` ve `telemetry_1.csv` desteklenir; bilinmeyen solar ölçüm `-1` → NULL saklanır. Uptime bir UTC tarih/saat değildir. Gerçek ölçüm saatini çıkarmak için saha başlangıç/bakım zamanı ayrıca not edilmelidir.
- USB Whisper verisi: `/api/mesh/events`; etiketler `/api/mesh/labels`; ham CSV dışa aktarma `/api/mesh/export.csv`. Mesh olay veritabanı `OG_PILOT_DB` (varsayılan `pilot_events.sqlite3`), eski telemetri veritabanından ayrıdır. Ortak bağlama anahtarları `device_id`, `boot/boot_id` ve `sequence` olabilir, ancak uzak Nest'in `source_device_id` bilgisini ve olayın **köken boot ID**'sini dikkate almadan otomatik birleştirme yapmayın.
- Termal OGFR v2/v3: eski `frames_1.bin`, sonra yeni `frames_0.bin` sırasıyla `python tools/decode_frames.py --device-id NEST-... --include-pixels frames_1.bin frames_0.bin --output thermal.jsonl`.
- İleride AI çalışması için yalnız alarmları değil, olağan dönemleri, negatif/pozitif olayları ve bağımsız doğrulama etiketlerini saklayın; iki direk ve kısa pilot yangın tahmin modeli doğrulamak için yeterli değildir.
- LittleFS dönen günlükler sonsuz arşiv değildir. Düzenli indirip iki ayrı yerde yedekleyin. Kontrolsüz yangın testi yapmayın.

## Kabul testi ve bilinen sınırlar

1. Her iki cihazda flash/boot, sensör/anten doğrulama ve 6–12 saat masa testi.
2. Çok sayıda farklı OG4 `ALERT → ACK → REPORT`, tekrar, reset, sıralama, packet loss senaryosu.
3. USB collector kesilip açılınca JSONL replay ve aynı kaydın çoğalmaması; OGFR frame decode/CRC ve dosya rotasyonu.
4. 24–72 saat güç/kararlılık ve güvenli ısı kaynağıyla gündüz-gece testleri. Sonra uzun saha pilota geçilebilir.

GitHub'a bu kaynakları yüklediğinizde `.github/workflows/verify-v061.yml` bağımsız Python, npm ve PlatformIO **derleme doğrulaması** yapar. CI yalnız geçici sahte şifreyle firmware derler ve bu firmware'i dağıtmaz; gerçek cihaz anahtarlarını her cihazda ayrı üretin.

## v0.6.1: Uzaktan müdahale

`REMOTE_CONTROL_v0.6.1.md` rehberini okuyun. PC kontrolü için USB collector **--backend** ile çalışmalı. Servo/Pan-Tilt varsayılan olarak etkin değildir, hareket komutları fiziksel kurulum doğrulanana kadar UNSUPPORTED döner. OTA hâlâ yerel bakım Wi-Fi'sindedir; LoRa üzerinden firmware gönderilmez.
