#!/usr/bin/env python3
"""Decode ESP32 LittleFS OGFR v2/v3 thermal ring into JSONL for future analysis.

Frame temperatures are stored as signed tenths of a degree Celsius. A failed
CRC or partial record is reported and not treated as ground truth.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import sys

HEADER = struct.Struct("<IHHIIhhHbbBBH")  # legacy v2
HEADER_V3 = struct.Struct("<IHHIIIhhHbbBBH")  # v3 adds boot_counter
PIXELS = struct.Struct("<768h")
MAGIC = 0x4F474652


def crc16(data: bytes) -> int:
    value = 0xFFFF
    for b in data:
        value ^= b << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x1021) & 0xFFFF if (value & 0x8000) else (value << 1) & 0xFFFF
    return value


def read_frames(data: bytes, *, include_pixels: bool = False):
    """Yield verified frames. v3 boot IDs make cold-reboot joins unambiguous.

    Reject invalid/incomplete frames rather than silently shifting the binary offset.
    Each file may contain v2 or v3 records as versions change during the pilot.
    """
    offset = 0
    while offset < len(data):
        if len(data) - offset < 8:
            raise ValueError(f"incomplete frame header at byte {offset}")
        magic, version, count = struct.unpack_from("<IHH", data, offset)
        if magic != MAGIC or count != 768 or version not in (2, 3):
            raise ValueError(f"bad header at offset {offset}")
        header = HEADER_V3 if version == 3 else HEADER
        length = header.size + PIXELS.size
        piece = data[offset:offset + length]
        if len(piece) < length:
            raise ValueError(f"incomplete frame at byte {offset}, have {len(piece)} / {length}")
        fields = header.unpack(piece[:header.size])
        if version == 3:
            (_, _, _, boot, seq, uptime, maximum, ambient,
             cluster, x, y, level, persistence, stored_crc) = fields
        else:
            (_, _, _, seq, uptime, maximum, ambient,
             cluster, x, y, level, persistence, stored_crc) = fields
            boot = None  # legacy v2 cannot safely disambiguate a cold reboot
        encoded_pixels = piece[header.size:]
        if crc16(encoded_pixels) != stored_crc:
            raise ValueError(f"CRC mismatch at offset {offset}")
        frame = {"format_version": version, "boot": boot, "sequence": seq,
                 "uptime_ms": uptime, "max_temp": maximum / 10, "ambient_temp": ambient / 10,
                 "cluster": cluster, "hotspot_x": x, "hotspot_y": y,
                 "fire_level": level, "persistence": persistence,
                 "pixel_crc": stored_crc}
        if include_pixels:
            frame["pixels_c"] = [p / 10 for p in PIXELS.unpack(encoded_pixels)]
        yield frame
        offset += length


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("files", nargs="+", type=Path, help="/frames_1.bin first, then /frames_0.bin")
    ap.add_argument("--include-pixels", action="store_true")
    ap.add_argument("--device-id", default="", help="Nest ID read from /status; required for reliable multi-device AI joins")
    ap.add_argument("--output", default="frames.jsonl")
    args = ap.parse_args()
    with open(args.output, "w", encoding="utf-8") as out:
        for file in args.files:
            for frame in read_frames(file.read_bytes(), include_pixels=args.include_pixels):
                frame["source_file"] = file.name
                if args.device_id: frame["device_id"] = args.device_id
                out.write(json.dumps(frame) + "\n")
    print(args.output)


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        print(error, file=sys.stderr)
        raise SystemExit(2)
