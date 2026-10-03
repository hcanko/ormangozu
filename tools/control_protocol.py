"""Test-only Python reference for the OGC2 packet body/signature.

The live USB collector never signs LoRa frames: only its physical bridge Nest can
sign/transmit them. Use this module for test vectors and protocol audits, not to
replace radio/firmware tests. 96-bit truncated HMAC-SHA256, same as mesh_auth.cpp.
"""
from __future__ import annotations
from dataclasses import dataclass
import hashlib
import hmac
import re

ID_PATTERN = re.compile(r"^NEST-[A-F0-9]{12}$")
OPS = {"STATUS", "SAMPLE", "FAST", "AUTO", "MANUAL", "PAN", "TILT", "HOME"}


def tag(body: str, key: bytes) -> str:
    if len(key) < 32:
        raise ValueError("pilot key must have 32+ bytes")
    return hmac.new(key, body.encode("ascii"), hashlib.sha256).digest()[:12].hex()


def command(src: str, boot: int, dst: str, seq: int, op: str, arg: int, key: bytes, hop: int = 0) -> str:
    if not (ID_PATTERN.fullmatch(src) and ID_PATTERN.fullmatch(dst) and
            src != dst and boot > 0 and seq > 0 and op in OPS):
        raise ValueError("invalid command address/type/sequence")
    if op in ("PAN", "TILT") and not 20 <= arg <= 160:
        raise ValueError("invalid angle")
    if op not in ("PAN", "TILT") and arg != 0:
        raise ValueError("unexpected argument")
    if hop not in (0, 1, 2):
        raise ValueError("invalid bounded hop")
    body = f"OGC2|CMD|{src}|{boot}|{dst}|{seq}|{op}|{arg}|{hop}"
    frame = f"{body}|{tag(body,key)}"
    if len(frame) >= 240:
        raise ValueError("LoRa frame too long")
    return frame


def verify(frame: str, key: bytes, target: str) -> dict:
    try:
        body, mac = frame.rsplit("|", 1)
    except ValueError as exc:
        raise ValueError("missing MAC") from exc
    if not hmac.compare_digest(mac, tag(body, key)):
        raise ValueError("unauthenticated frame")
    fields = body.split("|")
    if len(fields) != 9 or fields[0:2] != ["OGC2", "CMD"]:
        raise ValueError("not an OGC2 command")
    _, _, src, boot, dst, seq, op, arg, hop = fields
    if hop not in ("0", "1", "2"):
        raise ValueError("invalid hop")
    if not (ID_PATTERN.fullmatch(src) and ID_PATTERN.fullmatch(dst) and dst == target):
        raise ValueError("wrong address")
    nboot, nseq, narg = int(boot), int(seq), int(arg)
    if nboot <= 0 or nseq <= 0 or op not in OPS:
        raise ValueError("invalid command")
    if op in ("PAN", "TILT") and not 20 <= narg <= 160:
        raise ValueError("invalid servo angle")
    if op not in ("PAN", "TILT") and narg != 0:
        raise ValueError("unexpected argument")
    return {"source": src, "boot": nboot, "target": dst, "sequence": nseq,
            "opcode": op, "argument": narg, "hop": int(hop)}


@dataclass
class PilotReplay:
    source: str = ""
    boot: int = 0
    sequence: int = 0

    def accept(self, record: dict) -> bool:
        if self.source and record["source"] != self.source:
            return False
        current = (record["boot"], record["sequence"])
        if current <= (self.boot, self.sequence):
            return False
        self.source = record["source"]
        self.boot, self.sequence = current
        return True
