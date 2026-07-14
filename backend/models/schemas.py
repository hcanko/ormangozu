from pydantic import BaseModel
from typing import List, Optional
from datetime import datetime

# ==========================================
# 1. CIHAZ VE KULE YÖNETİMİ ŞEMALARI
# ==========================================

class TowerCreate(BaseModel):
    """Sisteme yeni bir kule/direk kaydı yapılırken kullanılır."""
    id: str
    name: str
    ip: str
    lat: float
    lng: float
    bearing: float  # Kulenin baktığı ana yön açısı
    sleep_interval: int = 2 

# schemas.py içindeki Tower sınıfını bul ve şu satırı ekle:

class Tower(BaseModel):
    id: str
    name: str
    ip: str
    lat: float
    lng: float
    bearing: float
    status: str = "offline"
    sleep_interval: int = 2
    true_bearing: Optional[float] = None
    battery: float = 100.0
    is_online: bool = False
    pixels: Optional[List[float]] = []
    
    # --- YENİ EKLENEN HAM VERİ SATIRLARI ---
    max_temp: Optional[float] = 0.0
    gas_raw_resistance: Optional[float] = 0.0
    
    currentData: Optional[dict] = None  
    fire_score: float = 0.0
    last_update: Optional[datetime] = None

    class Config:
        orm_mode = True


# ==========================================
# 2. VERİ TRANSFER VE ANALİZ ŞEMALARI
# ==========================================

class SensorDataIncoming(BaseModel):
    device_id: str
    ip: str = "0.0.0.0"

    max_temp: float
    gas_raw_resistance: float
    battery_level: int
    battery_mv: float = 0.0

    lat: float
    lng: float

    pixels: List[float]

    alert_level: int = 0

    mlx_ok: bool = True
    gas_ok: bool = True
    gps_fix: bool = False

    servo_angle: int = 0

    uptime_ms: int = 0
    read_counter: int = 0
    error_counter: int = 0

class ThermalData(BaseModel):
    """Kulelerin anlık sensör durumunu temsil eder."""
    avg_temp: float
    delta_t: float           # Sıcaklık artış hızı
    gas_level: float        # Ham direnç değeri
    gas_ppm_impact: float   # Gaz anomalisi yüzdesi
    fire_score: float       # Hesaplanan yangın skoru
    status: str             # NORMAL, UYARI, KRITIK

# ==========================================
# 3. ALARM VE DASHBOARD ŞEMALARI
# ==========================================

class AlertResponse(BaseModel):
    """Yangın veya kritik durum oluştuğunda fırlatılan alarm paketi."""
    id: int
    tower_id: str
    timestamp: datetime
    max_temp: float
    delta_t: float
    gas_level: float
    fire_score: float
    status: str
    # Konum bilgisi (Alarmın geldiği nokta)
    lat: float
    lng: float

    class Config:
        orm_mode = True

class FireLocation(BaseModel):
    """Nirengi sonucu hesaplanan yangın koordinatı."""
    lat: float
    lng: float
    confidence_score: float = 0.0 # Tahmin güven oranı

class DashboardResponse(BaseModel):
    """React Dashboard'un tek bir seferde tüm harita durumunu almasını sağlar."""
    towers: List[Tower]
    active_alerts: List[AlertResponse]
    fireLocation: Optional[FireLocation] = None

# ==========================================
# 4. GELECEK REZERVASYONLARI (LOGLAMA İÇİN)
# ==========================================

class TelemetryLogSchema(BaseModel):
    """Gelecekte eklenecek rüzgar ve pusula verilerini de kapsayan geniş log şeması."""
    timestamp: datetime
    device_id: str
    max_temp: float
    delta_t: float
    gas_raw_resistance: float
    fire_score: float
    battery: float
    lat: float
    lng: float
    # Opsiyonel Alanlar (Gelecek Güncellemesi)
    compass_bearing: Optional[float] = None
    wind_speed: Optional[float] = None

    class Config:
        orm_mode = True