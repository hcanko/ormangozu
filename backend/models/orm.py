from datetime import UTC, datetime


def utcnow() -> datetime:
    return datetime.now(UTC).replace(tzinfo=None)

from sqlalchemy import Boolean, Column, DateTime, Float, Integer, String, Text

from models.database import Base


class TowerDB(Base):
    __tablename__ = "towers"

    id = Column(String, primary_key=True, index=True)
    name = Column(String, nullable=False)
    ip = Column(String, default="0.0.0.0")
    lat = Column(Float, nullable=False)
    lng = Column(Float, nullable=False)
    bearing = Column(Float, default=0.0)
    sleep_interval = Column(Integer, default=300)
    battery_level = Column(Float, default=100.0)
    is_online = Column(Boolean, default=False)


class SensorLogDB(Base):
    __tablename__ = "sensor_logs"

    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    tower_id = Column(String, index=True, nullable=False)
    timestamp = Column(DateTime, default=utcnow, index=True)
    received_at = Column(DateTime, default=utcnow)
    source = Column(String, default="online")

    protocol_version = Column(Integer, nullable=True)
    boot_id = Column(String, nullable=True)
    sequence = Column(Integer, nullable=True)
    firmware_version = Column(String, nullable=True)
    device_timestamp = Column(Integer, nullable=True)

    max_temp_raw = Column(Float, nullable=True)
    avg_temp = Column(Float, nullable=True)
    top5_temp = Column(Float, nullable=True)
    gas_level = Column(Float, nullable=True)

    delta_t = Column(Float, nullable=True)
    gas_ppm_impact = Column(Float, nullable=True)
    fire_score = Column(Float, nullable=True)
    status = Column(String, nullable=True)

    device_delta_t = Column(Float, nullable=True)
    gas_ema = Column(Float, nullable=True)
    device_gas_drop_pct = Column(Float, nullable=True)
    device_fire_score = Column(Float, nullable=True)
    device_status = Column(String, nullable=True)

    battery_level = Column(Float, nullable=True)
    battery_mv = Column(Float, nullable=True)
    solar_voltage_mv = Column(Float, nullable=True)
    solar_current_ma = Column(Float, nullable=True)
    charge_state = Column(String, nullable=True)

    lat = Column(Float, nullable=True)
    lng = Column(Float, nullable=True)
    compass_bearing = Column(Float, nullable=True)
    wind_speed = Column(Float, nullable=True)

    alert_level = Column(Integer, default=0)
    mlx_ok = Column(Boolean, default=True)
    gas_ok = Column(Boolean, default=True)
    gps_fix = Column(Boolean, default=False)
    servo_angle = Column(Integer, nullable=True)

    uptime_ms = Column(Integer, nullable=True)
    read_counter = Column(Integer, nullable=True)
    error_counter = Column(Integer, nullable=True)
    reset_reason = Column(String, nullable=True)
    free_heap = Column(Integer, nullable=True)
    wifi_rssi = Column(Integer, nullable=True)


class SystemSettingsDB(Base):
    __tablename__ = "system_settings"

    id = Column(Integer, primary_key=True, default=1)
    weight_delta_t = Column(Float, default=0.40)
    weight_temp = Column(Float, default=0.30)
    weight_gas = Column(Float, default=0.30)
    override_temp = Column(Float, default=90.0)
    updated_at = Column(DateTime, default=utcnow, onupdate=utcnow)


class TestEventDB(Base):
    __tablename__ = "test_events"

    id = Column(Integer, primary_key=True, index=True, autoincrement=True)
    timestamp = Column(DateTime, default=utcnow, index=True)
    event_type = Column(String, default="NOTE", index=True)
    note = Column(Text, nullable=False)
    tower_id = Column(String, nullable=True, index=True)
    distance_m = Column(Float, nullable=True)
    fire_size = Column(String, nullable=True)
    wind_direction = Column(Float, nullable=True)
    wind_speed = Column(Float, nullable=True)
