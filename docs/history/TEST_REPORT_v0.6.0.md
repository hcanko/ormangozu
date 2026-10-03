# v0.6.0 — yapılan ve yapılamayan doğrulama

2026-10-02 / bu teslimin kaynak kodu.

**Başarılı**
- `python -m compileall -q backend tools tests` (Python sözdizimi).
- `python -m unittest discover -s tests -v`: **10/10** (OG4 paketi ve HMAC/doğrulama, tekrar koruması referans testi, frame/CRC v2/v3, USB replay, mesh/etiket, ayrıca yeni birleşik API testi).
- `cd backend && python -m pytest -q tests/test_smoke.py`: **2/2** (eski sensör/kalibrasyon ve CSV akışı).
- Birleşik test, izole geçici veritabanlarında v0.6 OG4 CSV sütunlarının importunu, ikinci importta idempotency'yi, solar -1/NULL semantiğini, eski sağlık endpoint'ini, yeni mesh+label/export endpoint'ini ve kalibrasyonu birlikte kontrol eder.
- Dosya bütünlüğü ve ZIP içerik kontrolü, Python `zipfile.testzip`.

**Bu çalışma ortamında tamamlanamayan**
- Yerel PlatformIO kurulu değil; `pip install platformio` ağ/DNS erişimi olmadığı için indirilemedi. Bu nedenle **ESP C++ derlemesi geçmiştir denemez**, `.bin` teslim edilmemiştir.
- npm bağımlılıkları yerel önbellekte eksik; `npm ci --offline` önbellek bulunmadığı için tamamlanamadı. **Frontend npm/TypeScript üretim derlemesi geçmiştir denemez.**
- Gerçek kart, radyo, LoRa, LittleFS bölümleme, OTA, brownout ve 24–72 saat dayanıklılık testi yapılmadı.

Kaynakta `.github/workflows/verify-v060.yml` ile Python/React/PlatformIO derleme testi GitHub Actions'ta çalışabilir; burada çalışmış **gibi** gösterilmemiştir. Workflow derleme sırasında yalnız geçici dummy anahtarlar kullanır, dummy-anahtarlı `.bin` yayımlamaz. Gerçek cihaz yüklemesi öncesinde donanımın flash kapasitesi/pin haritası ve her bir cihaza özel parolalar doğrulanmalıdır.
