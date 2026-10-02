import math
from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator


class TowerCreate(BaseModel):
    id: str = Field(min_length=1, max_length=64)
    name: str = Field(min_length=1, max_length=120)
    ip: str = "0.0.0.0"
    lat: float = Field(ge=-90, le=90)
    lng: float = Field(ge=-180, le=180)
    bearing: float = Field(default=0.0, ge=0, lt=360)
    sleep_interval: int = Field(default=300, ge=1, le=86400)

    model_config = ConfigDict(from_attributes=True)


class ThermalData(BaseModel):
    avg_temp: float
    delta_t: float
    gas_level: float
    gas_ppm_impact: float
    fire_score: float
    status: str


class Tower(BaseModel):
    id: str
    name: str
    ip: str
    lat: float
    lng: float
    bearing: float
    status: str = "offline"
    sleep_interval: int = 300
    true_bearing: float | None = None
    battery: float = 100.0
    is_online: bool = False
    pixels: list[float] = Field(default_factory=list)
    max_temp: float = 0.0
    gas_raw_resistance: float = 0.0
    currentData: ThermalData | None = None
    fire_score: float = 0.0
    last_update: datetime | None = None

    model_config = ConfigDict(from_attributes=True)


class SensorDataIncoming(BaseModel):
    protocol_version: int = Field(default=1, ge=1, le=100)
    device_id: str = Field(min_length=1, max_length=64)
    boot_id: str | None = Field(default=None, max_length=64)
    sequence: int | None = Field(default=None, ge=0)
    firmware_version: str | None = Field(default=None, max_length=64)
    device_timestamp: int | None = Field(default=None, ge=0)
    ip: str = "0.0.0.0"

    max_temp: float = Field(ge=-100, le=1000)
    gas_raw_resistance: float = Field(ge=0)
    battery_level: float = Field(ge=0, le=100)
    battery_mv: float = Field(default=0.0, ge=0, le=100000)
    solar_voltage_mv: float | None = Field(default=None, ge=0, le=100000)
    solar_current_ma: float | None = Field(default=None, ge=-100000, le=100000)
    charge_state: str | None = Field(default=None, max_length=40)

    lat: float = Field(ge=-90, le=90)
    lng: float = Field(ge=-180, le=180)
    pixels: list[float] = Field(min_length=768, max_length=768)

    alert_level: int = Field(default=0, ge=0, le=4)
    mlx_ok: bool = True
    gas_ok: bool = True
    gps_fix: bool = False
    servo_angle: int = Field(default=0, ge=-360, le=360)
    compass_bearing: float | None = Field(default=None, ge=0, lt=360)

    uptime_ms: int = Field(default=0, ge=0)
    read_counter: int = Field(default=0, ge=0)
    error_counter: int = Field(default=0, ge=0)
    reset_reason: str | None = Field(default=None, max_length=80)
    free_heap: int | None = Field(default=None, ge=0)
    wifi_rssi: int | None = Field(default=None, ge=-150, le=20)

    top5_temp: float | None = None
    local_delta_t: float | None = None
    gas_ema: float | None = Field(default=None, ge=0)
    gas_drop_pct: float | None = Field(default=None, ge=0, le=100)
    local_fire_score: float | None = Field(default=None, ge=0, le=100)
    local_status: str | None = Field(default=None, max_length=24)

    @field_validator("pixels")
    @classmethod
    def validate_pixels(cls, pixels: list[float]) -> list[float]:
        if any(not math.isfinite(value) for value in pixels):
            raise ValueError("pixels must contain only finite values")
        return pixels

    @field_validator(
        "max_temp", "gas_raw_resistance", "battery_level", "battery_mv",
        "lat", "lng", "top5_temp", "local_delta_t", "gas_ema",
        "gas_drop_pct", "local_fire_score", mode="before"
    )
    @classmethod
    def validate_finite_numbers(cls, value: Any) -> Any:
        if value is not None and isinstance(value, (int, float)) and not math.isfinite(value):
            raise ValueError("numeric values must be finite")
        return value


class CalibrationSettings(BaseModel):
    weight_delta_t: float = Field(ge=0, le=1)
    weight_temp: float = Field(ge=0, le=1)
    weight_gas: float = Field(ge=0, le=1)
    override_temp: float = Field(ge=40, le=300)

    @model_validator(mode="after")
    def require_nonzero_total(self) -> "CalibrationSettings":
        if self.weight_delta_t + self.weight_temp + self.weight_gas <= 0:
            raise ValueError("at least one weight must be greater than zero")
        return self


class AlertResponse(BaseModel):
    id: int
    tower_id: str
    timestamp: datetime
    avg_temp: float | None = None
    max_temp_raw: float | None = None
    delta_t: float | None = None
    gas_level: float | None = None
    fire_score: float | None = None
    status: str | None = None
    lat: float | None = None
    lng: float | None = None

    model_config = ConfigDict(from_attributes=True)


class FireLocation(BaseModel):
    lat: float
    lng: float
    confidence_score: float = 0.0


class DashboardResponse(BaseModel):
    towers: list[Tower]
    active_alerts: list[AlertResponse] = Field(default_factory=list)
    fireLocation: FireLocation | None = None


class CheckpointRequest(BaseModel):
    note: str = Field(min_length=1, max_length=500)


class TestEventCreate(BaseModel):
    timestamp: datetime | None = None
    event_type: Literal[
        "NOTE", "BASELINE", "FIRE_IGNITION", "SMOKE_VISIBLE",
        "FIRE_EXTINGUISHED", "RAIN", "POWER_INTERRUPTION", "MAINTENANCE"
    ] = "NOTE"
    note: str = Field(min_length=1, max_length=1000)
    tower_id: str | None = Field(default=None, max_length=64)
    distance_m: float | None = Field(default=None, ge=0)
    fire_size: str | None = Field(default=None, max_length=120)
    wind_direction: float | None = Field(default=None, ge=0, lt=360)
    wind_speed: float | None = Field(default=None, ge=0)


class TestEventResponse(TestEventCreate):
    id: int
    timestamp: datetime
    model_config = ConfigDict(from_attributes=True)


class OfflineImportResult(BaseModel):
    filename: str
    imported_rows: int
    skipped_duplicates: int
    imported_events: int
    rejected_rows: int
    errors: list[str] = Field(default_factory=list)
