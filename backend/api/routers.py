import asyncio
import httpx
import time
from datetime import datetime
from fastapi import APIRouter, Depends, HTTPException, WebSocket, WebSocketDisconnect, BackgroundTasks
from sqlalchemy.orm import Session
from pydantic import BaseModel
import math
from services.sensor_fusion import GLOBAL_WEIGHTS

# --- Projeye Özgü Importlar ---
from models.schemas import DashboardResponse, TowerCreate, AlertResponse, SensorDataIncoming
from models.database import get_db
from models.orm import TowerDB, SensorLogDB
from core.algorithms import calculate_intersection, generate_sensor_data, ThermalAnalyzer, GasAnalyzer
from services.sensor_fusion import SensorFusion
from .logger import logger

router = APIRouter()
SIMULATION_ACTIVE = False

# ==========================================
# OTONOM SEVK (WHISPER) YARDIMCI FONKSİYONLARI
# ==========================================
def calculate_distance(lat1, lon1, lat2, lon2):
    """Haversine formülü ile iki GPS koordinatı arasındaki mesafeyi metre cinsinden bulur."""
    R = 6371000  # Dünya yarıçapı (metre)
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlam = math.radians(lon2 - lon1)
    
    a = math.sin(dphi/2)**2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlam/2)**2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
    return R * c # Metre döner

async def execute_whisper_protocol(trigger_id: str, neighbors: list):
    """Arka planda asenkron olarak komşu kulelere UYAN komutu fırlatır ve LOGLAR."""
    mesaj_tetik = f"🚨 SWARM TETİKLENDİ: {trigger_id}, {len(neighbors)} kuleyi uyarıyor"
    print(f"\n{mesaj_tetik}")
    
    # 1. LOG: Swarm kararının verildiği an
    if hasattr(logger, 'is_logging') and logger.is_logging:
        logger.mark_event(mesaj_tetik)
    
    async with httpx.AsyncClient() as client:
        for neighbor in neighbors:
            print(f"📡 [WHISPER] Hedef: {neighbor['id']} (IP: {neighbor['ip']}) -> Uyandırılıyor...")
            try:
                # Komşuya uyanma emrini gönder
                await client.post(f"http://{neighbor['ip']}/sleep?value=500", timeout=3.0)
                
                # 2. LOG: Karşı kule emri başarıyla aldığında
                mesaj_basari = f"✅ SWARM İLETİLDİ: {trigger_id} -> {neighbor['id']} (Hızlı Moda Geçti)"
                print(mesaj_basari)
                if hasattr(logger, 'is_logging') and logger.is_logging:
                    logger.mark_event(mesaj_basari)
                    
            except Exception as e:
                # 3. LOG: Kuleye ulaşılamazsa (Kopukluk durumu)
                mesaj_hata = f"⚠️ SWARM HATA: {neighbor['id']} kulesine ulaşılamadı!"
                print(mesaj_hata)
                if hasattr(logger, 'is_logging') and logger.is_logging:
                    logger.mark_event(mesaj_hata)

# ==========================================
# ZEKA MOTORLARI HAFIZASI (Her direk için ayrı)
# ==========================================
thermal_analyzers = {}
gas_analyzers = {}
fusion_center = SensorFusion()

# Dashboard'un anlık çekeceği canlı veriler
live_tower_data = {}

class CalibrationData(BaseModel):
    weight_delta_t: float
    weight_temp: float
    weight_gas: float

@router.post("/settings/calibration")
async def update_calibration(settings: CalibrationSettings):
    GLOBAL_WEIGHTS["weight_delta_t"] = settings.weight_delta_t
    GLOBAL_WEIGHTS["weight_temp"] = settings.weight_temp
    GLOBAL_WEIGHTS["weight_gas"] = settings.weight_gas
    GLOBAL_WEIGHTS["override_temp"] = settings.override_temp

    total = (
        settings.weight_delta_t +
        settings.weight_temp +
        settings.weight_gas
    )

    return {
        "status": "success",
        "total_weight": total,
        "new_settings": GLOBAL_WEIGHTS
    }

# ==========================================
# WEBSOCKET (CANLI YAYIN YÖNETİCİSİ)
# ==========================================
class ConnectionManager:
    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, message: dict):
        for connection in self.active_connections:
            try:
                await connection.send_json(message)
            except Exception:
                pass

manager = ConnectionManager()

@router.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)


# ==========================================
# TOWER CRUD (KULE YÖNETİMİ)
# ==========================================
@router.post("/towers", response_model=TowerCreate)
def create_tower(tower: TowerCreate, db: Session = Depends(get_db)):
    db_tower = TowerDB(**tower.dict())
    db.add(db_tower)
    db.commit()
    db.refresh(db_tower)
    return db_tower

@router.get("/towers", response_model=list[TowerCreate])
def get_towers(db: Session = Depends(get_db)):
    return db.query(TowerDB).all()

@router.delete("/towers/{tower_id}")
def delete_tower(tower_id: str, db: Session = Depends(get_db)):
    db_tower = db.query(TowerDB).filter(TowerDB.id == tower_id).first()
    if not db_tower:
        raise HTTPException(status_code=404, detail="Kule bulunamadı")
    db.delete(db_tower)
    db.commit()
    return {"message": "Silindi"}

@router.post("/simulation/toggle")
def toggle_simulation():
    global SIMULATION_ACTIVE
    SIMULATION_ACTIVE = not SIMULATION_ACTIVE
    return {"simulation": SIMULATION_ACTIVE}


# ==========================================
# VERİ ALIMI VE SENSÖR FÜZYONU (ANA KALP)
# ==========================================
# 🔥 YENİ: background_tasks eklendi
@router.post("/sensor-data")
async def receive_sensor_data(data: SensorDataIncoming, background_tasks: BackgroundTasks, db: Session = Depends(get_db)):
    tower = db.query(TowerDB).filter(TowerDB.id == data.device_id).first()
    
    # 🔥 YENİ: Gelen paketten IP'yi al (Eğer schemas.py henüz güncellenmediyse hata vermesin diye getattr kullanıyoruz)
    incoming_ip = getattr(data, 'ip', '0.0.0.0')

    if not tower:
        print(f"🌍 [YENİ CİHAZ] {data.device_id} sisteme otomatik kaydediliyor...")
        tower = TowerDB(
            id=data.device_id,
            name=f"Orman Gözü {data.device_id[-4:]}",
            ip=incoming_ip, # 🔥 YENİ: Cihazın canlı IP adresini kaydet
            lat=data.lat,
            lng=data.lng,
            bearing=0.0,
            sleep_interval=5,
            battery_level=data.battery_level,
            is_online=True
        )
        db.add(tower)
        db.commit()
        db.refresh(tower)
    else:
        # 🔥 YENİ: Var olan cihazın IP adresi Wi-Fi üzerinden değiştiyse güncelle
        if incoming_ip != "0.0.0.0":
            tower.ip = incoming_ip

    # 1. Simülasyon Kontrolü
    pixels_to_process = data.pixels
    gas_res_to_process = data.gas_raw_resistance
    if SIMULATION_ACTIVE:
        sim_pixels, sim_gas = generate_sensor_data(True, tower.id)
        pixels_to_process = sim_pixels
        gas_res_to_process = sim_gas

    # 2. Direğe Özel Zeka Motorlarını Başlat/Getir
    if tower.id not in thermal_analyzers:
        thermal_analyzers[tower.id] = ThermalAnalyzer()
        gas_analyzers[tower.id] = GasAnalyzer()

    t_analyzer = thermal_analyzers[tower.id]
    g_analyzer = gas_analyzers[tower.id]

    # 3. MATEMATİK MOTORU (Delta T ve Gas Baseline)
    avg_temp, delta_t = t_analyzer.process_pixels_and_delta_t(pixels_to_process)
    
    # YENİ ASİMETRİK EMA ENTEGRASYONU
    gas_analysis = g_analyzer.process_reading(gas_res_to_process)
    gas_impact = gas_analysis["drop_percentage"]

    # 4. KARAR MOTORU (Ağırlıklı Yangın Skoru)
    fusion_result = fusion_center.calculate_fire_score(avg_temp, delta_t, gas_impact)
    fire_score = fusion_result["fire_score"]
    status = fusion_result["status"]
    
    # ==========================================
    # 🚀 WHISPER PROTOKOLÜ (OTONOM SEVKİYAT)
    # ==========================================
    current_time = time.time()
    last_whisper = live_tower_data.get(tower.id, {}).get("last_whisper", 0)

    if status in ["UYARI", "KRİTİK"] and (current_time - last_whisper) > 60:
        neighbors_to_wake = []
        all_towers = db.query(TowerDB).filter(TowerDB.id != tower.id).all()
        
        for t in all_towers:
            dist = calculate_distance(data.lat, data.lng, t.lat, t.lng)
            if dist <= 3000: 
                neighbors_to_wake.append({"id": t.id, "ip": t.ip})
        
        if neighbors_to_wake:
            background_tasks.add_task(execute_whisper_protocol, tower.id, neighbors_to_wake)
            last_whisper = current_time 

    # ==========================================
    # 5. Dashboard İçin Canlı Veriyi Hafızaya Al
    # ==========================================
    live_tower_data[tower.id] = {
        "avg_temp": avg_temp,
        "delta_t": delta_t,
        "gas_level": gas_res_to_process,
        "gas_ppm_impact": gas_impact,
        "fire_score": fire_score,
        "status": status,
        "pixels": pixels_to_process,
        "battery_level": data.battery_level,
        "battery_mv": data.battery_mv,
        "alert_level": data.alert_level,
        "mlx_ok": data.mlx_ok,
        "gas_ok": data.gas_ok,
        "gps_fix": data.gps_fix,
        "servo_angle": data.servo_angle,
        "uptime_ms": data.uptime_ms,
        "read_counter": data.read_counter,
        "error_counter": data.error_counter,
        "last_update": current_time,
        "last_whisper": last_whisper,
        "raw_max_temp": data.max_temp
    }

    # 6. Donanım Bilgilerini Güncelle
    tower.battery_level = data.battery_level
    tower.is_online = True
    
    # 7. VERİTABANINA LOGLA
    new_log = SensorLogDB(
        tower_id=tower.id,
        timestamp=datetime.utcnow(),
        avg_temp=avg_temp,
        gas_level=gas_res_to_process,
        delta_t=delta_t,
        gas_ppm_impact=gas_impact,
        fire_score=fire_score,
        status=status,
        battery_level=data.battery_level,
        lat=data.lat,
        lng=data.lng,
        battery_mv=data.battery_mv,
        alert_level=data.alert_level,

        mlx_ok=data.mlx_ok,
        gas_ok=data.gas_ok,
        gps_fix=data.gps_fix,

        servo_angle=data.servo_angle,

        uptime_ms=data.uptime_ms,
        read_counter=data.read_counter,
        error_counter=data.error_counter,
    )
    db.add(new_log)
    db.commit()

    if hasattr(logger, 'is_logging') and logger.is_logging:
        logger.log_data(
            tower_id=tower.id,
            max_temp=avg_temp,
            delta_t=delta_t,
            gas_raw_resistance=gas_res_to_process,
            gas_ppm_impact=gas_impact,
            fire_score=fire_score,
            status=status,
            battery=data.battery_level,
            lat=data.lat,
            lng=data.lng
        )

    # 8. WebSocket Yayını
    await manager.broadcast({
        "type": "SENSOR_UPDATE",
        "tower_id": tower.id,
        "fire_score": fire_score,
        "status": status,
        "avg_temp": avg_temp,
        "gas_level": gas_res_to_process,
        "pixels": pixels_to_process,

        "max_temp": data.max_temp,
        "gas_raw_resistance": gas_res_to_process,
        "gas_ppm_impact": gas_impact,
    
        "battery_level": data.battery_level,
        "battery_mv": data.battery_mv,
  
        "alert_level": data.alert_level,

        "mlx_ok": data.mlx_ok,
        "gas_ok": data.gas_ok,
        "gps_fix": data.gps_fix,

        "servo_angle": data.servo_angle,

        "uptime_ms": data.uptime_ms,
        "read_counter": data.read_counter,
        "error_counter": data.error_counter
    })

    if status in ["UYARI", "KRİTİK"]:
        await manager.broadcast({
            "type": "FIRE_ALERT",
            "tower_id": tower.id,
            "status": status,
            "fire_score": fire_score,
            "avg_temp": avg_temp,
            "delta_t": delta_t
        })

    return {"status": "success", "fire_score": fire_score, "decision": status}


# ==========================================
# DASHBOARD ÜRETİCİSİ VE NİRENGİ (TRIANGULATION)
# ==========================================
async def generate_dashboard_data(db: Session):
    towers = db.query(TowerDB).all()
    current_time = time.time()
    
    response_towers = []
    critical_towers = []

    for t in towers:
        live_data = live_tower_data.get(t.id, {})
        last_update = live_data.get("last_update", 0)
        is_online = (current_time - last_update) < 30
        
        if not is_online:
            t.is_online = False
            db.commit()

        status = live_data.get("status", "offline" if not is_online else "NORMAL")
        
        tower_dict = {
            "id": t.id,
            "name": t.name,
            "ip": t.ip,
            "lat": t.lat,
            "lng": t.lng,
            "bearing": t.bearing,
            "status": status,
            "sleep_interval": t.sleep_interval,
            "is_online": is_online,
            "pixels": live_data.get("pixels", []),
            "battery": live_data.get("battery_level", t.battery_level),
            
            # --- HAM VERİLERİ (RAW) REACT'E YOLLUYORUZ ---
            "max_temp": live_data.get("raw_max_temp", 0.0),
            "gas_raw_resistance": live_data.get("gas_level", 0.0),
            
            "currentData": {
                "avg_temp": live_data.get("avg_temp", 0),
                "delta_t": live_data.get("delta_t", 0),
                "gas_level": live_data.get("gas_level", 0),
                
                # React'in beklediği Duman Anomalisi (%) verisini de buraya ekledik:
                "gas_ppm_impact": live_data.get("gas_ppm_impact", 0), 
                
                "fire_score": live_data.get("fire_score", 0),
                "status": status
            }
        }
        response_towers.append(tower_dict)

        if status in ["UYARI", "KRİTİK"] and is_online:
            critical_towers.append(t)

    fire_location = None
    if len(critical_towers) >= 2:
        t1, t2 = critical_towers[0], critical_towers[1]
        intersection = calculate_intersection(t1.lat, t1.lng, t1.bearing, t2.lat, t2.lng, t2.bearing)
        if intersection:
            fire_location = {"lat": intersection["lat"], "lng": intersection["lng"]}

    return {
        "towers": response_towers,
        "fireLocation": fire_location,
        "active_alerts": [] 
    }

@router.get("/towers/live", response_model=DashboardResponse)
async def get_towers_live(db: Session = Depends(get_db)):
    return await generate_dashboard_data(db)


# ==========================================
# LOGGER KONTROLLERİ VE CİHAZ KOMUTLARI
# ==========================================
@router.get("/logger/status")
def get_logger_status():
    return {"is_logging": getattr(logger, 'is_logging', False)}

@router.post("/logger/toggle")
def toggle_logger():
    status = logger.toggle() if hasattr(logger, 'toggle') else False
    return {"is_logging": status}

class CheckpointRequest(BaseModel):
    note: str

@router.post("/logger/checkpoint")
def mark_checkpoint(req: CheckpointRequest, db: Session = Depends(get_db)):
    if hasattr(logger, 'mark_event'):
        success = logger.mark_event(req.note)
        if success:
            return {"status": "ok"}
    raise HTTPException(status_code=400, detail="Kayıt aktif değil.")

@router.post("/towers/{tower_id}/command")
async def send_command_to_tower(tower_id: str, command: str, value: int, db: Session = Depends(get_db)):
    tower = db.query(TowerDB).filter(TowerDB.id == tower_id).first()
    if not tower: raise HTTPException(status_code=404, detail="Direk bulunamadı.")
    
    try:
        async with httpx.AsyncClient() as client:
            res = await client.post(f"http://{tower.ip}/{command}?value={value}", timeout=5.0)
            return {"status": "success", "response": res.text}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/alerts")
def get_recent_alerts(limit: int = 50, db: Session = Depends(get_db)):
    alerts = db.query(SensorLogDB).filter(
        SensorLogDB.status.in_(["UYARI", "KRİTİK"])
    ).order_by(SensorLogDB.timestamp.desc()).limit(limit).all()
    return alerts

@router.get("/towers/{tower_id}/history")
def get_tower_history(tower_id: str, limit: int = 30, db: Session = Depends(get_db)):
    logs = db.query(SensorLogDB).filter(
        SensorLogDB.tower_id == tower_id
    ).order_by(SensorLogDB.timestamp.desc()).limit(limit).all()
    
    history_data = []
    for log in reversed(logs):
        history_data.append({
            "time": log.timestamp.strftime("%H:%M:%S"),
            "temp": log.avg_temp,
            "gas": log.gas_level,
            "fire_score": log.fire_score
        })
    return history_data

class CalibrationSettings(BaseModel):
    weight_delta_t: float
    weight_temp: float
    weight_gas: float
    override_temp: float 

