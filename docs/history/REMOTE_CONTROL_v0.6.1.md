# Orman Gözü v0.6.1 — Nest uzaktan kontrol (iki düğümlü pilot)

## Fiili iletişim yolu

Bilgisayar (React kontrol ekranı) -> local FastAPI `POST /api/control/commands` ->
USB collector (`tools/collect_usb.py`) -> USB ile bağlı **Nest A** -> adresli,
HMAC doğrulamalı **OGC1/CMD** LoRa paketi -> **Nest B** -> SensorTask ->
OGC1/RESULT -> Nest A USB `OGCTRL:` -> collector -> API -> kontrol ekranı.

**Üçüncü gateway gerekmez:** Pilot boyunca USB ile PC'ye bağlanan Nest A köprüdür.
Bu, bir üretim gateway'i ya da internete açık uzaktan erişim sistemi değildir.
A ve B'nin ikisine de v0.6.1 firmware ve aynı pilot LoRa anahtarı gerekir.

## Desteklenen adresli komutlar

| Komut | Argüman | İşlem |
| --- | --- | --- |
| STATUS | 0 | Son kaydedilmiş termal/enerji özetini getirir |
| SAMPLE | 0 | Hemen yeni ölçüm alır ve özetini döner |
| FAST | 0 | 2 dakikalık hızlı örnekleme başlatır ve ilk ölçümü döner |
| AUTO | 0 | Manuel moddan çık, kamerayı merkeze al (servo takılıysa) |
| MANUAL | 0 | Servo varsa en fazla 2 dakika manuel müdahale |
| PAN/TILT | 20–160 | Servo varsa **mutlak açı** komutu; MANUAL şart |
| HOME | 0 | Servo varsa merkeze dön ve AUTO |

Cihaz tarafında komut **yalnızca hedef Nest kimliğine** gönderilir, HMAC-SHA256
etiketi denetlenir ve komutun `sender_boot + sequence` değeri NVS'de saklanarak
eski paketin yeniden işletilmesi engellenir. Kullanıcı tarafında genel amaçlı
komut veya `EXEC`, uzaktan `ERASE`, `STOP_DETECTOR`, firmware-over-LoRa yoktur.

Üretim öncesi: ortak pilot anahtarı ayrı kimlik doğrulamalı komuta/role dayalı
anahtarlara çevrilmeli, yetki/audit gözden geçirilmeli, komut iletim hatalarında
uçtan uca durum teyidi radyo kayıp testleriyle güçlendirilmelidir.

## Servo durumu / donanım güvenliği

`src/config.h` içinde **OG_ENABLE_PAN_TILT = 0** güvenli varsayılandır. Şu anda
`MANUAL/PAN/TILT/HOME` komutları `UNSUPPORTED` döner. Yani yazılımda komut yolu
hazır olsa da fiziksel kamera dönüşü **çalıştığı iddia edilmemektedir**.

Donanım tamamlandığında iki servo için güç rayı, mekanik açı sınırları, ayrı GPIO,
ESP32-S3 pin uyumluluğu ve gerçek PWM sinyali doğrulandıktan sonra
`OG_PAN_SERVO_PIN`, `OG_TILT_SERVO_PIN` doldurulup etkinleştirilebilir.
ESP32 GPIO'sundan doğrudan servo beslemeyin; uygun regülatör/güç izolasyonu
kullanın. İlk testte kamera mekanik stoplarına vurmasın. PAN/TILT 20–160°
mutlak komutlarıdır, sürekli video joystick değildir.

Manuel mod en fazla iki dakikadır; yerel WATCH veya eş Nest ALERT olduğunda
otomatik moda döner. **Termal ölçüm ve yangın algılama durdurulmaz**, ancak
operatör kamerayı çevirirse eski görüş sektörünün geçici olarak taranmayacağını
unutmayın. Fiziksel servo/360° rutin tarama ancak donanım gösterildiğinde onaylanır.

## Yerel kurulum

1. FastAPI'yi `127.0.0.1` üzerinde açın. `.env` örneğindeki `OG_CLIENT_TOKEN`
   yerine rastgele 32+ karakter kullanın. Bu API'yi internetten erişilebilir yapmayın.
2. Nest A'yı USB'ye bağlayın ve `tools/requirements.txt` paketlerini kurun.
3. İki Nest'e aynı OG4 Whisper + OGC1 kontrol firmware'i kurun; kimlik ve
   gerçek PCB/flash ayrıntılarını doğrulamadan yükleme yapmayın.
4. Komut toplayıcısı (`--backend` ile çalışmalı):

   ```bash
   # Linux/macOS
   export OG_CLIENT_TOKEN='your-long-random-token'
   python tools/collect_usb.py --port /dev/ttyACM0 --backend http://127.0.0.1:8000 --replay
   # Windows PowerShell: $env:OG_CLIENT_TOKEN="..."
   # python tools/collect_usb.py --port COM4 --backend http://127.0.0.1:8000 --replay
   ```

5. React yan menüden **Nest Kontrol** sayfasını açın, token ve tam cihaz kimliğini
   girin. `STATUS`, `SAMPLE`, `FAST` komutlarını deneyin. İsterseniz doğrudan
   USB seri terminalinde şu pilot komutu gönderilebilir:

   ```text
   WHOAMI
   CTRL 123 NEST-001122AABBCC STATUS 0
   ```

   Aynı portu USB collector ve seri terminal **eşzamanlı** açamaz.

## Bağlantı kaybı / bekleyen komut davranışı

- `queued` iş 45 saniye içinde collector'a teslim edilmezse `timeout` olur.
- Collector işi sahiplenmiş olsa da 50 saniye içinde sonuç gelmezse `timeout` olur.
- Bu zaman aşımındaki komut, collector tekrar bağlandığında otomatik yürütülmez.
- OGC1 CMD/RESULT, OG4 alarm trafiğinden ayrı tutulmuştur; normal WATCH ve
  ALERT/ACK/REPORT davranışı korunur.
- Geç gelen sonuçlar USB olay günlüğünde bulunabilir, fakat kapatılmış işi
  geriye dönük başarıya çevirmeyiz.
- Firmware/OTA'nın internetten veya kilometrelerce uzaktan açılması yoktur:
  mevcut `MAINT ON` + bakım Wi-Fi'si üzerinden yakından yapılır.
- Termal **tam kare LoRa'dan canlı yayınlanmaz**; disk logu bakım bağlantısıyla
  indirilir. LoRa sonuç paketi sadece ölçüm özetidir.

## Test matrisi (fiziksel doğrulama şart)

- Önce iki kartın kimliği/flash/LoRa pinleri doğrulanmalı.
- STATUS, SAMPLE, FAST, komut ACK/RESULT, paket kaybı/tekrar/reboot testleri.
- Eş Nest kritik alarm sırasında manuel müdahalenin iptal olması.
- Eski/replay komutunun yeniden servo çalıştırmaması.
- Relay USB sökülünce `timeout`, yeniden bağlanınca eski komut yürütülmemesi.
- Servo takılmadan PAN/TILT komutlarına `UNSUPPORTED` dönüşü.
- Servo eklendikten sonra ayrı güç, mekanik stop, pan/tilt sınır ve fail-safe testi.
- 24–72 saat düşük güç, sensör ve LittleFS dayanıklılığı; sonra bir aylık pilot.
