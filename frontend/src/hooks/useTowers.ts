import { useEffect, useState } from 'react';
import { apiUrl, WS_URL } from '../lib/api';
import type { DashboardData, Tower } from '../types/index';

const EMPTY_DATA: DashboardData = {
  towers: [],
  fireLocation: null,
  active_alerts: [],
};

export const useTowers = () => {
  const [data, setData] = useState<DashboardData>(EMPTY_DATA);

  useEffect(() => {
    let disposed = false;
    let socket: WebSocket | null = null;
    let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
    let keepAliveTimer: ReturnType<typeof setInterval> | null = null;
    let retryCount = 0;

    const fetchDashboard = async () => {
      try {
        const response = await fetch(apiUrl('/towers/live'));
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const result = await response.json();
        if (!disposed) {
          setData({
            towers: Array.isArray(result.towers) ? result.towers : [],
            fireLocation: result.fireLocation ?? null,
            active_alerts: Array.isArray(result.active_alerts) ? result.active_alerts : [],
          });
        }
      } catch (error) {
        console.error('Dashboard verisi çekilemedi:', error);
      }
    };

    const updateSingleTower = (message: Record<string, unknown>) => {
      const towerId = String(message.tower_id ?? '');
      if (!towerId) return;

      setData((previous) => {
        let found = false;
        const towers = previous.towers.map((tower): Tower => {
          if (tower.id !== towerId) return tower;
          found = true;
          return {
            ...tower,
            fire_score: Number(message.fire_score ?? tower.fire_score ?? 0),
            status: String(message.status ?? tower.status),
            is_online: true,
            battery: Number(message.battery_level ?? tower.battery),
            pixels: Array.isArray(message.pixels) ? (message.pixels as number[]) : tower.pixels,
            currentData: {
              avg_temp: Number(message.avg_temp ?? tower.currentData?.avg_temp ?? 0),
              delta_t: Number(message.delta_t ?? tower.currentData?.delta_t ?? 0),
              gas_level: Number(message.gas_level ?? tower.currentData?.gas_level ?? 0),
              gas_ppm_impact: Number(
                message.gas_ppm_impact ?? tower.currentData?.gas_ppm_impact ?? 0,
              ),
              fire_score: Number(message.fire_score ?? tower.currentData?.fire_score ?? 0),
              status: String(message.status ?? tower.currentData?.status ?? tower.status),
            },
          };
        });

        if (!found) void fetchDashboard();
        return { ...previous, towers };
      });
    };

    const handleMessage = (raw: string) => {
      try {
        const message = JSON.parse(raw);
        if (Array.isArray(message.towers)) {
          setData({
            towers: message.towers,
            fireLocation: message.fireLocation ?? null,
            active_alerts: Array.isArray(message.active_alerts) ? message.active_alerts : [],
          });
          return;
        }

        if (message.type === 'SENSOR_UPDATE' || message.type === 'FIRE_ALERT') {
          updateSingleTower(message);
        }
      } catch (error) {
        console.error('WebSocket mesajı işlenemedi:', error);
      }
    };

    const connect = () => {
      if (disposed) return;
      socket = new WebSocket(WS_URL);

      socket.onopen = () => {
        retryCount = 0;
        console.log('🟢 WebSocket bağlantısı kuruldu.');
        keepAliveTimer = setInterval(() => {
          if (socket?.readyState === WebSocket.OPEN) socket.send('ping');
        }, 20_000);
      };

      socket.onmessage = (event) => handleMessage(event.data);
      socket.onerror = (error) => console.error('🔴 WebSocket hatası:', error);
      socket.onclose = () => {
        if (keepAliveTimer) clearInterval(keepAliveTimer);
        keepAliveTimer = null;
        if (disposed) return;
        const delay = Math.min(30_000, 1_000 * 2 ** retryCount);
        retryCount += 1;
        reconnectTimer = setTimeout(connect, delay);
      };
    };

    void fetchDashboard();
    connect();
    const recoveryPoll = setInterval(fetchDashboard, 30_000);

    return () => {
      disposed = true;
      clearInterval(recoveryPoll);
      if (reconnectTimer) clearTimeout(reconnectTimer);
      if (keepAliveTimer) clearInterval(keepAliveTimer);
      socket?.close();
    };
  }, []);

  return data;
};
