"""Pure Python reference for the *pilot* LoRa framing (OG4).

One shared key across TWO nodes is deliberately not a production key strategy.
HMAC proves integrity/authenticity to holders of that shared key. It does not
provide confidentiality or protect against an attacker extracting firmware.
"""
from dataclasses import dataclass
import hashlib
import hmac

VALID_TYPES = {"ALERT", "ACK", "REPORT", "CONFIRM"}


def valid_node_id(value: str) -> bool:
    return len(value) == 17 and value.startswith("NEST-") and all(
        ch in "0123456789ABCDEF" for ch in value[5:]
    )


class ProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class Packet:
    kind: str
    source: str
    source_boot: int
    destination: str
    event_boot: int
    sequence: int
    level: int = 0
    health: int = 0
    score: float = 0.0
    max_temp: float = 0.0
    cluster: int = 0
    persistence: int = 0
    gas_drop: float = 0.0
    battery: int = 0
    hop: int = 0  # v0.7 authenticated relay hop (0..2)


def encode(packet: Packet, shared_key: str) -> str:
    if packet.kind not in VALID_TYPES or len(shared_key) < 32:
        raise ProtocolError("invalid kind or weak shared key")
    if not valid_node_id(packet.source) or (
        packet.destination != "*" and not valid_node_id(packet.destination)
    ):
        raise ProtocolError("invalid pilot Nest MAC identity")
    if packet.hop not in range(3):
        raise ProtocolError("hop out of range")
    if min(packet.source_boot, packet.event_boot, packet.sequence) <= 0:
        raise ProtocolError("boot counters and sequence must be positive")
    body = "|".join(("OG4", packet.kind, packet.source, str(packet.source_boot),
                     packet.destination, str(packet.event_boot), str(packet.sequence),
                     str(packet.level), str(packet.health), f"{packet.score:.1f}",
                     f"{packet.max_temp:.1f}", str(packet.cluster), str(packet.persistence),
                     f"{packet.gas_drop:.1f}", str(packet.battery), str(packet.hop)))
    tag = hmac.new(shared_key.encode(), body.encode(), hashlib.sha256).digest()[:12].hex()
    return body + "|" + tag


def decode(wire: str, shared_key: str) -> Packet:
    if len(wire) > 240: raise ProtocolError("oversize")
    fields = wire.split("|")
    if len(fields) != 17 or fields[0] != "OG4" or fields[1] not in VALID_TYPES:
        raise ProtocolError("bad packet framing")
    body, tag = wire.rsplit("|", 1)
    expected = hmac.new(shared_key.encode(), body.encode(), hashlib.sha256).digest()[:12].hex()
    if not hmac.compare_digest(expected, tag.lower()):
        raise ProtocolError("bad MAC")
    try:
        p = Packet(fields[1], fields[2], int(fields[3]), fields[4], int(fields[5]),
                   int(fields[6]), int(fields[7]), int(fields[8]), float(fields[9]),
                   float(fields[10]), int(fields[11]), int(fields[12]), float(fields[13]),
                   int(fields[14]), int(fields[15]))
    except ValueError as exc:
        raise ProtocolError("bad field type") from exc
    if p.hop not in range(3):
        raise ProtocolError("invalid relay hop")
    if min(p.source_boot, p.event_boot, p.sequence) <= 0:
        raise ProtocolError("zero boot or sequence")
    if not valid_node_id(p.source) or (
        p.destination != "*" and not valid_node_id(p.destination)
    ):
        raise ProtocolError("invalid pilot Nest MAC identity")
    return p


class ReplayWindow:
    """Reference for message types with monotonic source sequences. ACKs correlate to requests."""
    def __init__(self):
        self.last: dict[tuple[str, str], tuple[int, int, int]] = {}

    def fresh(self, p: Packet) -> bool:
        if p.kind == "ACK":
            raise ProtocolError("ACK must be correlated with the pending request instead")
        key = (p.source, p.kind)
        previous = self.last.get(key, (0, 0, 0))
        if (p.source_boot, p.event_boot, p.sequence) <= previous: return False
        self.last[key] = (p.source_boot, p.event_boot, p.sequence)
        return True


def matches_ack(ack: Packet, pending: Packet) -> bool:
    return (ack.kind == "ACK" and ack.destination == pending.source and
            ack.source != pending.source and ack.event_boot == pending.event_boot and
            ack.sequence == pending.sequence and
            (pending.destination == "*" or ack.source == pending.destination))
