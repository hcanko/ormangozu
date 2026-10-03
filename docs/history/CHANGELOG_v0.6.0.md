# Orman Gözü v0.6.0 — 2026-10-02

## Kaynaklar

- PC `ormangozu-new-.zip`: backend (`main` lifespan/migration, extended ORM/schemas, CSV import, persisted calibration, old dashboard, tests), frontend `src/pages/*`, `src/hooks/useTowers.ts`, `src/lib/api.ts`, frontend manifest and lock.
- v0.5.0: entire OG4 ESP/C++ directory, USB tools/decoder, mesh event API and related regression tests, `WhisperPilot.tsx`.
- Eski `src/components/*` kopyaları bilerek kaldırıldı; PC'deki daha yeni `src/pages/*` gerçek App router'a bağlandı.

## Yeni düzeltmeler

- Backend ana router ile `api.mesh_events` bir arada; API sürümü `0.6.0-pilot`.
- Offline `gas_raw`, `fire_status`, `trigger`, `ambient_temp`, `hotspot_threshold`, `largest_hotspot_cluster`, `persistence_count`, `health_level`, `network_confirmed` alanları korunuyor; solar `-1` NULL.
- SensorLogDB'ye yalnız yeni alanlar ekleyen SQLite migrasyonu; `kurulum.py` yıkıcı resetten güvenli `create_all` + additive migration'a dönüştürüldü.
- Bilgisayardaki `useTowers` reconnect/recovery ve yapılandırılabilir API adresleri korunuyor; WhisperPilot da `apiUrl` kullanıyor.
- C++ semantiği `FIRE_NETWORK_CORROBORATED` olarak adlandırıldı; RF OG4 sayısal protokolü değişmedi.
- USB collector JSONL satırını HTTP denemesinden önce `flush + fsync` ile kaydediyor; timeout durumunda çalışmayı sürdürerek kayıt kaybı riskini düşürüyor.
- Flash için varsayılan 8MB ve yalnız doğrulanmış cihazlar için açık 16MB PlatformIO hedefi.
- İzole birleşik API/regresyon testi ve GitHub Actions kaynak/firmware derleme doğrulaması eklendi.

## Kapsam dışı / doğrulanmamış

- Fiziksel ESP/Heltec/SX1262 ile 2 cihaz testi, LoRa airtime/duty cycle sahası, gerçek OTA ve güç tüketimi.
- Kaynak oluşturan bu ortamda PlatformIO/npm bağımlılık indirme/derleme tamamlanamadı (ağ DNS erişimi yok). Dolayısıyla **kullanıcıya onaylı .bin veya derlenmiş frontend** verilmemektedir.
- Gerçek güncel PC merkezinin uzaktaki LoRa ağına erişmesi için USB ile bağlı bir Nest veya gelecekte gateway gerekmektedir. USB summary, eski tam termal harita HTTP hattının yerini otomatik almaz.
- İHA ve yangın tahmin AI modeli henüz yoktur; yalnız pilot veri/etiket altyapısı hazırlanmıştır.
