import { useState, useEffect } from 'react';
import { AlertTriangle, Clock, Thermometer, Wind, RefreshCw, Activity, ShieldCheck } from 'lucide-react';
import { apiUrl } from '../lib/api';

// 1. Arayüzü yeni backend verilerimize göre güncelledik
interface Alert {
  id: number;
  tower_id: string;
  timestamp: string;
  avg_temp?: number;
  max_temp?: number;
  delta_t?: number;
  gas_level: number;
  fire_score?: number;
  status: string;
}

export default function Alerts() {
  const [alerts, setAlerts] = useState<Alert[]>([]);
  const [loading, setLoading] = useState(true);

  const fetchAlerts = async () => {
    setLoading(true);
    try {
      // Son 50 alarmı getir
      const response = await fetch(apiUrl('/alerts?limit=50'));
      const data = await response.json();
      setAlerts(data);
    } catch (error) {
      console.error("Alarmlar çekilemedi:", error);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchAlerts();
  }, []);

  // Tarihi Türkiye saat dilimine ve okunabilir formata çeviren yardımcı fonksiyon
  const formatDate = (isoString: string) => {
    const date = new Date(isoString);
    return date.toLocaleString('tr-TR', {
      day: '2-digit', month: '2-digit', year: 'numeric',
      hour: '2-digit', minute: '2-digit', second: '2-digit'
    });
  };

  // 🛠️ ÇÖZÜM: Olası tanımsız veri çökmelerine karşı güvenli dizi
  const safeAlerts = alerts || [];

  return (
    <div style={{ padding: '30px', maxWidth: '1200px', margin: '0 auto', height: '100%', overflowY: 'auto' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '20px', borderBottom: '2px solid #e5e7eb', paddingBottom: '10px' }}>
        <h2 style={{ fontSize: '1.8rem', color: '#1f2937', display: 'flex', alignItems: 'center', gap: '10px', margin: 0 }}>
          <AlertTriangle size={28} color="#ef4444" /> Alarm Geçmişi (Loglar)
        </h2>
        <button onClick={fetchAlerts} style={{ padding: '8px 12px', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '5px', backgroundColor: '#e5e7eb', border: 'none', borderRadius: '6px' }}>
          <RefreshCw size={16} /> Yenile
        </button>
      </div>

      <div style={{ backgroundColor: 'white', padding: '20px', borderRadius: '8px', boxShadow: '0 4px 6px -1px rgba(0,0,0,0.1)' }}>
        {loading ? (
          <p>Kayıtlar yükleniyor...</p>
        ) : safeAlerts.length === 0 ? (
          <div style={{ textAlign: 'center', padding: '40px', color: '#6b7280' }}>
            <ShieldCheck size={48} color="#10b981" style={{ marginBottom: '10px' }} />
            <h3>Sistem Temiz</h3>
            <p>Geçmişe dönük herhangi bir yangın veya anomali kaydı bulunmuyor.</p>
          </div>
        ) : (
          <table style={{ width: '100%', borderCollapse: 'collapse', textAlign: 'left' }}>
            <thead>
              <tr style={{ borderBottom: '2px solid #e5e7eb', color: '#6b7280', backgroundColor: '#f9fafb' }}>
                <th style={{ padding: '12px' }}><Clock size={16} style={{ verticalAlign: 'middle', marginRight: '5px' }}/> Tarih / Saat</th>
                <th style={{ padding: '12px' }}>Direk ID</th>
                <th style={{ padding: '12px' }}><Thermometer size={16} style={{ verticalAlign: 'middle', marginRight: '5px' }}/> Sıcaklık & İvme</th>
                <th style={{ padding: '12px' }}><Wind size={16} style={{ verticalAlign: 'middle', marginRight: '5px' }}/> Gaz Direnci</th>
                <th style={{ padding: '12px' }}><Activity size={16} style={{ verticalAlign: 'middle', marginRight: '5px' }}/> Yangın Skoru</th>
                <th style={{ padding: '12px' }}>Karar / Durum</th>
              </tr>
            </thead>
            <tbody>
              {safeAlerts.map((alert) => {
                // Backend'den UYARI gelirse sarı, KRİTİK gelirse kırmızı rozet bas
                const isCritical = alert.status === "KRİTİK";
                const badgeColor = isCritical ? '#fee2e2' : '#fef3c7';
                const textColor = isCritical ? '#b91c1c' : '#d97706';
                
                return (
                  <tr key={alert.id} style={{ borderBottom: '1px solid #f3f4f6', backgroundColor: isCritical ? '#fef2f2' : '#fffbeb' }}>
                    <td style={{ padding: '12px', fontWeight: '500' }}>{formatDate(alert.timestamp)}</td>
                    <td style={{ padding: '12px', fontWeight: 'bold', color: '#1f2937' }}>{alert.tower_id}</td>
                    <td style={{ padding: '12px', color: '#ef4444', fontWeight: 'bold' }}>
                      {alert.avg_temp || alert.max_temp}°C 
                      <span style={{ fontSize: '0.8rem', color: '#6b7280', marginLeft: '5px' }}>
                        (ΔT: {alert.delta_t || 0})
                      </span>
                    </td>
                    <td style={{ padding: '12px', color: '#d97706', fontWeight: 'bold' }}>{alert.gas_level}</td>
                    <td style={{ padding: '12px', fontWeight: 'bold' }}>
                      %{alert.fire_score || 0}
                    </td>
                    <td style={{ padding: '12px' }}>
                      <span style={{ padding: '4px 8px', backgroundColor: badgeColor, color: textColor, borderRadius: '999px', fontSize: '0.85rem', fontWeight: 'bold' }}>
                        {alert.status || "ALARM"}
                      </span>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        )}
      </div>
    </div>
  );
}
