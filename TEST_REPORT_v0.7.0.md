# Orman Gözü v0.7.0 doğrulama raporu

## Mevcut ortamda yapılmış kontroller

- `python -m compileall -q backend tools tests` — geçti.
- `python -m pytest -q tests backend/tests` — **22/22 geçti**. Kapsam: OG4 ve OGC2 framing/replay/bounded relay hop, signed manifest keygen/verify/manipülasyon reddi, 96-byte sınırı, RF airtime referansı, backend birleşik olay/API, offline replay, operator/collector token izolasyonu, eski backend smoke.
- Source ve ZIP girişleri ayrıca ZIP oluşturulduğunda CRC/sha256 ile doğrulanır.

## YAPILMADI / BELİRSİZ

- PlatformIO gerçek C++/ESP32 derlemesi: bu çalıştırma ortamı bağımlılık indirmek için internete bağlanamadı (`pio` yerleşik değil). Sadece CI tarifi: `.github/workflows/verify-v070.yml`.
- `frontend/npm ci && npm run build`: node_modules yok, bağımlılıklar bu ortamda indirilmedi; CI tarifi mevcut.
- Gerçek SX1262 iki Nest / 1-2 röle / seri port / OTA / elektrik kesintisi / eFuse / rollback / pil/enerji / algılama / menzil **yapılmadı**. Python testleri bunların yerine geçmez.
- RF yayın aralığının mevzuata uygunluğu ve gerçek OTA tamamlanma süresi doğrulanmadı. `%1` üzerinden yapılan RF hesabı yalnız muhafazakâr örnektir.
- Bağımsız pentest, fiziksel saldırı / private key compromise / üretim güvenli boot incelemesi yapılmadı. Pilot HMAC grup anahtarı üretim güvenliğine eşdeğer değildir.

## Çıkış kararı

**v0.7.0 kaynak adayı / iki-Nest laboratuvar testine hazır inceleme paketi. Sahaya yüklemeye onaylı binary veya siber güvenliği sertifikalı nihai ürün DEĞİLDİR.** `KALAN_ISLER_v0.7.0.md` kapılarından önce üretim/pilot sahasına bırakmayın.
