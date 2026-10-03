# Orman Gözü v0.7.0 — İlk kurulum ve güvenli uzaktan güncelleme

> Kapsam: Bilgisayar ⇄ USB ile bağlı Nest A ⇄ LoRa ⇄ hedef Nest B (gerekirse en fazla 2 yönlendirme). İHA dahil değil. İlk v0.7.0 kurulumu **iki cihazda da USB ile** yapılır; protokol önceki sürümlerle radyo uyumlu değildir.

## 1. Bilgisayar hazırlığı

Python 3.12, Node.js 22, PlatformIO kurulu olmalı. Gerçek kart modelini, flash kapasitesini ve radio pinlerini okuyup PlatformIO env'ini seçin.

```bash
python -m pip install -r backend/requirements.txt -r tools/requirements.txt pytest platformio
python -m pytest -q tests backend/tests
cd OrmanGozu_Node
pio run -e heltec_wifi_lora_32_V3
cd ../frontend
npm ci
npm run build
```

Başarılı otomatik test sayısı kaynak incelemesinde 22; yerel ESP/npm derlemesi bu ortamda yapılamadı. GitHub'a bu ZIP içeriğini gerçek credential olmadan `main` veya bir `v070` dalında yüklersen `.github/workflows/verify-v070.yml` Python, frontend ve ESP derlemesini test eder. CI derlemesindeki dummy `secrets.h` **ASLA gerçek karta yüklenmez**.

## 2. Anahtarlar (ilk radyo güncellemesinden ÖNCE)

Önce proje dışındaki `../secure-keys/` klasörünü oluşturun. Özel signing PEM'i repo DIŞINDA ve parolalı tutun. PC'de örnek:

```bash
# OG_SIGN_PASSWORD ortam değişkenini en az 12 karakterlik geçici parola ile önceden ayarlayın.
python tools/lora_ota.py keygen --private-key ../secure-keys/og-signing-root.pem
```

Araç `.pubhex` dosyasını da oluşturur. `OrmanGozu_Node/src/secrets.example.h` dosyasını her karta özel `secrets.h` olarak kopyalayın ve **tüm placeholderları değiştirin**:

- `OG_MESH_KEY`: rastgele 32+ karakterlik sadece uyarı grubu anahtarı
- `OG_CONTROL_KEY`: bundan **farklı** rastgele 32+ karakterlik kontrol/OTA radyo grubu anahtarı
- `OG_CONTROLLER_ID`: bilgisayara USB ile bağlı radyo köprüsünün gerçek `WHOAMI` / `NEST-` kimliği (iki kartta aynı yetkili köprü)
- `OG_OTA_PUBLIC_KEY_HEX`: `.pubhex` içindeki 130 haneli P256 açık nokta
- `OG_MAINT_AP_PASSWORD`, `OG_MAINT_HTTP_PASSWORD`, `OG_OTA_PASSWORD`: birbirinden ayrı 12+ karakter parolalar (legacy OTA zaten kapalı)
- Son olarak `OG_PROVISIONED 1`.

Aynı OG4/OGC2/OGU1 protokol sürümünü iki karta USB ile yükleyin. **Özel signing PEM veya backend token'ları hiçbir Nest'e yüklenmez.**

Her firmware yüklemesinden sonra cihazı otomatik filo kaydına alın ve yerel
depolama/LoRa/termal/çevresel sensör öz-testini çalıştırın:

```bash
python tools/provision_device.py --port COM5 --backend http://127.0.0.1:8000
```

Araç `WHOAMI` ile eFuse/MAC tabanlı `NEST-...` kimliğini okur; elle cihaz ID'si
verilmez. Bütün kontroller geçerse backend durumu `READY_FOR_INSTALLATION`
yapar. GNSS entegrasyonu etkinleşene kadar `location_status=PENDING` normaldir.

## 3. Backend ve kullanıcı arayüzü

`backend/.env.example` içeriğini gerçek, birbirinden farklı rastgele `OG_CLIENT_TOKEN` (yalnız collector) ve `OG_OPERATOR_TOKEN` (UI/yönetim) ile yerel ortamınıza aktarın. API'yi doğrudan internete açmayın.

```bash
cd backend
python main.py
# ayrı terminal / proje kökünden:
cd frontend
npm run dev
# ayrı terminal / proje kökünden:
python tools/collect_usb.py --port COM5 --output pilot_usb.jsonl --backend http://127.0.0.1:8000 --replay
```

Windows COM5 yerine gerçek bağlantı noktasını yazın. Collector kullanımı `OG_CLIENT_TOKEN` ortam değişkeni gerektirir. UI'da `OG_OPERATOR_TOKEN` kullanılır. Normal kontrol `Nest Kontrol` ekranında seçilen hedefe `STATUS`, `SAMPLE`, `FAST`, `AUTO` gönderir; PAN/TILT sadece servo takıldıysa ve firmware'de açıkça etkinleştirildiyse kullanılabilir.

## 4. Gelecekteki yeni firmware'i PC'de imzalama

Her güncelleme için `OrmanGozu_Node/src/config.h` içindeki `OG_FIRMWARE_BUILD` sayısını ileri artırın (örn. 701); yeni firmware'i **gerçek donanım için** derleyip çıkan `.bin` dosyasının 1.5 MiB'den küçük olduğunu doğrulayın. Manifest `--build` tam olarak derlediğiniz binary'nin `OG_FIRMWARE_BUILD` numarası olmalı; her hedef için ayrı manifest oluşturun.

```bash
# OG_SIGN_PASSWORD ortam değişkeni hâlâ ayarlı; özel anahtar repo dışında.
python tools/lora_ota.py sign --private-key ../secure-keys/og-signing-root.pem --firmware firmware.bin --target NEST-112233AABBCC --build 701 --manifest nest-b-701.signed.json
```

## 5. LoRa üzerinden hedefe aktarım

**USB COM portunu tek program açabilir:** `collect_usb.py`'yi durdur, sonra aynı USB köprüsüne OTA programını bağla. Seri `WHOAMI` yanıtı, kaynak `OG_CONTROLLER_ID` ile eşleşmeli.

```bash
python tools/lora_ota.py send --port COM5 --manifest nest-b-701.signed.json --firmware firmware.bin --relay-hops 0 --min-interval 180
```

`--relay-hops` için 0 (direkt), 1, 2 kullanılabilir. Program her 96B bloktan sonra cihazın `offset` yanıtını bekler; ağ kesilirse **aynı manifest, aynı imaj** ile komutu tekrar çalıştırınca kaldığı yerden devam eder. İmza/SHA, hedef ID ve build uygunsa pasif OTA bölümü hazırlanır, sonra reboot ve firmware build sorgulanır. Yanlış imza, yeni olmayan build, az batarya, yangın şüphesi veya dolu LittleFS reddedilir.

**Hız uyarısı:** 1MiB ≈10,923 blok. 180 saniyelik yalnız kaynak yayın aralığı ≈23 gün; her status, radyo tekrarları/röle ve yasal bant kuralları süreyi uzatır. Bu işleyişi kontrollü bench denemesi dışında otomatik sahaya uygulamadan RF ölçümü yapın. `python tools/rf_budget.py` yalnız hesap yardımcısıdır.

## 6. Yakından (bakım hotspot'undan) aynı imzalı güncelleme

Fiziksel USB seri porta `MAINT ON` yazın, Nest'in 10 dakika açık olan şifreli `OG-MAINT-*` ağına bağlanın, `OG_MAINT_HTTP_PASSWORD` ortam değişkenini tanımlayın:

```bash
python tools/lora_ota.py send-local --manifest nest-b-701.signed.json --firmware firmware.bin
```

Bu yol da imza ve SHA kontrolünü ATLAMAZ. Eski `ArduinoOTA` default kapalıdır. Güncellemeden sonra `WHOAMI` ile sürümü doğrulayın.

## 7. Saha öncesi kritik testler

`TEST_REPORT_v0.7.0.md` ve `KALAN_ISLER_v0.7.0.md` içindeki derleme/gerçek LoRa/power/reset/flash doluluk/Secure Boot-audit kontrol listesi bitmeden 'her şey tamam, yalnız saha' olarak işaretlemeyin.
