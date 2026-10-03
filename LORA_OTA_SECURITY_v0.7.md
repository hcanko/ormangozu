# v0.7.0 LoRa OTA ve siber güvenlik teknik notları

## İletişim

- Alarm: `OG4`, HMAC-SHA256'dan ilk 12 byte (96-bit) etiket, `OG_MESH_KEY`, son alan `hop` 0/1/2.
- Kontrol: `OGC2`, ayrı `OG_CONTROL_KEY`; yalnız `OG_CONTROLLER_ID` kaynaklı yeni boot+sequence komutu; hedef kimlik doğrulaması; `RESULT` dönüşü asıl tamamlanma kanıtı.
- OTA: `OGU1|kind|source|target|tid|param|data|hop|HMAC12`. Tüm LoRa frame uzunluğu hedefteki 239B altı kapıya uymalı. Tek aktif PC transferi; normal yangın ve müdahale kuyrukları düşük öncelikli OTA'dan önce işlenir. Röleler gelen doğrulanmış mesajı en fazla 2 kere iletir; `hop` yeniden HMAC'lenir. Hedef ve yetkili köprü sabittir.
- **HMAC şifreleme değildir:** yayın alanındaki biri metni dinleyebilir; anahtarı ele geçirilen aynı pilot grubu taklit edilebilir. Bu pilotta per-device anahtar, key-revocation, intrusion detection ve güvenli anahtar depolaması henüz yapılmış değildir.

## İmzalı güncelleme

Kanonik imza dizgesi: `OGOTA1|TARGET_NEST_ID|TRANSFER_ID_8HEX|BUILD|SIZE|LOWERCASE_SHA256`. Bilgisayar/offline imzalama makinesi P-256 ECDSA/SHA-256 ile `r||s` (64 byte) imzalar. Nest yalnız provision edilmiş **açık** anahtarı bilir; manifestin göndericinin getirdiği alternatif public key'ine güvenmez. Güncelleme önce LittleFS `ogota.part` içinde sahnelenir. `Q` eksik offset'i verir; tekrar gönderilen blok mevcut dosyadaki byte'larla karşılaştırılır. Sonuçta `SIZE`, dosya SHA-256, signature ve build artışı sağlanmadan aktif uygulama değişmez. `E` safhasında mevcut termal WATCH/uyarı ve son alarmın yakınlığı ile batarya yüzdesi denetlenir; `X` reboot 10s sonra yapılır. Firmware açıldıktan sonra ilk sağlıklı MLX/BME/LoRa çevriminde build NVS kaydı ve platform izin veriyorsa OTA onayı alınır.

**Sınırlamalar:** güvenli rollback davranışı hedef board, bootloader rollback ayarı, bölümleme ve sahada zorunlu fault-injection testine bağlıdır. İmzalı OTA uygulamasının kendisi, daha önce fiziksel olarak yüklenen ilk firmware'e güven varsayar; chip Secure Boot henüz aktif değildir. `OG_FIRMWARE_BUILD` firmware kaynak kodunda, bilgisayarın manifest `--build` değeriyle birebir eşleştirilmelidir; USB, debug portları ve bootrom güvenliği üretim aşamasında ayrıca kilitlenecektir. İleride yazılım imza anahtarını değiştirirken anahtar devri için ayrı uygulama protokolü gerekir.

## Radyoda tam 1 MiB maliyeti

`ceil(1048576/96) = 10923` veri bloklu STOP-AND-WAIT sisteminde varsayılan **180 sn kaynak yayın aralığı tek başına 22.76 gündür**. 10 binden fazla status/ACK, 1–2 röle ve ara alarm/yeniden gönderimler buna eklenir. Dolayısıyla 1 MiB uzaktan yüklemeyi birkaç saatlik iş olarak satmayın. `tools/rf_budget.py` belirli SF/BW/CR/CRC/preamble için tahmin verir; gerçek SX1262 airtime, gönderilen paket uzunlukları, yasal alt bant/yayın gücü/duty cycle veya LBT+AFA sahada ayrıca doğrulanmalıdır. Bir merkez bağlantısı yoksa bilgisayarın radyo köprüsüne erişimi de yoktur. Küçük model/parametre güncellemeleri veya delta OTA'nın gelecekte ayrı uygulanması beklenir.

## İki kartlık pilot güvenlik kontrolü

1. Her Nest için gerçek MAC kimliği (`WHOAMI`); her cihaza `secrets.example.h` üzerinden **ayrı yerel `secrets.h`**. İki node aynı random `OG_MESH_KEY` ve `OG_CONTROL_KEY` pilot grubu anahtarlarını ve **gerçek USB köprüsü ID'sini** paylaşır; iki anahtar birbirinden farklı olmalıdır.
2. İmza özel anahtarı ESP'ye **asla** yüklenmez veya Git'e eklenmez. En az 12 karakter parolalı PEM anahtar offline tutulur. Açık nokta `OG_OTA_PUBLIC_KEY_HEX` iki cihaza yüklenir. Demo/dummy CI firmware asla sahaya yüklenmez.
3. Bilgisayar backend'i `127.0.0.1`; `OG_CLIENT_TOKEN` yalnız veri-ingest/USB; `OG_OPERATOR_TOKEN` yalnız UI/kontrol/kalibrasyon. İnternete doğrudan port açmayın; ihtiyaç varsa güncel VPN/TLS reverse proxy + gerçek operatör kimlik yönetimi şarttır.
4. LoRa HMAC manipülasyonu, eski komut tekrarı, yanlış hedef, başka signing key, eski build, yanlış SHA/uzunluk, güncelleme anında alarm, düşük pil, elektrik kesintisi ve staging doluluğu senaryolarını fiziksel olarak deneyin.
5. İki firmware sürümü arasında OG4/OGC2/OGU1 uyumsuzluğu vardır: **ilk pilot v0.7.0'a geçerken her iki direk USB ile provision edilmelidir.** Normal OTA yalnız yeni sürümlere yöneliktir.
6. Kalıcı tesislerde eFuse/Secure Boot v2/Flash Encryption/NVS anahtar depolama, JTAG/UART recovery politikası, per-device key/rotation/revocation, audit log ve bağımsız güvenlik testini yedek kartta aşamalı yapın; ilk iki kartta geri dönüşsüz eFuse ayarı yapmayın.

## Paylaşılan flash/enerji sınırları

8MB profilinde OTA app0/app1 2.5 MiB'er, LittleFS ~2.94 MiB. Saha kayıtlarının döner sınırları iki telemetri toplam 384 KiB, iki LoRa toplam 64 KiB, iki füzyon toplam 64 KiB, iki termal toplam 640 KiB = yaklaşık **1152 KiB**. LittleFS metadata/normal payla birlikte 1.5 MiB OTA ve 128 KiB güvenlik payı hedeflenir. Normal arka plan CSV her 30 dakikada bir, MLX frame her 2 saatte bir; anomalide daha sık kayıt nedeniyle 30 günlük eksiksiz geçmiş garanti edilmez. Eski kayıtları OTA otomatik silmez; yetersiz alanda `NO_SPACE` döner, teknik ekip kayıtları arşivlemeli. Cihaz flash gerçekten 16MB ise ayrı `partitions_16mb.csv` seçilebilir. 1S pil/solar enerji dengesini ayrı test edin.
