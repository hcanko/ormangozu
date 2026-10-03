"""Local PC ingest for ground-truth-ready pilot records.

Do not expose this pilot API directly on the public internet. The X-Client-Token
is a development credential; all user/admin routes need a later auth audit.
"""
from __future__ import annotations
import csv
from contextlib import contextmanager
from datetime import datetime, timezone
import hmac
import io
import json
import os
import sqlite3
from pathlib import Path
from typing import Any, Literal, Iterator

from fastapi import APIRouter, Header, HTTPException, Query
from fastapi.responses import Response
from pydantic import BaseModel, Field, ConfigDict

router = APIRouter(prefix="/mesh", tags=["Pilot Mesh Events"])
DB_PATH = Path(os.environ.get("OG_PILOT_DB", "pilot_events.sqlite3"))


def auth(x_client_token: str | None) -> None:
    expected = os.environ.get("OG_CLIENT_TOKEN", "")
    if not expected or not x_client_token or not hmac.compare_digest(expected, x_client_token):
        raise HTTPException(status_code=401, detail="X-Client-Token required")


def operator_auth(x_client_token: str | None) -> None:
    """Operator and USB-collector credentials are independent."""
    expected = os.environ.get("OG_OPERATOR_TOKEN", "")
    if not expected or not x_client_token or not hmac.compare_digest(expected, x_client_token):
        raise HTTPException(status_code=401, detail="Operator credential required")


@contextmanager
def connection() -> Iterator[sqlite3.Connection]:
    c = sqlite3.connect(DB_PATH, timeout=5)
    c.execute("PRAGMA busy_timeout=5000")
    c.execute("PRAGMA journal_mode=WAL")
    c.executescript("""
    CREATE TABLE IF NOT EXISTS pilot_event (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      received_at TEXT NOT NULL, record_kind TEXT NOT NULL,
      device_id TEXT NOT NULL, event_type TEXT,
      event_key TEXT UNIQUE NOT NULL, payload_json TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS control_command (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      created_at TEXT NOT NULL, updated_at TEXT NOT NULL,
      target_id TEXT NOT NULL, opcode TEXT NOT NULL, argument INTEGER NOT NULL,
      state TEXT NOT NULL, bridge_device_id TEXT, claimed_at TEXT,
      result_status TEXT, result_json TEXT
    );
    CREATE TABLE IF NOT EXISTS event_label (
      event_id INTEGER PRIMARY KEY REFERENCES pilot_event(id),
      truth TEXT NOT NULL, notes TEXT NOT NULL DEFAULT '', labelled_at TEXT NOT NULL
    );
    """)
    try:
        with c:
            yield c
    finally:
        c.close()


class MeshEvent(BaseModel):
    # Preserve forward-compatible sensor/fusion attributes for future AI features.
    model_config = ConfigDict(extra="allow")

    device_id: str = Field(min_length=4, max_length=40)  # USB-connected observer
    source_device_id: str | None = None  # remote measuring Nest in RX events
    record_kind: Literal["mesh", "telemetry", "fusion", "control"]
    received_at: str | None = None
    # We retain complete payload for later feature extraction and explicit labelling.
    type: str | None = None
    result: str | None = None
    firmware: str | None = None
    boot: int | None = None
    origin_boot: int | None = None
    sequence: int | None = None
    uptime_ms: int | None = None
    direction: str | None = None
    peer: str | None = None
    max_temp: float | None = None
    ambient_temp: float | None = None
    cluster: int | None = None
    score: float | None = None
    level: int | None = None
    health: int | None = None
    persistence: int | None = None
    gas_drop_pct: float | None = None
    gas_res: float | None = None
    top5_temp: float | None = None
    hotspot_threshold: float | None = None
    battery_pct: int | None = None
    battery_mv: float | None = None
    rssi: float | None = None
    snr: float | None = None


class EventLabel(BaseModel):
    event_id: int = Field(ge=1)
    truth: Literal["fire", "no_fire", "uncertain", "controlled_test", "equipment_fault"]
    notes: str = Field(default="", max_length=1000)


def event_key(e: MeshEvent) -> str:
    # Direction + peer needed: a USB-connected Nest can observe RX and TX
    # with matching event ids. Never discard independently collected telemetry.
    if e.record_kind == "control":
        payload = e.model_dump() if hasattr(e, "model_dump") else e.dict()
        return "ctrl|{}|{}|{}|{}".format(e.device_id, payload.get("command_id"),
                                          payload.get("stage"), payload.get("status"))
    if e.boot is not None and e.sequence is not None:
        stamp = e.received_at or ""
        if e.record_kind == "telemetry":
            return f"t|{e.device_id}|{e.boot}|{e.sequence}"
        if e.record_kind == "fusion":
            return f"f|{e.device_id}|{e.boot}|{e.origin_boot}|{e.sequence}|{e.result}|{e.peer}|{stamp}"
        return f"m|{e.device_id}|{e.peer}|{e.direction}|{e.type}|{e.boot}|{e.origin_boot}|{e.sequence}|{stamp}"
    return f"raw|{e.device_id}|{e.record_kind}|{e.received_at}|{e.type}"


@router.post("/events")
def ingest(e: MeshEvent, x_client_token: str | None = Header(default=None)) -> dict[str, Any]:
    auth(x_client_token)
    now = datetime.now(timezone.utc).isoformat()
    payload = e.model_dump() if hasattr(e, "model_dump") else e.dict()
    payload["received_at"] = payload.get("received_at") or now
    with connection() as c:
        cursor = c.execute(
            "INSERT OR IGNORE INTO pilot_event(received_at, record_kind, device_id, event_type, event_key, payload_json) "
            "VALUES(?,?,?,?,?,?)",
            (payload["received_at"], e.record_kind, e.device_id, e.type, event_key(e), json.dumps(payload)),
        )
        if cursor.rowcount == 0:
            cursor = c.execute("SELECT id FROM pilot_event WHERE event_key=?", (event_key(e),))
            return {"id": cursor.fetchone()[0], "duplicate": True}
        return {"id": cursor.lastrowid, "duplicate": False}


@router.get("/events")
def list_events(limit: int = Query(default=100, ge=1, le=1000),
                x_client_token: str | None = Header(default=None)) -> list[dict]:
    operator_auth(x_client_token)
    with connection() as c:
        rows = c.execute("""SELECT p.id,p.payload_json,COALESCE(l.truth,''),COALESCE(l.notes,'')
             FROM pilot_event p LEFT JOIN event_label l ON l.event_id=p.id
             ORDER BY p.id DESC LIMIT ?""", (limit,)).fetchall()
    return [{"id": id_, **json.loads(payload), "truth": truth, "notes": notes}
            for id_, payload, truth, notes in rows]


@router.post("/labels")
def set_truth(label: EventLabel, x_client_token: str | None = Header(default=None)) -> dict:
    operator_auth(x_client_token)
    now = datetime.now(timezone.utc).isoformat()
    with connection() as c:
        if c.execute("SELECT 1 FROM pilot_event WHERE id=?", (label.event_id,)).fetchone() is None:
            raise HTTPException(status_code=404, detail="event missing")
        c.execute("INSERT INTO event_label(event_id,truth,notes,labelled_at) VALUES (?,?,?,?) "
                  "ON CONFLICT(event_id) DO UPDATE SET truth=excluded.truth,notes=excluded.notes,labelled_at=excluded.labelled_at",
                  (label.event_id, label.truth, label.notes, now))
    return {"status": "labelled", "id": label.event_id}


@router.get("/export.csv")
def export_csv(x_client_token: str | None = Header(default=None)) -> Response:
    operator_auth(x_client_token)
    with connection() as c:
        rows = c.execute("""SELECT p.id,p.received_at,p.record_kind,p.device_id,p.event_type,
           p.payload_json,COALESCE(l.truth,''),COALESCE(l.notes,'') FROM pilot_event p
           LEFT JOIN event_label l ON l.event_id=p.id ORDER BY p.id""").fetchall()
    out = io.StringIO()
    writer = csv.writer(out)
    writer.writerow(["id", "received_at", "kind", "device_id", "type", "payload_json", "truth", "notes"])
    writer.writerows(rows)
    return Response(out.getvalue(), media_type="text/csv",
                    headers={"Content-Disposition": "attachment; filename=pilot_events.csv"})
