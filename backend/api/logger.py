import csv
import os
import threading
from datetime import datetime


class SensorLogger:
    def __init__(self, filename: str = "sensor_logs.csv") -> None:
        self.filename = filename
        self.is_logging = False
        self._lock = threading.Lock()
        self._ensure_header()

    def _ensure_header(self) -> None:
        directory = os.path.dirname(os.path.abspath(self.filename))
        os.makedirs(directory, exist_ok=True)
        if not os.path.exists(self.filename) or os.path.getsize(self.filename) == 0:
            with open(self.filename, mode="w", newline="", encoding="utf-8") as handle:
                csv.writer(handle).writerow([
                    "Zaman", "Direk_ID", "Max_Sicaklik", "Delta_T",
                    "Ham_Gaz_Ohm", "Gaz_PPM_Etkisi", "Yangin_Skoru",
                    "Durum", "Pil", "Enlem", "Boylam",
                    "Pusula", "Ruzgar_Hizi", "Olay_Notu",
                ])

    def _append(self, row: list[object]) -> None:
        with self._lock:
            with open(self.filename, mode="a", newline="", encoding="utf-8") as handle:
                csv.writer(handle).writerow(row)

    def log_data(
        self, tower_id: str, max_temp: float, delta_t: float,
        gas_raw_resistance: float, gas_ppm_impact: float, fire_score: float,
        status: str, battery: float, lat: float | None = None,
        lng: float | None = None, compass_bearing: float | None = None,
        wind_speed: float | None = None,
    ) -> bool:
        if not self.is_logging:
            return False
        self._append([
            datetime.now().isoformat(timespec="milliseconds"), tower_id, max_temp,
            delta_t, gas_raw_resistance, gas_ppm_impact, fire_score, status,
            battery, lat, lng, compass_bearing, wind_speed, "",
        ])
        return True

    def mark_event(self, note: str, *, force: bool = False) -> bool:
        if not self.is_logging and not force:
            return False
        self._append([
            datetime.now().isoformat(timespec="milliseconds"), "SISTEM", "", "",
            "", "", "", "", "", "", "", "", "", f"📍 {note}",
        ])
        return True

    def toggle(self) -> bool:
        if self.is_logging:
            self.mark_event("KAYIT DURDURULDU", force=True)
            self.is_logging = False
        else:
            self.is_logging = True
            self.mark_event("KAYIT BAŞLADI", force=True)
        return self.is_logging


logger = SensorLogger()
