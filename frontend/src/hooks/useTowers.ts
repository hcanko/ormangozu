import { useState, useEffect } from 'react';
import type { DashboardData } from '../types/index';

export const useTowers = () => {
  const [data, setData] = useState<DashboardData>({ 
    towers: [], 
    fireLocation: null 
  });

  useEffect(() => {
    // 1. ADIM: Sayfa ilk açıldığında haritayı doldurmak için mevcut durumu BİR KERE çekiyoruz
    const fetchInitialData = async () => {
      try {
        const response = await fetch('http://localhost:8000/api/towers/live');
        if (response.ok) {
          const result = await response.json();
          setData({
            towers: result.towers || [],
            fireLocation: result.fireLocation || null
          });
        }
      } catch (error) {
        console.error("Başlangıç verisi çekilemedi:", error);
      }
    };

    fetchInitialData();

    // 2. ADIM: WebSocket (Canlı Yayın) Bağlantısını Başlatıyoruz
    const ws = new WebSocket('ws://localhost:8000/api/ws');

    ws.onopen = () => {
      console.log("🟢 WebSocket Bağlantısı Kuruldu! Canlı veri akışı devrede.");
    };

    ws.onmessage = (event) => {
      try {
        const message = JSON.parse(event.data);

        // Backend'den gelen SENSOR_UPDATE veya FIRE_ALERT tipindeki mesajları yakala
if (message.type === 'SENSOR_UPDATE' || message.type === 'FIRE_ALERT') {
          setData(prevData => {
            const updatedTowers = prevData.towers.map(tower => {
              if (tower.id === message.tower_id) {
                return {
                  ...tower,
                  fire_score: message.fire_score,
                  status: message.status,
                  ...(message.pixels && { pixels: message.pixels }), // KAMERA GÖRÜNTÜSÜNÜ GÜNCELLE
                  currentData: {
                    ...tower.currentData,
                    avg_temp: message.avg_temp,
                    fire_score: message.fire_score,
                    status: message.status,
                    ...(message.gas_level !== undefined && { gas_level: message.gas_level }), // GAZ VERİSİNİ GÜNCELLE
                    ...(message.delta_t !== undefined && { delta_t: message.delta_t })
                  }
                };
              }
              return tower;
            });

            return {
              ...prevData,
              towers: updatedTowers
            };
          });
        }
      } catch (error) {
        console.error("WebSocket mesajı işlenirken hata:", error);
      }
    };

    ws.onerror = (error) => {
      console.error("🔴 WebSocket Hatası:", error);
    };

    ws.onclose = () => {
      console.log("⚪ WebSocket Bağlantısı Kapandı.");
    };

    // Component ekrandan kalktığında (başka sayfaya geçildiğinde) bağlantıyı temizle
    return () => {
      ws.close();
    };
  }, []);

  return data;
};