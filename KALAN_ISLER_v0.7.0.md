# v0.7.0 sonrası çalışma ve kabul kapıları

**Kod yazıldı ≠ saha/üretim onayı.** Bu belge yapılması gerekenlerin tamamını gösterir.

## Gerekli doğrulama ve donanım hazırlığı

- [ ] Gerçek kart revizyonu (Heltec V3/V4), RF SX1262 pinleri, 8MB/16MB flash, 868MHz anten/bağlantı, servo güç ve motor sürücüsünü belgeleyin.
- [ ] Offline P-256 imza çifti, gerçek iki ayrı HMAC anahtarı, gerçek `OG_CONTROLLER_ID` provision edin; dummy CI anahtarı kullanmayın. Her Nest için benzersiz kimlikleri kaydedin.
- [ ] `pio run -e heltec_wifi_lora_32_V3` (gerekirse kart revizyonuna uyarlayın) ve `npm ci && npm run build` tamamen başarılı olsun. Bilgisayar/Node/Python temiz kurulum testi.
- [ ] İki Nest önce USB ile aynı protokol v0.7.0'a geçsin; normal OG4 Whisper `ALERT -> ACK -> REPORT`, bağımsız alarm ve manuel `STATUS/SAMPLE/FAST/AUTO` hedeflemesi fiziken geçsin.
- [ ] Araya C eklenirse `PC -> A -> C -> B` komutu/cevabı ve yalıtılmış B'ye erişim deneyin; paket kaybı, RF engelleri, yanlış hedef/kimlik, tekrar mesaj, röle loop denemeleri ve LoRa airtime ölçün.
- [ ] LittleFS offline CSV ve termal kare rotasyonunu, 8MB OTA rezervini, uzun süreli güç/sıcaklık döngüsü, watchdog/brownout, NVS sayaç kalıcılığını test edin.
- [ ] İmzalı OTA: hedef doğru/imza doğru; yanlış anahtar/imza/size/SHA/build/ID; paket kaybı; rastgele reset ile `Q` kaldığı offset; E/X arasında yeniden başlatma; kesilen güç; alarm/batarya nedeniyle reddetme; başarılı boot/rollback/fault injection.
- [ ] Gerçek 96 byte bloklardaki LoRa SF/BW/CR ayarlarına, güç ve Türkiye'deki uygulanabilir alt bant/LBT/AFA/yayın süresine uygun aktarım takvimi hazırlayın. 1 MB'ın uzun sürmesini net olarak saha paydaşlarıyla paylaşın; alarm önceliği test edin.
- [ ] Bakım AP'si yalnız fiziksel MAINT ON, yerel OTA da imza gerektiriyor mu, farklı operatör/collector token yetkileri ve eski HTTP endpointlerin engellenmesi testi.
- [ ] 6–12h bench -> 24h -> 72h dayanıklılık -> izinli güvenli 2–4h açık alan -> 1 aylık offline pilot. Gerçek yangın yakma önerilmez; güvenli kontrol kaynakları ve gerekli izinler kullanın.

## Ürüne/gerçek uzak orman ağına geçişte testin ÖTESİNDE kalanlar

- [ ] Gerçek radyo kapsama GIS/anten/irtifa ölçümü; USB radyo köprüsü ormandaki uçlara ulaşmıyorsa uygun yerde uplink/gateway veya mevcut Nest rölesi; kontrol bilgisayarı ile bu köprü arasında gerçek ağ bağlantısı.
- [ ] Uzaktan OTA transfer optimizasyonu (seçmeli blok ACK, kısa diff/model/config güncelleme, daha etkin duty-cycle planlama). v0.7.0 full OTA uzun süren bir pilot altyapısıdır.
- [ ] Sahaya bırakılacak ürün için per-device cryptographic provisioning, anahtar iptali/değişimi, eFuse Secure Boot + flash/NVS encryption, imzalı kurtarma yolu, fiziksel tamper/audit ve bağımsız pentest.
- [ ] Dinamik mesh rota keşfi/health heartbeat; v0.7.0 yalnız sınırlandırılmış flood-forward yapar. Her topografyada ve kaç Nest olursa olsun kapsama sağlamaz.
- [ ] Termal algılama kalibrasyonu, yanlış pozitif/negatif analizi, normal dönemin veri seti, insan doğrulama etiketleri ve sonrasında ayrılmış eğitim/test setiyle AI modeli.
- [ ] Servo/Pan-Tilt gerçekten kurulacaksa mekanik limit, akım, engel ve enerji testi. Varsayılan firmware'de aktüatör kapalıdır.
