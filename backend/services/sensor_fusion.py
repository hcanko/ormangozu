import math
import time
from typing import Mapping

GLOBAL_WEIGHTS = {
    "weight_delta_t": 0.40,
    "weight_temp": 0.30,
    "weight_gas": 0.30,
    "override_temp": 90.0,
}


def apply_calibration(settings: Mapping[str, float]) -> dict[str, float]:
    """Normalize score weights and apply them atomically."""
    delta = float(settings["weight_delta_t"])
    temp = float(settings["weight_temp"])
    gas = float(settings["weight_gas"])
    total = delta + temp + gas
    if total <= 0:
        raise ValueError("calibration weight total must be greater than zero")

    GLOBAL_WEIGHTS.update({
        "weight_delta_t": delta / total,
        "weight_temp": temp / total,
        "weight_gas": gas / total,
        "override_temp": float(settings["override_temp"]),
    })
    return dict(GLOBAL_WEIGHTS)


class SensorFusion:
    def calculate_fire_score(self, max_temp: float, delta_t: float, gas_impact: float) -> dict:
        score_delta_t = min(max(delta_t * 50.0, 0.0), 100.0)
        score_temp = min(max((max_temp - 25.0) * 2.0, 0.0), 100.0)
        score_gas = min(max(gas_impact * 2.0, 0.0), 100.0)

        fire_score = round(
            score_delta_t * GLOBAL_WEIGHTS["weight_delta_t"]
            + score_temp * GLOBAL_WEIGHTS["weight_temp"]
            + score_gas * GLOBAL_WEIGHTS["weight_gas"],
            2,
        )

        if max_temp >= GLOBAL_WEIGHTS["override_temp"]:
            fire_score = 100.0
            status = "KRİTİK"
        elif fire_score > 80:
            status = "KRİTİK"
        elif fire_score > 55:
            status = "UYARI"
        else:
            status = "NORMAL"

        return {
            "fire_score": fire_score,
            "status": status,
            "details": {
                "score_delta_t": round(score_delta_t, 2),
                "score_temp": round(score_temp, 2),
                "score_gas": round(score_gas, 2),
            },
        }


# Legacy helpers retained for backwards compatibility.
tower_history: dict[str, dict[str, float]] = {}


def analyze_and_filter(tower_id: str, pixels: list[float], gas_level: float, base_bearing: float) -> dict:
    del gas_level, base_bearing
    current_time = time.time()
    sorted_pixels = sorted(pixels, reverse=True)
    top_5_avg_temp = sum(sorted_pixels[:5]) / 5.0

    dt_dt = 0.0
    if tower_id in tower_history:
        previous = tower_history[tower_id]
        elapsed = current_time - previous["time"]
        if elapsed > 0:
            dt_dt = (top_5_avg_temp - previous["temp"]) / elapsed

    tower_history[tower_id] = {"time": current_time, "temp": top_5_avg_temp}
    return {"filtered_temp": round(top_5_avg_temp, 2), "delta_t": round(dt_dt, 3)}


def calculate_triangulation(lat1: float, lon1: float, bearing1: float, lat2: float, lon2: float, bearing2: float):
    angle1 = math.radians(90 - bearing1)
    angle2 = math.radians(90 - bearing2)
    m1 = math.tan(angle1) if abs(angle1 % math.pi - math.pi / 2) > 0.001 else 1e10
    m2 = math.tan(angle2) if abs(angle2 % math.pi - math.pi / 2) > 0.001 else 1e10
    if abs(m1 - m2) < 1e-5:
        return None

    avg_lat = math.radians((lat1 + lat2) / 2.0)
    lon_scale = math.cos(avg_lat)
    if abs(lon_scale) < 1e-8:
        return None

    x1, y1 = lon1 * lon_scale, lat1
    x2, y2 = lon2 * lon_scale, lat2
    c1 = y1 - m1 * x1
    c2 = y2 - m2 * x2
    intersect_x = (c2 - c1) / (m1 - m2)
    intersect_y = m1 * intersect_x + c1
    return {"fire_lat": round(intersect_y, 6), "fire_lng": round(intersect_x / lon_scale, 6)}
