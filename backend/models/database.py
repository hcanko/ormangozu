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
    if "sensor_logs" not in inspector.get_table_names():
        return

    existing = {column["name"] for column in inspector.get_columns("sensor_logs")}
    additions = {
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
        "device_status": "VARCHAR",
        "reset_reason": "VARCHAR",
        "free_heap": "INTEGER",
        "wifi_rssi": "INTEGER",
        "solar_voltage_mv": "FLOAT",
        "solar_current_ma": "FLOAT",
        "charge_state": "VARCHAR",
    }

    with engine.begin() as connection:
        for name, sql_type in additions.items():
            if name not in existing:
                connection.execute(text(f"ALTER TABLE sensor_logs ADD COLUMN {name} {sql_type}"))

        connection.execute(
            text(
                "CREATE INDEX IF NOT EXISTS ix_sensor_logs_packet_identity "
                "ON sensor_logs (tower_id, boot_id, sequence)"
            )
        )
