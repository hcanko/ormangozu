# v0.6.1 sonrası doğrulama ve saha hazırlığı

## 1 — Donanımı koruyarak temel doğrulama (zorunlu)

- Her iki Heltec kartın V3/V4 modelini, SX1262 RF pinlerini, LoRa antenini, 8/16 MB flash kapasitesini ve pin haritasını fiziksel olarak doğrula.
- Veritabanı, NVS ve LittleFS yedeği al. İki karta da aynı v0.6.1 ve geçerli, sır olarak saklanan pilot mesh anahtarını yükle; AP/HTTP/OTA şifrelerini cihaz bazında ayır.
- PC'de `pio run` ile firmware'i ve `npm ci && npm run build` ile frontend'i doğrula. Bunu fiziksel flash/servo testinden ayrı tut.
- Sensör (MLX90640, BME680) kalibrasyonu, batarya, panel/regülatör ve dönen LittleFS arşivlerini test et.

## 2 — Çift yönlü haberleşme ve müdahale kabulü (zorunlu)

- A↔B OG4 ALERT / ACK / REPORT / CONFIRM; alarmlarda ağ teyidi ama otomatik 'yangın kesin' kararı verilmemesi.
- USB bağlı Nest A'dan Nest B'ye STATUS, SAMPLE, FAST; dönen sıcaklık, score, health ve batarya değerleri; aynı cihazdan yerel komut.
- Hatalı hedef ID, yanlış HMAC, tekrar oynatılmış/eskimiş OGC1 komutu, kopuk LoRa, reset, yarım kalmış ACK/RESULT, PC/USB kesintisi. Bekleyen işleri bekleme bitince kendiliğinden çalıştırmama.
- Öncelik: yerel WATCH ve eşten ALERT geldiğinde MANUAL kapatılsın; kontrol trafiği kritik alarm iletimini açlığa düşürmesin.

## 3 — Kamera hareket mekanizması (gerçek servo gerekiyorsa)

- Servo + mekanik pan/tilt, pinler, açı limitleri, ayrı ve kararlı enerji beslemesi kurulmalı. Bu olmadan yazılımdan kamera fiziksel hareket edemez.
- Sadece doğrulama sonrası `OG_ENABLE_PAN_TILT=1` ve gerçek GPIO'lar tanımlanmalı. Her iki eksen/stop/home, 2 dakikalık manuel süre aşımı, komut/alert çakışması ve güç dalgalanması test edilmeli.
- Sabit kamera ile ilk iki-Nest pilotu yapılabilir; servo sırf ilk veri toplama için zorunlu değildir.

## 4 — Pilot saha testi (zorunlu)

- 6–12 saat masa testi; yüzlerce yerel ve uzak komut/mesaj döngüsü; 24–72 saat enerji ve kilitlenme takibi; sonra ay boyunca internetsiz veri kayıt testi.
- İzinli, güvenli ve kontrollü ısı kaynaklarıyla 5/10/20/30/50m ve uygun arazide mesafe/gece/gündüz testleri; LoRa RSSI/SNR, algılama gecikmesi, yanlış/kaçan alarm ölçümü.
- Hava/güneş/gölge, su/yoğuşma ve servis aralığı; düzenli log indirme ve ikincil yedekleme.

## 5 — Sonraki mühendislik (pilottan öğrenerek)

- Tam kapsamlı tekil cihaz anahtarları, yetki/rol, cihaz revocation, imzalı OTA, rollback ve değişiklik denetimi; pilot ortak anahtar üretime uygun değildir.
- Bilgisayarın 7/24 izlemesi gerekirse ayrı LoRa USB/gateway ya da sabit PC'ye bağlı Nest; gateway üzerinden modem/uplink sonraki aşama.
- AI için normal/alarmlı örnekler, ham termal kareler, sıcaklık/hava durumu ve bağımsız olay doğrulama etiketlerini biriktir; iki Nest ile model doğruluğu iddia etme.
- İHA entegrasyonu bu aşamanın dışında tutuldu.
