import math
from datetime import datetime
from typing import Any, Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator


ProductModel = Literal["MINI_NEST", "NEST_VISION", "NEST_INDUSTRIAL", "NEST_HUB", "NEST_RELAY"]
NetworkRole = Literal["NODE", "HUB", "RELAY", "BASE"]
LifecycleState = Literal[
    "DISCOVERED", "READY_FOR_INSTALLATION", "LOCATION_PENDING", "ACTIVE",
    "TEST_FAILED", "MAINTENANCE", "DISABLED", "REVOKED",
]
LocationStatus = Literal["PENDING", "GNSS_ACQUIRING", "GNSS_FIXED", "MANUAL", "UNAVAILABLE"]
BackhaulType = Literal["NONE", "LORA_LONG_HAUL", "LTE", "SATELLITE", "ETHERNET", "WIFI"]


class DeviceCapabilities(BaseModel):
    thermal: bool = True
    environmental: bool = True
    smoke: bool = False
    optical: bool = False
    ptz: bool = False
    gnss: bool = False
    solar: bool = False
    battery: bool = True
    relay: bool = True
    lte: bool = False
    satellite: bool = False


class DeviceSelfTest(BaseModel):
    storage: bool | None = None
    lora: bool | None = None
    thermal: bool | None = None
    environmental: bool | None = None

    def passed(self, capabilities: DeviceCapabilities | None = None) -> bool | None:
        values = [self.storage, self.lora]
        if capabilities is None or capabilities.thermal:
            values.append(self.thermal)
        if capabilities is None or capabilities.environmental:
            values.append(self.environmental)
        if all(value is None for value in values):
            return None
        return all(value is True for value in values)


class TowerCreate(BaseModel):
    id: str = Field(min_length=1, max_length=64)
    name: str = Field(min_length=1, max_length=120)
    ip: str = "0.0.0.0"
    lat: float = Field(default=0.0, ge=-90, le=90)
    lng: float = Field(default=0.0, ge=-180, le=180)
    bearing: float = Field(default=0.0, ge=0, lt=360)
    sleep_interval: int = Field(default=300, ge=1, le=86400)
    product_model: ProductModel = "MINI_NEST"
    network_role: NetworkRole = "NODE"
    lifecycle_state: LifecycleState = "DISCOVERED"
    location_status: LocationStatus = "PENDING"
    hardware_revision: str | None = Field(default=None, max_length=64)
    capabilities_json: str = "{}"
    self_test_json: str = "{}"
    backhaul: BackhaulType = "NONE"
    primary_hub_id: str | None = Field(default=None, max_length=64)
    secondary_hub_id: str | None = Field(default=None, max_length=64)
    firmware_version: str | None = Field(default=None, max_length=64)

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
    product_model: ProductModel = "MINI_NEST"
    network_role: NetworkRole = "NODE"
    lifecycle_state: LifecycleState = "DISCOVERED"
    location_status: LocationStatus = "PENDING"
    hardware_revision: str | None = None
    capabilities: DeviceCapabilities = Field(default_factory=DeviceCapabilities)
    self_test: DeviceSelfTest = Field(default_factory=DeviceSelfTest)
    backhaul: BackhaulType = "NONE"
    primary_hub_id: str | None = None
    secondary_hub_id: str | None = None
    firmware_version: str | None = None
    provisioned_at: datetime | None = None
    last_seen_at: datetime | None = None

    model_config = ConfigDict(from_attributes=True)


class DeviceRegistration(BaseModel):
    device_id: str = Field(pattern=r"^NEST-[0-9A-F]{12}$")
    firmware_version: str = Field(min_length=1, max_length=64)
    hardware_revision: str | None = Field(default=None, max_length=64)
    product_model: ProductModel = "MINI_NEST"
    network_role: NetworkRole = "NODE"
    capabilities: DeviceCapabilities = Field(default_factory=DeviceCapabilities)
    self_test: DeviceSelfTest = Field(default_factory=DeviceSelfTest)
    backhaul: BackhaulType = "NONE"


class DeviceUpdate(BaseModel):
    name: str | None = Field(default=None, min_length=1, max_length=120)
    lifecycle_state: LifecycleState | None = None
    location_status: LocationStatus | None = None
    lat: float | None = Field(default=None, ge=-90, le=90)
    lng: float | None = Field(default=None, ge=-180, le=180)
    bearing: float | None = Field(default=None, ge=0, lt=360)
    primary_hub_id: str | None = Field(default=None, max_length=64)
    secondary_hub_id: str | None = Field(default=None, max_length=64)

    @model_validator(mode="after")
    def require_coordinate_pair(self) -> "DeviceUpdate":
        if (self.lat is None) != (self.lng is None):
            raise ValueError("lat and lng must be provided together")
        return self


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
    ambient_temp: float | None = None
    hotspot_threshold: float | None = None
    largest_hotspot_cluster: int | None = Field(default=None, ge=0)
    persistence_count: int | None = Field(default=None, ge=0)
    fire_level: int | None = Field(default=None, ge=0, le=4)
    health_level: int | None = Field(default=None, ge=0, le=2)
    network_confirmed: bool | None = None

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
