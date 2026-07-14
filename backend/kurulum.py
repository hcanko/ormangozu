from models.database import SessionLocal, engine, Base
from models.orm import TowerDB, SensorLogDB

# 1. Eski tabloları tamamen sil (Eski sütun yapılarından kurtulmak için ŞART!)
print("🧹 Eski veritabanı kalıntıları temizleniyor...")
Base.metadata.drop_all(bind=engine)

# 2. Yeni "Zeki" sütunlar ve güncel tablo yapısıyla baştan yarat
print("🛠️ Yeni nesil Zeki Veritabanı oluşturuluyor...")
Base.metadata.create_all(bind=engine)

# 3. Yeni ve güncel IP ile direğimizi ekle
db = SessionLocal()

yeni_direk = TowerDB(
    id="TOWER-001",
    name="Alfa Kulesi",
    lat=38.4237,
    lng=27.1428,
    ip="192.168.1.19", 
    is_online=True,
    bearing=0.0,
    battery_level=100.0,
    sleep_interval=2
)

db.add(yeni_direk)
db.commit()
print("✅ Temiz veritabanı kuruldu ve Alfa Kulesi (192.168.1.19) başarıyla eklendi!")