"""LoRa explicit-header, CRC-enabled time-on-air estimator for RF test planning.

Indicative only: technician must check actual on-air duration and Turkish band
conditions including LBT/AFA/duty cycle using a spectrum/radio measurement.
"""
from __future__ import annotations
import math


def airtime_seconds(payload_bytes: int, sf=9, bandwidth_khz=125.0, coding_rate_denominator=7,
                    preamble=12, crc=True) -> float:
    if not (1 <= payload_bytes <= 255 and 7 <= sf <= 12 and bandwidth_khz > 0 and
            5 <= coding_rate_denominator <= 8):
        raise ValueError('invalid LoRa parameters')
    sym=2**sf/(bandwidth_khz*1000)
    de=1 if sf>=11 and bandwidth_khz<=125 else 0
    denominator=4*(sf-2*de)
    numerator=8*payload_bytes-4*sf+28+(16 if crc else 0)
    payload_symbols=8+max(math.ceil(numerator/denominator),0)*coding_rate_denominator
    return (preamble+4.25+payload_symbols)*sym


def minimum_maintenance_gap(payload_bytes: int, *, duty_cycle=0.01, margin=1.15, **kwargs) -> float:
    if not 0 < duty_cycle <= 1: raise ValueError('invalid duty cycle')
    return airtime_seconds(payload_bytes, **kwargs)*margin/duty_cycle

if __name__=='__main__':
    for size in (90,160,216):
        print(f'{size} B: airtime={airtime_seconds(size):.2f} sec; '
              f'illustrative 1% gap incl margin={minimum_maintenance_gap(size):.0f} sec')
