#!/usr/bin/env python3
"""Collect the USB serial OGMESH / OGTELEM JSON lines from ONE connected Nest.

This is a pilot PC collector, not a dedicated unattended LoRa gateway.
It preserves raw records in JSONL and can optionally POST them to the LOCAL API.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import sys
import time
import urllib.error
import urllib.request

PREFIXES = {"OGMESH:": "mesh", "OGTELEM:": "telemetry", "OGFUSION:": "fusion", "OGCTRL:": "control"}


def parse_serial_line(line: str) -> dict | None:
    for prefix, kind in PREFIXES.items():
        if line.startswith(prefix):
            obj = json.loads(line[len(prefix):])
            if not isinstance(obj, dict) or not isinstance(obj.get("device_id"), str):
                raise ValueError("device_id required")
            obj["record_kind"] = kind
            obj["received_at"] = datetime.now(timezone.utc).isoformat()
            return obj
    return None


def forward(record: dict, base_url: str, token: str) -> None:
    request = urllib.request.Request(
        base_url.rstrip("/") + "/api/mesh/events",
        data=json.dumps(record, separators=(",", ":")).encode("utf-8"),
        headers={"Content-Type": "application/json", "X-Client-Token": token},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=3) as response:
        if not 200 <= response.status < 300:
            raise RuntimeError(f"backend status {response.status}")


def backend_json(url: str, token: str, *, payload: dict | None = None) -> dict | list | None:
    request = urllib.request.Request(
        url, data=None if payload is None else json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json", "X-Client-Token": token},
        method="POST" if payload is not None else "GET",
    )
    with urllib.request.urlopen(request, timeout=3) as response:
        return json.load(response)


def send_control_result(record: dict, base_url: str, token: str) -> None:
    if record.get("record_kind") == "control":
        # Only a signed/locally generated OGC2 result makes it here, via the USB Nest.
        body = {key: record[key] for key in (
            "command_id", "device_id", "target_id", "opcode", "stage", "status",
            "max_temp", "score", "battery_pct", "pan", "tilt", "manual", "health"
        ) if key in record}
        backend_json(base_url.rstrip("/") + "/api/control/result", token, payload=body)


def claim_next(base_url: str, token: str, bridge: str) -> dict | None:
    from urllib.parse import urlencode
    url = base_url.rstrip("/") + "/api/control/next?" + urlencode({"bridge_device_id": bridge})
    response = backend_json(url, token, payload={})
    return response if isinstance(response, dict) else None


def register_identity(base_url: str, token: str, identity: dict) -> dict | None:
    """Discover an attached bridge without downgrading a prior self-test state."""
    device_id = identity.get("device_id")
    firmware = identity.get("firmware")
    if not isinstance(device_id, str) or not isinstance(firmware, str):
        return None
    payload = {
        "device_id": device_id,
        "firmware_version": firmware,
        "hardware_revision": identity.get("hardware_revision"),
        "product_model": identity.get("product_model", "MINI_NEST"),
        "network_role": identity.get("network_role", "NODE"),
        "capabilities": identity.get("capabilities", {}),
        "backhaul": identity.get("backhaul", "NONE"),
    }
    response = backend_json(base_url.rstrip("/") + "/api/devices/register", token, payload=payload)
    return response if isinstance(response, dict) else None


def replay_existing(path: Path, base_url: str, token: str) -> tuple[int, int]:
    """Re-send prior offline JSONL; backend has stable event keys for idempotency.

    Never remove or rewrite the local file on a partial network failure.
    """
    sent = 0
    failed = 0
    if not path.exists():
        return sent, failed
    with path.open("r", encoding="utf-8") as source:
        for line_no, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
                if not isinstance(record, dict):
                    raise ValueError("not an object")
                forward(record, base_url, token)
                send_control_result(record, base_url, token)
                sent += 1
            except (ValueError, urllib.error.URLError, RuntimeError, TimeoutError) as exc:
                failed += 1
                print(f"[replay] line {line_no} NOT sent: {exc}", file=sys.stderr)
                # Stop on connectivity loss; preserve ordered history for later retry.
                if isinstance(exc, (urllib.error.URLError, TimeoutError)):
                    break
    return sent, failed


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="", help="COM4, /dev/ttyACM0, etc.; unnecessary with --replay-only")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--output", default="pilot_usb.jsonl")
    ap.add_argument("--backend", default="", help="Optional local URL e.g. http://127.0.0.1:8000")
    ap.add_argument("--replay", action="store_true", help="Re-send existing JSONL before live capture")
    ap.add_argument("--replay-only", action="store_true", help="Re-send saved JSONL without opening a USB port")
    args = ap.parse_args(argv)
    if not args.replay_only and not args.port:
        ap.error("--port required for live capture")
    if args.replay_only and not args.backend:
        ap.error("--backend required for --replay-only")
    token = os.environ.get("OG_CLIENT_TOKEN", "")
    if args.backend and not token:
        ap.error("OG_CLIENT_TOKEN must be present when --backend is used")
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.replay or args.replay_only:
        if not args.backend:
            ap.error("--backend required for replay")
        sent, failed = replay_existing(output, args.backend, token)
        print(f"[replay] forwarded={sent} failed={failed}; JSONL preserved")
        if args.replay_only or failed:
            return 0 if failed == 0 else 3
    try:
        import serial
    except ImportError:
        print("Install pyserial: python -m pip install pyserial", file=sys.stderr)
        return 2
    print(f"[collector] {args.port} => {output.resolve()}")
    try:
        with serial.Serial(args.port, args.baud, timeout=0.25, write_timeout=2) as port, output.open("a", encoding="utf-8") as dest:
            bridge_id = ""
            last_whoami = 0.0
            last_poll = 0.0
            while True:
                clock = time.monotonic()
                if not bridge_id and clock - last_whoami > 3.0:
                    port.write(b"WHOAMI\n")
                    last_whoami = clock
                if bridge_id and args.backend and clock - last_poll > 1.0:
                    last_poll = clock
                    try:
                        job = claim_next(args.backend, token, bridge_id)
                        if job:
                            line = f"CTRL {job['id']} {job['target_id']} {job['opcode']} {job['argument']}\n"
                            port.write(line.encode("ascii"))
                            port.flush()
                            print(f"[control] dispatched #{job['id']} to {job['target_id']} {job['opcode']}")
                    except (urllib.error.URLError, urllib.error.HTTPError, RuntimeError, TimeoutError) as exc:
                        print(f"[control] backend unavailable: {exc}", file=sys.stderr)
                raw = port.readline().decode("utf-8", errors="replace").strip()
                if not raw:
                    continue
                if raw.startswith("OGIDENT:"):
                    try:
                        ident = json.loads(raw[len("OGIDENT:"):])
                        value = ident.get("device_id", "")
                        if isinstance(value, str) and value.startswith("NEST-") and len(value) == 17:
                            bridge_id = value
                            print(f"[bridge] {bridge_id} {ident.get('firmware','')}")
                            if args.backend:
                                registered = register_identity(args.backend, token, ident)
                                if registered:
                                    print(f"[registry] {bridge_id} {registered.get('lifecycle_state')}")
                    except (ValueError, json.JSONDecodeError, urllib.error.URLError,
                            urllib.error.HTTPError, RuntimeError, TimeoutError) as exc:
                        print(f"[registry] bridge discovery not forwarded: {exc}", file=sys.stderr)
                    continue
                try:
                    record = parse_serial_line(raw)
                except (ValueError, json.JSONDecodeError) as exc:
                    print(f"[invalid JSON] {exc}", file=sys.stderr)
                    continue
                if record is None:
                    print(raw)
                    continue
                if not bridge_id:
                    bridge_id = record["device_id"]
                dest.write(json.dumps(record, ensure_ascii=False) + "\n")
                dest.flush()
                os.fsync(dest.fileno())  # persist each accepted sample BEFORE HTTP forwarding
                print(f"[{record['record_kind']}] {record['device_id']} "
                      f"{record.get('type', record.get('opcode', record.get('level','')))}")
                if args.backend:
                    try:
                        forward(record, args.backend, token)
                        send_control_result(record, args.backend, token)
                    except (urllib.error.URLError, urllib.error.HTTPError, RuntimeError, TimeoutError) as exc:
                        print(f"[offline] backend not available: {exc}; JSONL retained", file=sys.stderr)
    except KeyboardInterrupt:
        print("\n[collector] stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
