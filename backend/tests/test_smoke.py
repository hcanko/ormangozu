import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

DB_PATH = Path(__file__).with_name("test_pilot.db")
if DB_PATH.exists():
    DB_PATH.unlink()
os.environ["DATABASE_URL"] = f"sqlite:///{DB_PATH}"
os.environ["ENABLE_HTTP_WHISPER"] = "0"

from fastapi.testclient import TestClient
from main import app


def sample_payload(sequence: int = 1):
    pixels = [25.0] * 768
    pixels[-5:] = [40.0, 41.0, 42.0, 43.0, 44.0]
    return {
        "protocol_version": 1,
        "device_id": "TOWER-TEST",
        "boot_id": "BOOT-1",
        "sequence": sequence,
        "firmware_version": "0.1.0",
        "max_temp": 44.0,
        "gas_raw_resistance": 100000.0,
        "battery_level": 80,
        "battery_mv": 3950,
        "lat": 38.4237,
        "lng": 27.1428,
        "pixels": pixels,
        "mlx_ok": True,
        "gas_ok": True,
    }


def test_health_and_sensor_flow():
    with TestClient(app) as client:
        assert client.get("/api/health").status_code == 200

        response = client.post("/api/sensor-data", json=sample_payload())
        assert response.status_code == 200, response.text
        assert response.json()["status"] == "success"

        duplicate = client.post("/api/sensor-data", json=sample_payload())
        assert duplicate.status_code == 200
        assert duplicate.json()["status"] == "duplicate"

        dashboard = client.get("/api/towers/live")
        assert dashboard.status_code == 200, dashboard.text
        assert dashboard.json()["towers"][0]["id"] == "TOWER-TEST"

        settings = client.post(
            "/api/settings/calibration",
            json={
                "weight_delta_t": 4,
                "weight_temp": 3,
                "weight_gas": 3,
                "override_temp": 90,
            },
        )
        # Values are constrained to 0..1; use valid non-normalized values next.
        assert settings.status_code == 422
        settings = client.post(
            "/api/settings/calibration",
            json={
                "weight_delta_t": 0.8,
                "weight_temp": 0.6,
                "weight_gas": 0.6,
                "override_temp": 90,
            },
        )
        assert settings.status_code == 200, settings.text
        assert abs(settings.json()["total_weight"] - 1.0) < 1e-6


def test_csv_import():
    csv_data = (
        "timestamp,device_id,boot_id,sequence,max_temp,top5_temp,gas_raw_resistance,"
        "gas_drop_pct,fire_score,status,battery_mv,lat,lng\n"
        "2026-07-18T12:00:00,TOWER-OFFLINE,B1,1,50,46,90000,5,22,NORMAL,3900,38.4,27.1\n"
    )
    with TestClient(app) as client:
        response = client.post(
            "/api/import/device-log",
            files={"file": ("log.csv", csv_data, "text/csv")},
        )
        assert response.status_code == 200, response.text
        assert response.json()["imported_rows"] == 1
