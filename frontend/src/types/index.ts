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
  product_model?: 'MINI_NEST' | 'NEST_VISION' | 'NEST_INDUSTRIAL' | 'NEST_HUB' | 'NEST_RELAY';
  network_role?: 'NODE' | 'HUB' | 'RELAY' | 'BASE';
  lifecycle_state?: 'DISCOVERED' | 'READY_FOR_INSTALLATION' | 'LOCATION_PENDING' | 'ACTIVE' | 'TEST_FAILED' | 'MAINTENANCE' | 'DISABLED' | 'REVOKED';
  location_status?: 'PENDING' | 'GNSS_ACQUIRING' | 'GNSS_FIXED' | 'MANUAL' | 'UNAVAILABLE';
  hardware_revision?: string | null;
  capabilities?: Record<string, boolean>;
  self_test?: Record<string, boolean | null>;
  backhaul?: 'NONE' | 'LORA_LONG_HAUL' | 'LTE' | 'SATELLITE' | 'ETHERNET' | 'WIFI';
  primary_hub_id?: string | null;
  secondary_hub_id?: string | null;
  firmware_version?: string | null;
  provisioned_at?: string | null;
  last_seen_at?: string | null;
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
