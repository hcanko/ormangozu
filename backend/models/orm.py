from sqlalchemy import Column, String, Float, Integer, Boolean, DateTime
from datetime import datetime
from models.database import Base

class TowerDB(Base):
    """
    Kulelerin/Direklerin sabit bilgilerini ve anlık 
    çevrimiçi durumlarını tutan tablo.
    """
    __tablename__ = "towers"

    id = Column(String, primary_key=True, index=True)
    name = Column(String)
    ip = Column(String)
    lat = Column(Float)
    lng = Column(Float)
    bearing = Column(Float) # Kulenin baktığı sabit yön açısı
    sleep_interval = Column(Integer, default=2)
    
    # --- Donanım Sağlığı Sütunları ---
    battery_level = Column(Float, default=100.0)
    is_online = Column(Boolean, default=False)

class SensorLogDB(Base):
    """
    Sistemden akan tüm verilerin tarihsel olarak 
    kaydedildiği, geleceğe yönelik log tablosu.
    """
    __tablename__ = "sensor_logs"

    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    tower_id = Column(String, index=True)
    timestamp = Column(DateTime, default=datetime.utcnow)
    
    # --- Sensör Ham Verileri ---
    avg_temp = Column(Float)     # Filtrelenmiş ortalama sıcaklık
    gas_level = Column(Float)    # BME680 Ham Direnç Değeri
    
    # --- Orman Gözü Zeka Analiz Verileri ---
    delta_t = Column(Float)      # Sıcaklık Artış Hızı (C/sn)
    gas_ppm_impact = Column(Float) # Gaz Anomalisi Yüzdesi (%)
    fire_score = Column(Float)   # Nihai Yangın Skoru (0-100)
    status = Column(String)      # NORMAL, UYARI, KRITIK
    
    # --- Konum ve Enerji Durumu ---
    battery_level = Column(Float)
    lat = Column(Float)
    lng = Column(Float)
    
    # --- Gelecek Rezervasyonları (Data Logging & Telemetry) ---
    # Bu alanlar şu an boş kalabilir ancak veritabanında yerleri hazır.
    compass_bearing = Column(Float, nullable=True) # Manyetik pusula açısı
    wind_speed = Column(Float, nullable=True)      # Rüzgar hızı

    battery_mv = Column(Float, nullable=True)

    alert_level = Column(Integer, default=0)

    mlx_ok = Column(Boolean, default=True)
    gas_ok = Column(Boolean, default=True)
    gps_fix = Column(Boolean, default=False)

    servo_angle = Column(Integer, nullable=True)

    uptime_ms = Column(Integer, nullable=True)
    read_counter = Column(Integer, nullable=True)
    error_counter = Column(Integer, nullable=True)