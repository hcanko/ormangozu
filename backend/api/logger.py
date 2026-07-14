import csv
import os
from datetime import datetime

class SensorLogger:
    def __init__(self, filename="sensor_logs.csv"):
        self.filename = filename
        self.is_logging = False
        
        # Dosya yoksa başlıkları oluştur (Gelecek rezervasyonları ve zeka verileri eklendi)
        if not os.path.exists(self.filename):
            with open(self.filename, mode='w', newline='', encoding='utf-8') as f:
                writer = csv.writer(f)
                writer.writerow([
                    "Zaman", "Direk_ID", "Max_Sicaklik", "Delta_T", 
                    "Ham_Gaz_Ohm", "Gaz_PPM_Etkisi", "Yangin_Skoru", 
                    "Durum", "Pil", "Enlem", "Boylam", 
                    "Pusula", "Ruzgar_Hizi", "Olay_Notu"
                ])

    def log_data(self, tower_id, max_temp, delta_t, gas_raw_resistance, gas_ppm_impact, fire_score, status, battery, lat=None, lng=None, compass_bearing=None, wind_speed=None):
        if not self.is_logging:
            return
            
        with open(self.filename, mode='a', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            writer.writerow([
                timestamp, tower_id, max_temp, delta_t, 
                gas_raw_resistance, gas_ppm_impact, fire_score, 
                status, battery, lat, lng, 
                compass_bearing, wind_speed, ""
            ])

    def mark_event(self, note):
        """CSV dosyasına sadece o anki olayı belirten özel bir satır ekler"""
        if not self.is_logging:
            return False
            
        with open(self.filename, mode='a', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            # Sensör verileri boş, sadece Olay sütunu dolu bir işaretçi (Checkpoint) satırı
            writer.writerow([timestamp, "SISTEM", "", "", "", "", "", "", "", "", "", "", "", f"📍 {note}"])
        return True

    def toggle(self):
        self.is_logging = not self.is_logging
        if self.is_logging:
            self.mark_event("KAYIT BAŞLADI")
        else:
            self.mark_event("KAYIT DURDURULDU")
        return self.is_logging

logger = SensorLogger()