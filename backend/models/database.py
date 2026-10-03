import os
from collections.abc import Generator

from sqlalchemy import create_engine, inspect, text
from sqlalchemy.orm import declarative_base, sessionmaker, Session

SQLALCHEMY_DATABASE_URL = os.getenv("DATABASE_URL", "sqlite:///./ormangozu.db")

connect_args = {"check_same_thread": False} if SQLALCHEMY_DATABASE_URL.startswith("sqlite") else {}
engine = create_engine(SQLALCHEMY_DATABASE_URL, connect_args=connect_args, pool_pre_ping=True)

SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)
Base = declarative_base()


def get_db() -> Generator[Session, None, None]:
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def migrate_sqlite_schema() -> None:
    """Prototype-safe additive migration for existing SQLite databases.

    SQLAlchemy's create_all() does not add columns to an existing table. This helper
    only adds nullable columns used by Pilot v0.1; destructive changes still require
    a real migration tool (Alembic) later.
    """
    if not SQLALCHEMY_DATABASE_URL.startswith("sqlite"):
        return

    inspector = inspect(engine)
    tables = set(inspector.get_table_names())

    sensor_additions = {
        "received_at": "DATETIME",
        "source": "VARCHAR DEFAULT 'online'",
        "protocol_version": "INTEGER",
        "boot_id": "VARCHAR",
        "sequence": "INTEGER",
        "firmware_version": "VARCHAR",
        "device_timestamp": "INTEGER",
        "max_temp_raw": "FLOAT",
        "top5_temp": "FLOAT",
        "device_delta_t": "FLOAT",
        "gas_ema": "FLOAT",
        "device_gas_drop_pct": "FLOAT",
        "device_fire_score": "FLOAT",
        "sample_trigger": "VARCHAR",
        "ambient_temp": "FLOAT",
        "hotspot_threshold": "FLOAT",
        "largest_hotspot_cluster": "INTEGER",
        "persistence_count": "INTEGER",
        "fire_level": "INTEGER",
        "health_level": "INTEGER",
        "network_confirmed": "BOOLEAN",
        "device_status": "VARCHAR",
        "reset_reason": "VARCHAR",
        "free_heap": "INTEGER",
        "wifi_rssi": "INTEGER",
        "solar_voltage_mv": "FLOAT",
        "solar_current_ma": "FLOAT",
        "charge_state": "VARCHAR",
    }

    tower_additions = {
        "product_model": "VARCHAR NOT NULL DEFAULT 'MINI_NEST'",
        "network_role": "VARCHAR NOT NULL DEFAULT 'NODE'",
        "lifecycle_state": "VARCHAR NOT NULL DEFAULT 'DISCOVERED'",
        "location_status": "VARCHAR NOT NULL DEFAULT 'PENDING'",
        "hardware_revision": "VARCHAR",
        "capabilities_json": "TEXT NOT NULL DEFAULT '{}'",
        "self_test_json": "TEXT NOT NULL DEFAULT '{}'",
        "backhaul": "VARCHAR NOT NULL DEFAULT 'NONE'",
        "primary_hub_id": "VARCHAR",
        "secondary_hub_id": "VARCHAR",
        "firmware_version": "VARCHAR",
        "provisioned_at": "DATETIME",
        "last_seen_at": "DATETIME",
    }

    with engine.begin() as connection:
        if "sensor_logs" in tables:
            existing = {column["name"] for column in inspector.get_columns("sensor_logs")}
            for name, sql_type in sensor_additions.items():
                if name not in existing:
                    connection.execute(text(f"ALTER TABLE sensor_logs ADD COLUMN {name} {sql_type}"))

        if "towers" in tables:
            existing_towers = {column["name"] for column in inspector.get_columns("towers")}
            for name, sql_type in tower_additions.items():
                if name not in existing_towers:
                    connection.execute(text(f"ALTER TABLE towers ADD COLUMN {name} {sql_type}"))
            connection.execute(
                text("CREATE INDEX IF NOT EXISTS ix_towers_primary_hub_id ON towers (primary_hub_id)")
            )
            connection.execute(
                text("CREATE INDEX IF NOT EXISTS ix_towers_secondary_hub_id ON towers (secondary_hub_id)")
            )

        if "sensor_logs" in tables:
            connection.execute(
                text(
                    "CREATE INDEX IF NOT EXISTS ix_sensor_logs_packet_identity "
                    "ON sensor_logs (tower_id, boot_id, sequence)"
                )
            )
