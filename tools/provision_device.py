#!/usr/bin/env python3
"""Register an already-flashed Nest and verify that it is installation-ready.

This is intentionally a post-flash step: connect one Nest over USB, run the
command once, and the eFuse-derived NEST ID is registered without manual entry.
Secrets remain outside command-line arguments and are read from the environment.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

ID_RE = re.compile(r"^NEST-[0-9A-F]{12}$")


def parse_prefixed_json(line: str, prefix: str) -> dict | None:
    if not line.startswith(prefix):
        return None
    value = json.loads(line[len(prefix):])
    if not isinstance(value, dict):
        raise ValueError(f"{prefix} payload must be a JSON object")
    return value


def read_reply(port, command: bytes, prefix: str, timeout: float) -> dict:
    port.reset_input_buffer()
    port.write(command)
    port.flush()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        value = parse_prefixed_json(line, prefix)
        if value is not None:
            return value
    raise TimeoutError(f"Nest did not answer {command.decode().strip()}")


def register(base_url: str, token: str, identity: dict, self_test: dict) -> dict:
    device_id = identity.get("device_id", "")
    if not isinstance(device_id, str) or not ID_RE.fullmatch(device_id):
        raise ValueError("WHOAMI returned an invalid Nest ID")
    firmware = identity.get("firmware", "")
    if not isinstance(firmware, str) or not firmware:
        raise ValueError("WHOAMI did not include a firmware version")

    payload = {
        "device_id": device_id,
        "firmware_version": firmware,
        "hardware_revision": identity.get("hardware_revision"),
        "product_model": identity.get("product_model", "MINI_NEST"),
        "network_role": identity.get("network_role", "NODE"),
        "capabilities": identity.get("capabilities", {}),
        "self_test": self_test,
        "backhaul": identity.get("backhaul", "NONE"),
    }
    request = urllib.request.Request(
        base_url.rstrip("/") + "/api/devices/register",
        data=json.dumps(payload, separators=(",", ":")).encode("utf-8"),
        headers={"Content-Type": "application/json", "X-Client-Token": token},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=8) as response:
        return json.load(response)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="COM5, /dev/ttyACM0, etc.")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--backend", default="http://127.0.0.1:8000")
    parser.add_argument("--timeout", type=float, default=12.0)
    args = parser.parse_args(argv)

    token = os.environ.get("OG_CLIENT_TOKEN", "")
    if not token:
        parser.error("OG_CLIENT_TOKEN must be set for device registration")
    try:
        import serial
    except ImportError:
        print("Install pyserial: python -m pip install -r tools/requirements.txt", file=sys.stderr)
        return 2

    try:
        with serial.Serial(args.port, args.baud, timeout=0.25, write_timeout=2) as port:
            time.sleep(1.0)
            identity = read_reply(port, b"WHOAMI\n", "OGIDENT:", args.timeout)
            self_test = read_reply(port, b"SELFTEST\n", "OGSELFTEST:", args.timeout)
        result = register(args.backend, token, identity, self_test)
    except (ValueError, TimeoutError, urllib.error.URLError, urllib.error.HTTPError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 3

    checks = result.get("self_test", {})
    capabilities = result.get("capabilities", {})
    required_checks = ["storage", "lora"]
    if capabilities.get("thermal", True):
        required_checks.append("thermal")
    if capabilities.get("environmental", True):
        required_checks.append("environmental")
    passed = all(checks.get(name) is True for name in required_checks)
    print(f"Device: {result['device_id']}")
    print(f"Model/role: {result['product_model']} / {result['network_role']}")
    print(f"Firmware: {result['firmware_version']}")
    print("Self-test: " + ("PASS" if passed else "FAIL"))
    print(f"Lifecycle: {result['lifecycle_state']}")
    return 0 if result["lifecycle_state"] == "READY_FOR_INSTALLATION" else 4


if __name__ == "__main__":
    raise SystemExit(main())
