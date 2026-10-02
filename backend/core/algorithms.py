import math
import random
import time
import numpy as np
from collections import deque
import matplotlib.pyplot as plt
import datetime
import os

FOV_X = 55.0

class ThermalAnalyzer:
    def __init__(self, history_size=10):
        # Son 'history_size' kadar (zaman, sıcaklık) ölçümünü hafızada tutar.
        self.history = deque(maxlen=history_size)

    def process_pixels_and_delta_t(self, pixels):
        """
        1. Gürültü Filtresi: Hatalı tek bir pikseli baz almamak için en sıcak 5 pikselin ortalamasını alır.
        2. Diferansiyel Analiz: Delta T (Sıcaklık değişim ivmesi) hesaplar.
        """
        now = time.time()
        
        # --- GÜRÜLTÜ FİLTRESİ (Piksel Kümesi Analizi) ---
        if len(pixels) < 5:
            raise ValueError("At least five thermal pixels are required")
        sorted_pixels = sorted(pixels, reverse=True)
        top_5_avg_temp = sum(sorted_pixels[:5]) / 5.0
        
        self.history.append((now, top_5_avg_temp))
        
        # Eğer elimizde yeterli geçmiş veri yoksa ivme 0'dır
        if len(self.history) < 2:
            return round(top_5_avg_temp, 2), 0.0
            
        # --- DELTA T (Sıcaklık İvmesi) HESAPLAMA ---
        old_time, old_temp = self.history[0]
        time_diff = now - old_time
        
        if time_diff == 0:
            return round(top_5_avg_temp, 2), 0.0
            
        # Delta T = (Son Sıcaklık - İlk Sıcaklık) / Geçen Zaman
        delta_t = (top_5_avg_temp - old_temp) / time_diff
        
        return round(top_5_avg_temp, 2), round(delta_t, 3)

# ==========================================
# YENİ NESİL ASİMETRİK GAZ FİLTRESİ (EMA)
# ==========================================
class GasAnalyzer:
    def __init__(self, alpha_up=0.05, alpha_down=0.001, alert_threshold_pct=15.0):
        self.ema = None
        self.alpha_up = alpha_up       # Isınma/Temiz hava adaptasyon hızı
        self.alpha_down = alpha_down   # Duman/Düşüş direniş hızı (makası açar)
        self.alert_threshold_pct = alert_threshold_pct

    def process_reading(self, raw_gas_ohm: float) -> dict:
        # İlk ölçüm koruması
        if self.ema is None or raw_gas_ohm <= 0:
            self.ema = raw_gas_ohm if raw_gas_ohm > 0 else 100000.0
            return {"ema_value": round(self.ema, 2), "drop_percentage": 0.0, "is_critical": False}

        # Asimetrik Güncelleme Mantığı
        if raw_gas_ohm > self.ema:
            # Yükseliş: Sensör ısınıyor veya hava temizlendi
            self.ema = (self.alpha_up * raw_gas_ohm) + ((1.0 - self.alpha_up) * self.ema)
        else:
            # Düşüş: Olası yangın/duman durumu, referansı yavaş düşür
            self.ema = (self.alpha_down * raw_gas_ohm) + ((1.0 - self.alpha_down) * self.ema)

        # Düşüş Yüzdesini Hesapla
        if self.ema > 0:
            drop_percentage = ((self.ema - raw_gas_ohm) / self.ema) * 100.0
        else:
            drop_percentage = 0.0
        
        return {
            "ema_value": round(self.ema, 2),
            "drop_percentage": round(max(0.0, drop_percentage), 2), # Negatif düşüşleri 0 kabul et
            "is_critical": drop_percentage >= self.alert_threshold_pct
        }

def generate_sensor_data(is_simulating: bool, tower_id: str):
    """Termal matrisi ve gaz sensörü verisini üretir."""
    matrix = np.random.normal(25, 1.5, (24, 32))
    gas_level = random.uniform(10, 20)
    
    if is_simulating:
        fire_x = 24 if tower_id == "T1" else 8
        for y in range(24):
            for x in range(32):
                dist = math.sqrt((x - fire_x)**2 + (y - 12)**2)
                if dist < 5:
                    matrix[y, x] += (5 - dist) * 15
        gas_level = random.uniform(80, 120)
                    
    return matrix.flatten().tolist(), gas_level

def calculate_intersection(lat1, lng1, bearing1, lat2, lng2, bearing2):
    """Approximate bearing intersection with longitude scaling.

    This remains a planar approximation and should be treated as a pilot estimate,
    not a survey-grade coordinate.
    """
    from services.sensor_fusion import calculate_triangulation

    result = calculate_triangulation(lat1, lng1, bearing1, lat2, lng2, bearing2)
    if result is None:
        return None
    return {"lat": result["fire_lat"], "lng": result["fire_lng"]}

def save_thermal_snapshot(pixel_data, node_id, max_temp):
    # Klasör yoksa oluştur
    if not os.path.exists("snapshots"):
        os.makedirs("snapshots")
        
    try:
        # 768 piksellik düz listeyi 24 satır, 32 sütunluk matrise çevir
        data_matrix = np.array(pixel_data).reshape((24, 32))
        
        # Figür oluştur
        plt.figure(figsize=(8, 6))
        # 'inferno' veya 'jet' en iyi termal renk paletleridir
        plt.imshow(data_matrix, cmap='inferno', interpolation='bicubic') 
        plt.colorbar(label='Sıcaklık (°C)')
        plt.title(f"🚨 KRİTİK ALARM - {node_id} | Max Temp: {max_temp}°C")
        
        # Dosya adı: snapshots/TOWER-D018_20260510_120910.png
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"snapshots/{node_id}_{timestamp}.png"
        
        plt.savefig(filename)
        plt.close()
        print(f"📸 Termal anlık görüntü kaydedildi: {filename}")
    except Exception as e:
        print(f"Görüntü kaydedilemedi: {e}")