"""Local-only v0.7.0 pilot control queue.

UI enqueues allowlisted, target-addressed commands. USB collector leases ONE job
and relays it to the USB-connected Nest. That Nest uses authenticated OGC2 LoRa
for a remote peer. No raw LoRa injection, arbitrary firmware upload, or CLI.
This token is pilot-local only; do not expose to the public Internet.
"""
from __future__ import annotations
from datetime import datetime, timedelta, timezone
import re
from typing import Literal

from fastapi import APIRouter, Header, HTTPException, Query
from pydantic import BaseModel, Field, model_validator
from api.mesh_events import auth, operator_auth, connection

router = APIRouter(prefix="/control", tags=["Pilot device control"])
TARGET_RE = re.compile(r"^NEST-[0-9A-F]{12}$")
ControlOpcode = Literal["STATUS", "SAMPLE", "FAST", "AUTO", "MANUAL", "PAN", "TILT", "HOME"]
LEASE_SECONDS = 110
QUEUE_SECONDS = 45


class CommandCreate(BaseModel):
    target_id: str = Field(max_length=24)
    opcode: ControlOpcode
    argument: int = 0

    @model_validator(mode="after")
    def validate_fields(self):
        if not TARGET_RE.fullmatch(self.target_id):
            raise ValueError("target_id must be NEST- plus twelve uppercase hex digits")
        if self.opcode in ("PAN", "TILT"):
            if not 20 <= self.argument <= 160:
                raise ValueError("PAN/TILT angle must be between 20 and 160 degrees")
        elif self.argument != 0:
            raise ValueError("Only PAN/TILT accept a nonzero argument")
        return self


class CommandResult(BaseModel):
    command_id: int = Field(ge=1)
    device_id: str
    target_id: str
    opcode: ControlOpcode
    stage: Literal["SENT", "RESULT"]
    status: str = Field(min_length=1, max_length=40)
    max_temp: float | None = None
    score: float | None = None
    battery_pct: int | None = None
    pan: int | None = None
    tilt: int | None = None
    manual: bool | None = None
    health: int | None = None


def now() -> datetime:
    return datetime.now(timezone.utc)


def as_dict(row) -> dict | None:
    return dict(row) if row else None


def cleanup_expired(c, clock: datetime) -> None:
    # Fail closed: a stale offline instruction must NOT execute on late reconnection.
    clock_text = clock.isoformat()
    queue_cutoff = (clock - timedelta(seconds=QUEUE_SECONDS)).isoformat()
    lease_cutoff = (clock - timedelta(seconds=LEASE_SECONDS)).isoformat()
    c.execute("""UPDATE control_command SET state='timeout', result_status='EXPIRED_OFFLINE',
                 updated_at=? WHERE state='queued' AND created_at<?""", (clock_text, queue_cutoff))
    c.execute("""UPDATE control_command SET state='timeout', result_status='NO_RESULT',
                 updated_at=? WHERE state IN ('claimed','sent') AND claimed_at<?""",
              (clock_text, lease_cutoff))


@router.post("/commands")
def create_command(body: CommandCreate, x_client_token: str | None = Header(default=None)) -> dict:
    operator_auth(x_client_token)
    timestamp = now().isoformat()
    with connection() as c:
        cleanup_expired(c, now())
        # Only one pending command globally in the two-Nest pilot. Avoid collisions.
        busy = c.execute("SELECT id FROM control_command WHERE state IN ('queued','claimed','sent') LIMIT 1").fetchone()
        if busy:
            raise HTTPException(status_code=409, detail=f"Command {busy[0]} is still pending")
        cursor = c.execute("""INSERT INTO control_command
            (created_at,updated_at,target_id,opcode,argument,state)
            VALUES (?,?,?,?,?,'queued')""",
            (timestamp, timestamp, body.target_id, body.opcode, body.argument))
        return {"id": cursor.lastrowid, "state": "queued", **body.model_dump()}


@router.get("/commands")
def list_commands(limit: int = Query(default=30, ge=1, le=100),
                  x_client_token: str | None = Header(default=None)) -> list[dict]:
    operator_auth(x_client_token)
    with connection() as c:
        cleanup_expired(c, now())
        c.row_factory = __import__('sqlite3').Row
        rows = c.execute("SELECT * FROM control_command ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
        return [dict(row) for row in rows]


@router.post("/next")
def claim_next(bridge_device_id: str = Query(...),
               x_client_token: str | None = Header(default=None)) -> dict | None:
    auth(x_client_token)
    if not TARGET_RE.fullmatch(bridge_device_id):
        raise HTTPException(status_code=422, detail="invalid bridge ID")
    with connection() as c:
        c.execute("BEGIN IMMEDIATE")
        cleanup_expired(c, now())
        if c.execute("SELECT 1 FROM control_command WHERE state IN ('claimed','sent') LIMIT 1").fetchone():
            return None
        row = c.execute("SELECT id,target_id,opcode,argument FROM control_command "
                        "WHERE state='queued' ORDER BY id LIMIT 1").fetchone()
        if not row:
            return None
        timestamp = now().isoformat()
        c.execute("UPDATE control_command SET state='claimed',bridge_device_id=?,claimed_at=?,updated_at=? "
                  "WHERE id=? AND state='queued'", (bridge_device_id, timestamp, timestamp, row[0]))
        return {"id": row[0], "target_id": row[1], "opcode": row[2], "argument": row[3]}


@router.post("/result")
def update_result(body: CommandResult, x_client_token: str | None = Header(default=None)) -> dict:
    auth(x_client_token)
    with connection() as c:
        row = c.execute("SELECT target_id,opcode,bridge_device_id,state FROM control_command WHERE id=?",
                        (body.command_id,)).fetchone()
        if not row:
            raise HTTPException(status_code=404, detail="command ID not found")
        if (row[0], row[1], row[2]) != (body.target_id, body.opcode, body.device_id):
            raise HTTPException(status_code=409, detail="result does not match leased command/bridge")
        if row[3] in ("completed", "failed", "timeout"):
            return {"id": body.command_id, "state": row[3], "duplicate_or_late": True}
        if row[3] not in ("claimed", "sent"):
            raise HTTPException(status_code=409, detail="not leased")
        if body.stage == "SENT":
            c.execute("UPDATE control_command SET state='sent',updated_at=? WHERE id=?",
                      (now().isoformat(), body.command_id))
            return {"id": body.command_id, "state": "sent"}
        result_state = "completed" if body.status == "OK" else "failed"
        c.execute("""UPDATE control_command SET state=?,result_status=?,result_json=?,updated_at=? WHERE id=?""",
                  (result_state, body.status, body.model_dump_json(), now().isoformat(), body.command_id))
        return {"id": body.command_id, "state": result_state, "result_status": body.status}
