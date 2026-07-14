export interface ThermalData {
  avg_temp: number;
  delta_t: number;           // Sıcaklık ivmesi
  gas_level: number;         // Ham direnç
  gas_ppm_impact: number;    // Gaz anomalisi yüzdesi
  fire_score: number;        // Yapay zeka yangın skoru
  status: string;
}

export interface Tower {
  id: string;
  name: string;
  ip: string;
  lat: number;
  lng: number;
  bearing: number;
  status: string; // 'NORMAL' | 'UYARI' | 'KRİTİK' | 'offline'
  
  // DONANIM VE SENSÖR BİLGİLERİ
  is_online: boolean;
  battery: number;
  pixels: number[];
  is_gps_fixed?: boolean;  
  currentData?: ThermalData;
  
  // YENİ NESİL ZEKİ SİSTEM VERİLERİ
  fire_score?: number;
  last_update?: string;
}

export interface FireLocation {
  lat: number;
  lng: number;
  confidence_score?: number; // Nirengi doğruluk payı (Gelecek için rezerve)
}

export interface DashboardData {
  towers: Tower[];
  fireLocation: FireLocation | null;
}