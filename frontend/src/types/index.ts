export interface ThermalData {
  avg_temp: number;
  delta_t: number;
  gas_level: number;
  gas_ppm_impact: number;
  fire_score: number;
  status: string;
}

export interface Tower {
  id: string;
  name: string;
  ip: string;
  lat: number;
  lng: number;
  bearing: number;
  status: string;
  sleep_interval: number;
  is_online: boolean;
  battery: number;
  pixels: number[];
  max_temp?: number;
  gas_raw_resistance?: number;
  is_gps_fixed?: boolean;
  currentData?: ThermalData;
  fire_score?: number;
  last_update?: string;
}

export interface FireLocation {
  lat: number;
  lng: number;
  confidence_score?: number;
}

export interface DashboardData {
  towers: Tower[];
  fireLocation: FireLocation | null;
  active_alerts?: unknown[];
}
