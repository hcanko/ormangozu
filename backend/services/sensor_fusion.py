import time
import math

# ==========================================
# 1. LEGACY ANALİZ VE FİLTRELEME (Geriye Dönük Uyumluluk İçin)
# ==========================================
tower_history = {}

def analyze_and_filter(tower_id, pixels, gas_level, base_bearing):
    current_time = time.time()
    
    # 1. GÜRÜLTÜ FİLTRESİ (Piksel Kümesi Analizi)
    # Tek bir bozuk sensör pikselini baz almamak için en sıcak 5 pikselin ortalamasını alıyoruz
    sorted_pixels = sorted(pixels, reverse=True)
    top_5_avg_temp = sum(sorted_pixels[:5]) / 5.0
    
    # 2. DİFERANSİYEL ANALİZ (dT/dt - Sıcaklık Değişim Hızı)
    dt_dt = 0.0
    if tower_id in tower_history:
        prev_data = tower_history[tower_id]
        delta_temp = top_5_avg_temp - prev_data["temp"]
        delta_time = current_time - prev_data["time"]
        
        if delta_time > 0:
            dt_dt = delta_temp / delta_time # 1 Saniyedeki derece artışı
            
    # Gelecek hesaplama için şu anki değeri hafızaya kaydet
    tower_history[tower_id] = {"time": current_time, "temp": top_5_avg_temp}
    
    return {
        "filtered_temp": round(top_5_avg_temp, 2),
        "delta_t": round(dt_dt, 3)
    }

# ==========================================
# 2. YENİ NESİL SENSÖR FÜZYONU (Bulanık Mantık & Karar Motoru)
# ==========================================
# 🔥 YENİ: Ağırlıkları her yerden (Admin panelinden) erişilebilir Global bir sözlüğe alıyoruz
GLOBAL_WEIGHTS = {
    "weight_delta_t": 0.40,  # %40: Ani sıcaklık artışı (En tehlikelisi)
    "weight_temp": 0.30,     # %30: Mutlak sıcaklık (Ortam ısısı)
    "weight_gas": 0.30,       # %30: VOC / Gaz Anomalisi (Duman belirtisi)
    "override_temp": 90.0
}

class SensorFusion:
    def __init__(self):
        # Artık yerel (self) değişkenlere ihtiyacımız yok, GLOBAL_WEIGHTS okuyacağız
        pass

    def calculate_fire_score(self, max_temp, delta_t, gas_impact):
        """
        Gelen verileri 0 ile 100 arasında normalize edip ağırlıklı skor (Fire Score) çıkarır.
        """
        # 1. Delta T (Sıcaklık İvmesi) Skoru
        # Eğer sıcaklık saniyede 2 derece veya daha hızlı artıyorsa, bu skor %100 olur.
        score_delta_t = min(max(delta_t * 50, 0), 100)

        # 2. Mutlak Sıcaklık Skoru
        # 25 derecenin altı 0 puandır. 75 derece ve üstüne ulaştığında %100 olur.
        score_temp = min(max((max_temp - 25) * 2, 0), 100)

        # 3. Gaz Düşüş (Anomali) Skoru
        # Temiz hava baseline'ına göre dirençte %50'lik bir düşüş bile skoru %100 yapar.
        score_gas = min(max(gas_impact * 2, 0), 100)

        # 🧠 SENSÖR FÜZYONU: Sabit self değişkenleri yerine GLOBAL_WEIGHTS kullan
        fire_score = (score_delta_t * GLOBAL_WEIGHTS["weight_delta_t"]) + \
                     (score_temp * GLOBAL_WEIGHTS["weight_temp"]) + \
                     (score_gas * GLOBAL_WEIGHTS["weight_gas"])
        
        fire_score = round(fire_score, 2)

        # 🚨 DURUM (Status) Belirleme
        
        # --- YENİ: MUTLAK GÜVENLİK KİLİDİ (OVERRIDE) ---
        # Eğer ortam tek başına 90 derecenin üzerindeyse, 
        # gazı veya artış hızını bekleme! Direkt KRİTİK bas!
        if max_temp >= GLOBAL_WEIGHTS["override_temp"]:
            fire_score = 100.0
            status = "KRİTİK"
            
        # Eğer sıcaklık 90'ın altındaysa normal zeka algoritmasına güven
        else:
            if fire_score > 80:
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
                "score_gas": round(score_gas, 2)
            }
        }

# ==========================================
# 3. NİRENGİ (TRIANGULATION) MATEMATİĞİ
# ==========================================
def calculate_triangulation(lat1, lon1, bearing1, lat2, lon2, bearing2):
    """
    İki kuleden gelen pusula açılarını kesiştirip (yangının çıktığı) noktayı hesaplar.
    """
    # 1. Pusula açılarını (Kuzey=0, Doğu=90) matematiksel Kartezyen açılara çevir (Derece -> Radyan)
    angle1 = math.radians(90 - bearing1)
    angle2 = math.radians(90 - bearing2)

    # 2. Çizgilerin eğimlerini (m = tan(açı)) hesapla
    # Tan(90) veya Tan(270) sonsuzluğa gideceği için küçük bir tolerans (1e10) koyuyoruz
    m1 = math.tan(angle1) if abs(angle1 % math.pi - math.pi/2) > 0.001 else 1e10
    m2 = math.tan(angle2) if abs(angle2 % math.pi - math.pi/2) > 0.001 else 1e10

    # 3. Eğer iki direk de tam olarak aynı açıya (paralel) bakıyorsa kesişim olmaz
    if abs(m1 - m2) < 1e-5:
        return None  # Paralel bakış, hedef bulunamadı

    # 4. Dünya yuvarlak olduğu için kutuplara doğru boylamlar daralır.
    # Harita üzerinde milimetrik sonuç için Boylam Ölçekleme Faktörü uyguluyoruz.
    avg_lat = math.radians((lat1 + lat2) / 2.0)
    lon_scale = math.cos(avg_lat)
    
    # X, Y koordinatlarında kesişim bulma
    x1, y1 = lon1 * lon_scale, lat1
    x2, y2 = lon2 * lon_scale, lat2
    
    c1 = y1 - m1 * x1
    c2 = y2 - m2 * x2
    
    intersect_x = (c2 - c1) / (m1 - m2)
    intersect_y = m1 * intersect_x + c1
    
    target_lon = intersect_x / lon_scale
    target_lat = intersect_y
    
    return {
        "fire_lat": round(target_lat, 6),
        "fire_lng": round(target_lon, 6)
    }