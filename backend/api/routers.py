import csv
import io
import json
import math
import os
import time
from datetime import UTC, datetime
from typing import Any

import httpx
from fastapi import (
    APIRouter,
    BackgroundTasks,
    Depends,
    File,
    HTTPException,
    Header,
    Query,
    UploadFile,
    WebSocket,
    WebSocketDisconnect,
)
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from api.logger import logger
from api.mesh_events import auth as collector_auth, operator_auth
from core.algorithms import GasAnalyzer, ThermalAnalyzer, calculate_intersection, generate_sensor_data
from models.database import get_db
from models.orm import SensorLogDB, SystemSettingsDB, TestEventDB, TowerDB
from models.schemas import (
    AlertResponse,
    CalibrationSettings,
    CheckpointRequest,
    DashboardResponse,
    OfflineImportResult,
    SensorDataIncoming,
    TestEventCreate,
    TestEventResponse,
    TowerCreate,
)
from services.sensor_fusion import GLOBAL_WEIGHTS, SensorFusion, apply_calibration


def utcnow() -> datetime:
    return datetime.now(UTC).replace(tzinfo=None)


def _json_object(value: str | None) -> dict[str, Any]:
    try:
        parsed = json.loads(value or "{}")
        return parsed if isinstance(parsed, dict) else {}
    except (TypeError, ValueError):
        return {}

def operator_guard(x_client_token: str | None = Header(default=None)) -> None:
    operator_auth(x_client_token)


def legacy_sensor_guard(x_client_token: str | None = Header(default=None)) -> None:
    # Old HTTP device upload is disabled in the offline LoRa pilot, and must
    # never accept unauthenticated sensor data if explicitly enabled in a lab.
    if os.getenv("OG_ENABLE_LEGACY_SENSOR_HTTP", "0") != "1":
        raise HTTPException(status_code=403, detail="Legacy HTTP sensor ingest disabled")
    collector_auth(x_client_token)


router = APIRouter()
SIMULATION_ACTIVE = False
ENABLE_HTTP_WHISPER = os.getenv("ENABLE_HTTP_WHISPER", "0") == "1"
ONLINE_TIMEOUT_SECONDS = int(os.getenv("ONLINE_TIMEOUT_SECONDS", "30"))

thermal_analyzers: dict[str, ThermalAnalyzer] = {}
gas_analyzers: dict[str, GasAnalyzer] = {}
fusion_center = SensorFusion()
live_tower_data: dict[str, dict[str, Any]] = {}


def calculate_distance(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    radius_m = 6_371_000.0
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlam = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlam / 2) ** 2
    return radius_m * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))


async def execute_whisper_protocol(trigger_id: str, neighbors: list[dict[str, str]]) -> None:
    message = f"SWARM TETİKLENDİ: {trigger_id}, {len(neighbors)} kuleyi uyarıyor"
    print(message)
    logger.mark_event(message)

    async with httpx.AsyncClient() as client:
        for neighbor in neighbors:
            ip = neighbor.get("ip", "0.0.0.0")
            if not ip or ip == "0.0.0.0":
                continue
            try:
                response = await client.post(f"http://{ip}/sleep?value=500", timeout=3.0)
                response.raise_for_status()
                logger.mark_event(f"SWARM İLETİLDİ: {trigger_id} -> {neighbor['id']}")
            except Exception as exc:
                print(f"Whisper error for {neighbor['id']}: {exc}")
                logger.mark_event(f"SWARM HATA: {neighbor['id']} kulesine ulaşılamadı")


class ConnectionManager:
    def __init__(self) -> None:
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket) -> None:
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast(self, message: dict[str, Any]) -> None:
        dead: list[WebSocket] = []
        for connection in list(self.active_connections):
            try:
                await connection.send_json(message)
            except Exception:
                dead.append(connection)
        for connection in dead:
            self.disconnect(connection)


manager = ConnectionManager()


@router.get("/health")
def health() -> dict[str, Any]:
    return {
        "status": "ok",
        "simulation": SIMULATION_ACTIVE,
        "websocket_clients": len(manager.active_connections),
        "calibration": dict(GLOBAL_WEIGHTS),
    }


@router.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        manager.disconnect(websocket)


@router.get("/settings/calibration")
def get_calibration() -> dict[str, float]:
    return dict(GLOBAL_WEIGHTS)


@router.post("/settings/calibration", dependencies=[Depends(operator_guard)])
def update_calibration(settings: CalibrationSettings, db: Session = Depends(get_db)) -> dict[str, Any]:
    normalized = apply_calibration(settings.model_dump())
    row = db.query(SystemSettingsDB).filter(SystemSettingsDB.id == 1).first()
    if row is None:
        row = SystemSettingsDB(id=1)
        db.add(row)

    row.weight_delta_t = normalized["weight_delta_t"]
    row.weight_temp = normalized["weight_temp"]
    row.weight_gas = normalized["weight_gas"]
    row.override_temp = normalized["override_temp"]
    row.updated_at = utcnow()
    db.commit()

    return {
        "status": "success",
        "total_weight": round(
            normalized["weight_delta_t"] + normalized["weight_temp"] + normalized["weight_gas"], 6
        ),
        "new_settings": normalized,
    }


@router.post("/towers", response_model=TowerCreate, status_code=201, dependencies=[Depends(operator_guard)])
def create_tower(tower: TowerCreate, db: Session = Depends(get_db)):
    db_tower = TowerDB(**tower.model_dump())
    db.add(db_tower)
    try:
        db.commit()
    except IntegrityError as exc:
        db.rollback()
        raise HTTPException(status_code=409, detail="Bu kule kimliği zaten kayıtlı") from exc
    db.refresh(db_tower)
    return db_tower


@router.get("/towers", response_model=list[TowerCreate])
def get_towers(db: Session = Depends(get_db)):
    return db.query(TowerDB).order_by(TowerDB.id.asc()).all()


@router.delete("/towers/{tower_id}", dependencies=[Depends(operator_guard)])
def delete_tower(tower_id: str, db: Session = Depends(get_db)) -> dict[str, str]:
    tower = db.query(TowerDB).filter(TowerDB.id == tower_id).first()
    if tower is None:
        raise HTTPException(status_code=404, detail="Kule bulunamadı")
    db.delete(tower)
    db.commit()
    thermal_analyzers.pop(tower_id, None)
    gas_analyzers.pop(tower_id, None)
    live_tower_data.pop(tower_id, None)
    return {"message": "Silindi"}


@router.post("/simulation/toggle", dependencies=[Depends(operator_guard)])
def toggle_simulation() -> dict[str, bool]:
    global SIMULATION_ACTIVE
    SIMULATION_ACTIVE = not SIMULATION_ACTIVE
    return {"simulation": SIMULATION_ACTIVE}


def _packet_is_duplicate(db: Session, data: SensorDataIncoming) -> bool:
    if data.boot_id is None or data.sequence is None:
        return False
    return (
        db.query(SensorLogDB.id)
        .filter(
            SensorLogDB.tower_id == data.device_id,
            SensorLogDB.boot_id == data.boot_id,
            SensorLogDB.sequence == data.sequence,
        )
        .first()
        is not None
    )


@router.post("/sensor-data", dependencies=[Depends(legacy_sensor_guard)])
async def receive_sensor_data(
    data: SensorDataIncoming,
    background_tasks: BackgroundTasks,
    db: Session = Depends(get_db),
) -> dict[str, Any]:
    if _packet_is_duplicate(db, data):
        return {"status": "duplicate", "device_id": data.device_id, "sequence": data.sequence}

    tower = db.query(TowerDB).filter(TowerDB.id == data.device_id).first()
    if tower is None:
        tower = TowerDB(
            id=data.device_id,
            name=f"Orman Gözü {data.device_id[-4:]}",
            ip=data.ip,
            lat=data.lat,
            lng=data.lng,
            bearing=0.0,
            sleep_interval=300,
            battery_level=data.battery_level,
            is_online=True,
            lifecycle_state="ACTIVE",
            location_status="GNSS_FIXED" if data.gps_fix else "PENDING",
            firmware_version=data.firmware_version,
            last_seen_at=utcnow(),
        )
        db.add(tower)
        db.flush()
    else:
        if data.ip != "0.0.0.0":
            tower.ip = data.ip
        tower.lat = data.lat
        tower.lng = data.lng
        tower.firmware_version = data.firmware_version or tower.firmware_version
        tower.last_seen_at = utcnow()
        if data.gps_fix:
            tower.location_status = "GNSS_FIXED"

    pixels = data.pixels
    gas_resistance = data.gas_raw_resistance
    if SIMULATION_ACTIVE:
        pixels, gas_resistance = generate_sensor_data(True, tower.id)

    thermal_analyzer = thermal_analyzers.setdefault(tower.id, ThermalAnalyzer())
    gas_analyzer = gas_analyzers.setdefault(tower.id, GasAnalyzer())

    server_top5_temp, server_delta_t = thermal_analyzer.process_pixels_and_delta_t(pixels)
    gas_analysis = gas_analyzer.process_reading(gas_resistance)
    server_gas_drop = gas_analysis["drop_percentage"]
    fusion = fusion_center.calculate_fire_score(server_top5_temp, server_delta_t, server_gas_drop)
    fire_score = fusion["fire_score"]
    status = fusion["status"]

    now_epoch = time.time()
    last_whisper = live_tower_data.get(tower.id, {}).get("last_whisper", 0.0)
    if ENABLE_HTTP_WHISPER and status in {"UYARI", "KRİTİK"} and now_epoch - last_whisper > 60:
        neighbors = []
        for other in db.query(TowerDB).filter(TowerDB.id != tower.id).all():
            if other.ip and other.ip != "0.0.0.0" and calculate_distance(data.lat, data.lng, other.lat, other.lng) <= 3000:
                neighbors.append({"id": other.id, "ip": other.ip})
        if neighbors:
            background_tasks.add_task(execute_whisper_protocol, tower.id, neighbors)
            last_whisper = now_epoch

    live_tower_data[tower.id] = {
        "avg_temp": server_top5_temp,
        "delta_t": server_delta_t,
        "gas_level": gas_resistance,
        "gas_ppm_impact": server_gas_drop,
        "fire_score": fire_score,
        "status": status,
        "pixels": pixels,
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
        "last_update": now_epoch,
        "last_whisper": last_whisper,
        "raw_max_temp": data.max_temp,
        "device_fire_score": data.local_fire_score,
        "device_status": data.local_status,
        "health_level": data.health_level,
        "network_confirmed": data.network_confirmed,
    }

    tower.battery_level = data.battery_level
    tower.is_online = True

    log = SensorLogDB(
        tower_id=tower.id,
        timestamp=utcnow(),
        received_at=utcnow(),
        source="online",
        protocol_version=data.protocol_version,
        boot_id=data.boot_id,
        sequence=data.sequence,
        firmware_version=data.firmware_version,
        device_timestamp=data.device_timestamp,
        max_temp_raw=data.max_temp,
        avg_temp=server_top5_temp,
        top5_temp=data.top5_temp if data.top5_temp is not None else server_top5_temp,
        gas_level=gas_resistance,
        delta_t=server_delta_t,
        gas_ppm_impact=server_gas_drop,
        fire_score=fire_score,
        status=status,
        device_delta_t=data.local_delta_t,
        gas_ema=data.gas_ema,
        device_gas_drop_pct=data.gas_drop_pct,
        ambient_temp=data.ambient_temp,
        hotspot_threshold=data.hotspot_threshold,
        largest_hotspot_cluster=data.largest_hotspot_cluster,
        persistence_count=data.persistence_count,
        fire_level=data.fire_level,
        health_level=data.health_level,
        network_confirmed=data.network_confirmed,
        device_fire_score=data.local_fire_score,
        device_status=data.local_status,
        battery_level=data.battery_level,
        battery_mv=data.battery_mv,
        solar_voltage_mv=data.solar_voltage_mv,
        solar_current_ma=data.solar_current_ma,
        charge_state=data.charge_state,
        lat=data.lat,
        lng=data.lng,
        compass_bearing=data.compass_bearing,
        alert_level=data.alert_level,
        mlx_ok=data.mlx_ok,
        gas_ok=data.gas_ok,
        gps_fix=data.gps_fix,
        servo_angle=data.servo_angle,
        uptime_ms=data.uptime_ms,
        read_counter=data.read_counter,
        error_counter=data.error_counter,
        reset_reason=data.reset_reason,
        free_heap=data.free_heap,
        wifi_rssi=data.wifi_rssi,
    )
    db.add(log)
    db.commit()

    logger.log_data(
        tower_id=tower.id,
        max_temp=server_top5_temp,
        delta_t=server_delta_t,
        gas_raw_resistance=gas_resistance,
        gas_ppm_impact=server_gas_drop,
        fire_score=fire_score,
        status=status,
        battery=data.battery_level,
        lat=data.lat,
        lng=data.lng,
        compass_bearing=data.compass_bearing,
    )

    update_message = {
        "type": "SENSOR_UPDATE",
        "tower_id": tower.id,
        "fire_score": fire_score,
        "status": status,
        "avg_temp": server_top5_temp,
        "delta_t": server_delta_t,
        "gas_level": gas_resistance,
        "gas_ppm_impact": server_gas_drop,
        "pixels": pixels,
        "max_temp": data.max_temp,
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
        "device_fire_score": data.local_fire_score,
        "device_status": data.local_status,
        "health_level": data.health_level,
        "network_confirmed": data.network_confirmed,
    }
    await manager.broadcast(update_message)

    if status in {"UYARI", "KRİTİK"}:
        await manager.broadcast({
            "type": "FIRE_ALERT",
            "tower_id": tower.id,
            "status": status,
            "fire_score": fire_score,
            "avg_temp": server_top5_temp,
            "delta_t": server_delta_t,
        })

    return {
        "status": "success",
        "fire_score": fire_score,
        "decision": status,
        "server_analysis": fusion,
        "device_analysis": {
            "fire_score": data.local_fire_score,
            "status": data.local_status,
            "delta_t": data.local_delta_t,
            "gas_drop_pct": data.gas_drop_pct,
        },
    }


async def generate_dashboard_data(db: Session) -> dict[str, Any]:
    towers = db.query(TowerDB).all()
    now = time.time()
    response_towers: list[dict[str, Any]] = []
    critical_towers: list[TowerDB] = []
    changed = False

    for tower in towers:
        live = live_tower_data.get(tower.id, {})
        last_update = float(live.get("last_update", 0.0))
        is_online = now - last_update < ONLINE_TIMEOUT_SECONDS
        if tower.is_online != is_online:
            tower.is_online = is_online
            changed = True

        status = live.get("status", "offline" if not is_online else "NORMAL")
        response_towers.append({
            "id": tower.id,
            "name": tower.name,
            "ip": tower.ip,
            "lat": tower.lat,
            "lng": tower.lng,
            "bearing": tower.bearing,
            "status": status,
            "sleep_interval": tower.sleep_interval,
            "is_online": is_online,
            "pixels": live.get("pixels", []),
            "battery": live.get("battery_level", tower.battery_level),
            "max_temp": live.get("raw_max_temp", 0.0),
            "gas_raw_resistance": live.get("gas_level", 0.0),
            "fire_score": live.get("fire_score", 0.0),
            "product_model": tower.product_model or "MINI_NEST",
            "network_role": tower.network_role or "NODE",
            "lifecycle_state": tower.lifecycle_state or "DISCOVERED",
            "location_status": tower.location_status or "PENDING",
            "hardware_revision": tower.hardware_revision,
            "capabilities": _json_object(tower.capabilities_json),
            "self_test": _json_object(tower.self_test_json),
            "backhaul": tower.backhaul or "NONE",
            "primary_hub_id": tower.primary_hub_id,
            "secondary_hub_id": tower.secondary_hub_id,
            "firmware_version": tower.firmware_version,
            "provisioned_at": tower.provisioned_at,
            "last_seen_at": tower.last_seen_at,
            "currentData": {
                "avg_temp": live.get("avg_temp", 0.0),
                "delta_t": live.get("delta_t", 0.0),
                "gas_level": live.get("gas_level", 0.0),
                "gas_ppm_impact": live.get("gas_ppm_impact", 0.0),
                "fire_score": live.get("fire_score", 0.0),
                "status": status,
            },
        })
        if status in {"UYARI", "KRİTİK"} and is_online:
            critical_towers.append(tower)

    if changed:
        db.commit()

    fire_location = None
    if len(critical_towers) >= 2:
        first, second = critical_towers[:2]
        intersection = calculate_intersection(
            first.lat, first.lng, first.bearing, second.lat, second.lng, second.bearing
        )
        if intersection:
            fire_location = {**intersection, "confidence_score": 0.5}

    return {"towers": response_towers, "fireLocation": fire_location, "active_alerts": []}


@router.get("/towers/live", response_model=DashboardResponse)
async def get_towers_live(db: Session = Depends(get_db)):
    return await generate_dashboard_data(db)


@router.get("/logger/status")
def get_logger_status() -> dict[str, bool]:
    return {"is_logging": logger.is_logging}


@router.post("/logger/toggle", dependencies=[Depends(operator_guard)])
def toggle_logger() -> dict[str, bool]:
    return {"is_logging": logger.toggle()}


@router.post("/logger/checkpoint", dependencies=[Depends(operator_guard)])
def mark_checkpoint(req: CheckpointRequest) -> dict[str, str]:
    if not logger.mark_event(req.note):
        raise HTTPException(status_code=400, detail="Kayıt aktif değil")
    return {"status": "ok"}


@router.post("/towers/{tower_id}/command", deprecated=True)
def deprecated_ip_command(tower_id: str) -> dict[str, str]:
    # The legacy endpoint accepted arbitrary, unauthenticated HTTP paths and
    # forwarded them to a stored device IP. It MUST NOT be used for v0.6.1.
    # Keep a clear migration response for older dashboard clients, but do not
    # send any command or mutate equipment.
    del tower_id
    raise HTTPException(status_code=410,
                        detail="Legacy IP commands disabled. Use authenticated /api/control/commands")


@router.get("/alerts", response_model=list[AlertResponse])
def get_recent_alerts(
    limit: int = Query(default=50, ge=1, le=500), db: Session = Depends(get_db)
):
    return (
        db.query(SensorLogDB)
        .filter(SensorLogDB.status.in_(["UYARI", "KRİTİK"]))
        .order_by(SensorLogDB.timestamp.desc())
        .limit(limit)
        .all()
    )


@router.get("/towers/{tower_id}/history")
def get_tower_history(
    tower_id: str, limit: int = Query(default=30, ge=1, le=5000), db: Session = Depends(get_db)
) -> list[dict[str, Any]]:
    logs = (
        db.query(SensorLogDB)
        .filter(SensorLogDB.tower_id == tower_id)
        .order_by(SensorLogDB.timestamp.desc())
        .limit(limit)
        .all()
    )
    return [
        {
            "time": log.timestamp.isoformat() if log.timestamp else None,
            "temp": log.avg_temp,
            "max_temp": log.max_temp_raw,
            "gas": log.gas_level,
            "gas_drop_pct": log.gas_ppm_impact,
            "fire_score": log.fire_score,
            "device_fire_score": log.device_fire_score,
            "battery_mv": log.battery_mv,
        }
        for log in reversed(logs)
    ]


@router.post("/test-events", response_model=TestEventResponse, status_code=201, dependencies=[Depends(operator_guard)])
def create_test_event(event: TestEventCreate, db: Session = Depends(get_db)):
    row = TestEventDB(**event.model_dump(exclude={"timestamp"}), timestamp=event.timestamp or utcnow())
    db.add(row)
    db.commit()
    db.refresh(row)
    logger.mark_event(f"{row.event_type}: {row.note}")
    return row


@router.get("/test-events", response_model=list[TestEventResponse])
def list_test_events(
    limit: int = Query(default=200, ge=1, le=5000), db: Session = Depends(get_db)
):
    return db.query(TestEventDB).order_by(TestEventDB.timestamp.desc()).limit(limit).all()


def _first(row: dict[str, str], *names: str) -> str | None:
    for name in names:
        value = row.get(name)
        if value is not None and str(value).strip() != "":
            return str(value).strip()
    return None


def _to_float(value: str | None, default: float | None = None) -> float | None:
    if value is None:
        return default
    return float(value.replace(",", "."))


def _sensor_optional(value: str | None) -> float | None:
    """Nest sentinel -1 means unavailable, not a measured solar value."""
    v = _to_float(value)
    return None if v == -1 else v


def _to_int(value: str | None, default: int | None = None) -> int | None:
    if value is None:
        return default
    return int(float(value))


def _parse_datetime(value: str | None) -> datetime:
    if not value:
        return utcnow()
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00")).replace(tzinfo=None)
    except ValueError:
        for pattern in ("%Y-%m-%d %H:%M:%S", "%H:%M:%S.%f", "%H:%M:%S"):
            try:
                parsed = datetime.strptime(value, pattern)
                if parsed.year == 1900:
                    now = utcnow()
                    parsed = parsed.replace(year=now.year, month=now.month, day=now.day)
                return parsed
            except ValueError:
                continue
    return utcnow()


@router.post("/import/device-log", response_model=OfflineImportResult, dependencies=[Depends(operator_guard)])
async def import_device_log(file: UploadFile = File(...), db: Session = Depends(get_db)):
    if not file.filename or not file.filename.lower().endswith(".csv"):
        raise HTTPException(status_code=400, detail="CSV dosyası yükleyin")

    raw = await file.read()
    if len(raw) > 50 * 1024 * 1024:
        raise HTTPException(status_code=413, detail="Dosya 50 MB sınırını aşıyor")

    try:
        text = raw.decode("utf-8-sig")
    except UnicodeDecodeError as exc:
        raise HTTPException(status_code=400, detail="CSV UTF-8 kodlamasında olmalı") from exc

    reader = csv.DictReader(io.StringIO(text))
    imported = duplicates = events = rejected = 0
    errors: list[str] = []

    for line_number, row in enumerate(reader, start=2):
        try:
            event_note = _first(row, "Olay_Notu", "event_note", "note")
            device_id = _first(row, "device_id", "tower_id", "Direk_ID")
            if device_id == "SISTEM" or (event_note and not device_id):
                db.add(TestEventDB(
                    timestamp=_parse_datetime(_first(row, "timestamp", "Zaman")),
                    event_type="NOTE",
                    note=event_note or "İçe aktarılan olay",
                ))
                events += 1
                continue

            if not device_id:
                raise ValueError("device_id/tower_id eksik")

            boot_id = _first(row, "boot_id")
            sequence = _to_int(_first(row, "sequence", "read_counter"))
            if boot_id is not None and sequence is not None:
                exists = db.query(SensorLogDB.id).filter(
                    SensorLogDB.tower_id == device_id,
                    SensorLogDB.boot_id == boot_id,
                    SensorLogDB.sequence == sequence,
                ).first()
                if exists:
                    duplicates += 1
                    continue

            lat = _to_float(_first(row, "lat", "Enlem"), 0.0)
            lng = _to_float(_first(row, "lng", "Boylam"), 0.0)
            tower = db.query(TowerDB).filter(TowerDB.id == device_id).first()
            if tower is None:
                tower = TowerDB(
                    id=device_id,
                    name=f"Orman Gözü {device_id[-4:]}",
                    ip="0.0.0.0",
                    lat=lat or 0.0,
                    lng=lng or 0.0,
                    bearing=0.0,
                    sleep_interval=300,
                    is_online=False,
                    lifecycle_state="DISCOVERED",
                    location_status="PENDING" if not lat and not lng else "MANUAL",
                    firmware_version=_first(row, "firmware_version"),
                    last_seen_at=utcnow(),
                )
                db.add(tower)

            top5 = _to_float(_first(row, "top5_temp", "avg_temp", "Max_Sicaklik"))
            max_raw = _to_float(_first(row, "max_temp", "max_temp_raw", "Max_Sicaklik"), top5)
            gas = _to_float(_first(row, "gas_raw_resistance", "gas_raw", "Ham_Gaz_Ohm"), 0.0)
            server_delta = _to_float(_first(row, "delta_t", "Delta_T"), 0.0)
            gas_drop = _to_float(_first(row, "gas_drop_pct", "gas_ppm_impact", "Gaz_PPM_Etkisi"), 0.0)
            score = _to_float(_first(row, "fire_score", "local_fire_score", "Yangin_Skoru"), 0.0)
            status = _first(row, "status", "fire_status", "Durum") or "NORMAL"
            timestamp = _parse_datetime(_first(row, "timestamp", "received_at", "Zaman"))

            db.add(SensorLogDB(
                tower_id=device_id,
                timestamp=timestamp,
                received_at=utcnow(),
                source="offline_import",
                protocol_version=_to_int(_first(row, "protocol_version"), 1),
                boot_id=boot_id,
                sequence=sequence,
                firmware_version=_first(row, "firmware_version"),
                device_timestamp=_to_int(_first(row, "device_timestamp", "uptime_ms")),
                max_temp_raw=max_raw,
                avg_temp=top5,
                top5_temp=top5,
                gas_level=gas,
                delta_t=server_delta,
                gas_ppm_impact=gas_drop,
                fire_score=score,
                status=status,
                device_delta_t=_to_float(_first(row, "local_delta_t", "device_delta_t"), server_delta),
                gas_ema=_to_float(_first(row, "gas_ema")),
                device_gas_drop_pct=_to_float(_first(row, "device_gas_drop_pct"), gas_drop),
                device_fire_score=_to_float(_first(row, "local_fire_score", "device_fire_score"), score),
                device_status=_first(row, "local_status", "device_status", "fire_status") or status,
                sample_trigger=_first(row, "trigger"),
                ambient_temp=_to_float(_first(row, "ambient_temp")),
                hotspot_threshold=_to_float(_first(row, "hotspot_threshold")),
                largest_hotspot_cluster=_to_int(_first(row, "largest_hotspot_cluster")),
                persistence_count=_to_int(_first(row, "persistence_count")),
                fire_level=_to_int(_first(row, "fire_level")),
                health_level=_to_int(_first(row, "health_level")),
                network_confirmed=(_first(row, "network_confirmed") or "false").lower() in {"1", "true", "yes"},
                battery_level=_to_float(_first(row, "battery_level", "battery_pct", "Pil")),
                battery_mv=_to_float(_first(row, "battery_mv")),
                solar_voltage_mv=_sensor_optional(_first(row, "solar_voltage_mv")),
                solar_current_ma=_sensor_optional(_first(row, "solar_current_ma")),
                charge_state=_first(row, "charge_state"),
                lat=lat,
                lng=lng,
                compass_bearing=_to_float(_first(row, "compass_bearing", "Pusula")),
                wind_speed=_to_float(_first(row, "wind_speed", "Ruzgar_Hizi")),
                alert_level=_to_int(_first(row, "alert_level"), 0),
                mlx_ok=(_first(row, "mlx_ok") or "true").lower() in {"1", "true", "yes"},
                gas_ok=(_first(row, "gas_ok") or "true").lower() in {"1", "true", "yes"},
                gps_fix=(_first(row, "gps_fix") or "false").lower() in {"1", "true", "yes"},
                servo_angle=_to_int(_first(row, "servo_angle")),
                uptime_ms=_to_int(_first(row, "uptime_ms")),
                read_counter=_to_int(_first(row, "read_counter")),
                error_counter=_to_int(_first(row, "error_counter")),
                reset_reason=_first(row, "reset_reason"),
                free_heap=_to_int(_first(row, "free_heap")),
                wifi_rssi=_to_int(_first(row, "wifi_rssi")),
            ))
            imported += 1
        except Exception as exc:
            rejected += 1
            if len(errors) < 50:
                errors.append(f"Satır {line_number}: {exc}")

    db.commit()
    return OfflineImportResult(
        filename=file.filename,
        imported_rows=imported,
        skipped_duplicates=duplicates,
        imported_events=events,
        rejected_rows=rejected,
        errors=errors,
    )
