# Orman Gözü — Bilgisayardaki sürüm ile v0.5.0 karşılaştırması

Tarih: 2026-10-02. İncelenen iki ZIP:
- `ormangozu-new-.zip` (bu raporda **PC**)
- `OrmanGozu_Whisper_PC_OTA_v0.5.0_SOURCE.zip` (bu raporda **v0.5**)

## Kısa sonuç
PC paketi tek parça olarak v0.5'ten eski sayılmamalı. ESP kaynaklarının ana iskeleti daha eski offline thermal-mesh v0.3'e ait; buna karşın PC backend'i ve frontend veri bağlantısı v0.5'in bazı dosyalarından daha gelişmiş. v0.5 cihaz tarafında OG4 kimlik doğrulamalı LoRa, 30 sn peer scan, bilgisayara USB JSON kayıt aktarımı, daha kontrollü bakım Wi-Fi/OTA ve PC Whisper panelini ekliyor. Dolayısıyla hiçbir ZIP diğerinin üzerine olduğu gibi kopyalanmamalı.

## Dosya karşılaştırması
Geçici/ikili dosyalar (ör. .pyc, SQLite .db, görseller, CSV, node_modules, cache) hariç: PC 72, v0.5 70 dosya; 55 ortak dosyanın 28'i aynı, 27'si farklı. PC'ye özgü 17, v0.5'e özgü 15 dosya var. PC ESP ana kaynakları önceki `orman_gozu_thermal_mesh_v0_3.zip` ile birebir eşleşiyor.

## Farkların özeti

| Konu | PC | v0.5 | Entegrasyon notu |
|---|---|---|---|
| Yerel termal algoritma | Var (v0.3) | Korundu, küçük düzeltme | ESP v0.5 üzerinden ilerle |
| LoRa Whisper | OG3, ALERT/ACK/REPORT, 2 dk fast mode | OG4, HMAC etiket, boot/seq/replay; 30 sn peer scan, FUSION yayın | OG3 ve OG4 birbiriyle uyumsuz; iki cihaz aynı protokolde olmalı |
| Nest kimliği | MAC son 4 hanesi (`TOWER-...`) | Tam MAC + kalıcı boot counter (`NEST-...`) | Yeni kimliği benimse, eski log eşleme göçü gerekebilir |
| LittleFS | Ring log, 3 termal dosya, daha geniş kota | Daha kontrollü mount, 2 termal dosya, daha düşük kota ve seyreltilmiş baseline | Kart flash ve 1 aylık retention ölçülmeli; eski partition'la doğrudan flashlama yapılmamalı |
| Wi-Fi/OTA | Periyodik hotspot arama, koda gömülü pilot kimlik bilgileri; kimlik kontrolü olmayan log silme endpoint'i | Fiziksel USB `MAINT ON` ile 10 dk AP, ayrı AP/HTTP/OTA parolası, silme endpoint'i yok | v0.5 lehine; imzalı OTA/rollback henüz kanıtlanmadı |
| PC bağlantısı | HTTP telemetri ve dosyadan CSV import | Bir Nest USB üzerinden JSONL collector -> `/api/mesh/events` | İkisini birleştir; PC'nin kendisinde LoRa yok, uzak canlı izleme için köprü gerek |
| Backend offline kayıt | Geniş SensorLogDB, boot/sequence, duplicate kontrolü, migration, CSV içe aktarma | Ayrı pilot_events SQLite + label, ancak eski ana sensor_logs modeli | PC ana modelleri ve additive migration korunmalı, pilot mesh tabloları eklenmeli |
| Kalibrasyon | GET/POST ve SQLite persist | Yalnız POST ve bellek içi global | PC kalibrasyon sistemi korunmalı |
| Test olayı/etiket | TestEventDB ve `/test-events` | `/api/mesh/labels` içinde olay-etiketleme, export.csv | İki veri tipini ilişkilendirerek sakla |
| Frontend useTowers | Ortamdan API/WS, reconnect, 30 sn recovery GET, full dashboard desteği | Sabit localhost; reconnect/full dashboard işleme eksik | PC hook kesinlikle korunmalı |
| PC arayüz | Mevcut harita/uyarı/analitik | Yukarıdakilere ek `/whisper` ve WhisperPilot | PC arayüzüne sadece yeni panel/menü ekle |

## Birleştirme için önerilen teknik temel
1. **ESP:** v0.5 ESP dosyaları başlangıç; `config.h`, `platformio.ini`, RF pinleri ve 8/16 MB partition kart gerçekliği üzerinden tek tek doğrulanmalı. USB offline toplayıcı ve LoRa protokol testleri korunmalı.
2. **Backend:** PC `models/database.py`, `models/orm.py`, `models/schemas.py`, `api/routers.py`, `main.py`, `services/sensor_fusion.py` korunarak v0.5 `api/mesh_events.py` entegre edilmeli. İki SQLite akışının ortak anahtarları (device_id, boot, sequence, event_id) tasarlanmalı.
3. **Frontend:** PC `src/hooks/useTowers.ts` ve `src/lib/api.ts` korunmalı; v0.5 `WhisperPilot.tsx` ile router ve Layout menüsü eklenmeli. Hardcoded localhost geri gelmemeli.
4. **Veri/AI:** Offline termal v2/v3 decoder, yerel CSV import, USB JSONL replay, ground-truth label ve bakım/kayıt metadatası tek veri şemasında buluşmalı. Alarm olmayan örnekler de tutulmalı.
5. **Uyumluluk:** OG3/OG4 LoRa protokolleri birlikte çalışmaz. PC'deki `TOWER-...` ile v0.5'teki `NEST-...` kimlikleri farklıdır. Eski OGFR v2 termal kayıtları decoder tarafından desteklenir; v0.5 kayıt sürümü v3'tür. Flash partition değişikliği logları silebileceği için mutlaka yedek alınmalı.

## Test/Doğrulama durumu
- PC paketi `backend/tests/test_smoke.py`: **2 passed**.
- v0.5 `tests/test_pilot.py`: **9 passed**.
- Bunlar Python testleridir; PlatformIO/C++ derlemesi, frontend'in bağımlılıklar yüklenmiş hâlde `npm run build` sonucu, gerçek LoRa ve ArduinoOTA denemesi **bu karşılaştırmada doğrulanmadı**.
- Her iki sürümün tek tek test geçmesi birleşik yazılımın hazır olduğu anlamına gelmez. Birleşik sürüme uçtan uca yeni regresyon testleri yazılmalı.

## Sahadan önce yüksek öncelikli üç çakışma
- v0.5 ana backend dosyalarını PC'nin üzerine kopyalamak offline CSV içe aktarma, sensor log alanları, migration, test olayları ve kalibrasyonun kalıcılığını geri götürür.
- v0.5'in `useTowers` dosyası PC'nin tam dashboard WebSocket mesajlarını işleme ve otomatik yeniden bağlanma özelliklerini kaybeder; harita ilk veride takılabilir.
- PC `partitions.csv` 16 MB yerleşimi tarif ederken `platformio.ini` bunu açıkça kullanmaz; v0.5 8 MB bölümlendirme seçer. Hangi kartın ve flash kapasitesinin takılı olduğunu doğrulamadan herhangi birini sahadaki Nest'e yükleme.
