"""Device registry and zero-touch USB provisioning endpoints.

The collector credential may register a physically attached, already-flashed
Nest. Operator credentials remain required for lifecycle and installation
changes. GNSS/PTZ/backhaul capabilities describe what is currently enabled,
not what may merely be present on an unassembled prototype.
"""
from __future__ import annotations

from datetime import UTC, datetime
import json
from typing import Any

from fastapi import APIRouter, Depends, Header, HTTPException
from sqlalchemy.orm import Session

from api.mesh_events import auth, operator_auth
from models.database import get_db
from models.orm import TowerDB
from models.schemas import DeviceRegistration, DeviceUpdate

router = APIRouter(prefix="/devices", tags=["Device registry"])


def utcnow() -> datetime:
    return datetime.now(UTC).replace(tzinfo=None)


def _json_object(value: str | None) -> dict[str, Any]:
    try:
        parsed = json.loads(value or "{}")
        return parsed if isinstance(parsed, dict) else {}
    except (TypeError, ValueError):
        return {}


def device_dict(row: TowerDB) -> dict[str, Any]:
    return {
        "device_id": row.id,
        "name": row.name,
        "product_model": row.product_model,
        "network_role": row.network_role,
        "lifecycle_state": row.lifecycle_state,
        "location_status": row.location_status,
        "lat": row.lat,
        "lng": row.lng,
        "bearing": row.bearing,
        "hardware_revision": row.hardware_revision,
        "firmware_version": row.firmware_version,
        "capabilities": _json_object(row.capabilities_json),
        "self_test": _json_object(row.self_test_json),
        "backhaul": row.backhaul,
        "primary_hub_id": row.primary_hub_id,
        "secondary_hub_id": row.secondary_hub_id,
        "provisioned_at": row.provisioned_at,
        "last_seen_at": row.last_seen_at,
        "is_online": row.is_online,
    }


def collector_guard(x_client_token: str | None = Header(default=None)) -> None:
    auth(x_client_token)


def operator_guard(x_client_token: str | None = Header(default=None)) -> None:
    operator_auth(x_client_token)


@router.post("/register", dependencies=[Depends(collector_guard)])
def register_device(body: DeviceRegistration, db: Session = Depends(get_db)) -> dict[str, Any]:
    """Idempotently register a USB-attached Nest after firmware installation."""
    row = db.query(TowerDB).filter(TowerDB.id == body.device_id).first()
    created = row is None
    if row is not None and row.lifecycle_state == "REVOKED":
        raise HTTPException(status_code=403, detail="Revoked device cannot re-register")

    now = utcnow()
    test_passed = body.self_test.passed(body.capabilities)
    next_state = (
        "READY_FOR_INSTALLATION" if test_passed is True
        else "TEST_FAILED" if test_passed is False
        else "DISCOVERED"
    )
    if row is None:
        label = body.product_model.replace("_", " ").title()
        row = TowerDB(
            id=body.device_id,
            name=f"{label} {body.device_id[-4:]}",
            ip="0.0.0.0",
            lat=0.0,
            lng=0.0,
            bearing=0.0,
            sleep_interval=300,
            battery_level=100.0,
            is_online=False,
            lifecycle_state=next_state,
            location_status="PENDING",
        )
        db.add(row)
    elif test_passed is not None and row.lifecycle_state in {
        "DISCOVERED", "READY_FOR_INSTALLATION", "TEST_FAILED"
    }:
        row.lifecycle_state = next_state

    row.product_model = body.product_model
    row.network_role = body.network_role
    row.hardware_revision = body.hardware_revision
    row.firmware_version = body.firmware_version
    row.capabilities_json = body.capabilities.model_dump_json()
    row.self_test_json = body.self_test.model_dump_json()
    row.backhaul = body.backhaul
    row.last_seen_at = now
    if test_passed is True and row.provisioned_at is None:
        row.provisioned_at = now

    db.commit()
    db.refresh(row)
    return {"created": created, **device_dict(row)}


@router.get("", dependencies=[Depends(operator_guard)])
def list_devices(db: Session = Depends(get_db)) -> list[dict[str, Any]]:
    return [device_dict(row) for row in db.query(TowerDB).order_by(TowerDB.id.asc()).all()]


@router.patch("/{device_id}", dependencies=[Depends(operator_guard)])
def update_device(device_id: str, body: DeviceUpdate, db: Session = Depends(get_db)) -> dict[str, Any]:
    row = db.query(TowerDB).filter(TowerDB.id == device_id).first()
    if row is None:
        raise HTTPException(status_code=404, detail="Cihaz bulunamadı")

    changes = body.model_dump(exclude_unset=True)
    resulting_primary = changes.get("primary_hub_id", row.primary_hub_id)
    resulting_secondary = changes.get("secondary_hub_id", row.secondary_hub_id)
    if resulting_primary is not None and resulting_primary == resulting_secondary:
        raise HTTPException(status_code=422, detail="Primary and secondary Hub must be different")
    for hub_id in (resulting_primary, resulting_secondary):
        if hub_id is None:
            continue
        if hub_id == device_id:
            raise HTTPException(status_code=422, detail="A device cannot use itself as a Hub")
        hub = db.query(TowerDB).filter(TowerDB.id == hub_id).first()
        if hub is None or hub.network_role != "HUB":
            raise HTTPException(status_code=422, detail="primary_hub_id must reference a registered HUB")

    for field, value in changes.items():
        setattr(row, field, value)
    if "lat" in changes and "location_status" not in changes:
        row.location_status = "MANUAL" if (row.lat != 0.0 or row.lng != 0.0) else "PENDING"

    db.commit()
    db.refresh(row)
    return device_dict(row)
