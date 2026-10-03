import { useState, useEffect } from 'react';
import { useTowers } from '../hooks/useTowers';
import { Flame, Wind, Activity, Zap, CheckCircle, AlertTriangle, Database, Square, Flag, Eye } from 'lucide-react';

export default function MobileTest() {
  const { towers } = useTowers();
  const safeTowers = towers || [];

  const [isLogging, setIsLogging] = useState(false);
  const [checkpointNote, setCheckpointNote] = useState("");

  useEffect(() => {
    fetch('http://localhost:8000/api/logger/status')
      .then(res => res.json())
      .then(data => setIsLogging(data.is_logging))
      .catch(console.error);
  }, []);

  const toggleLogger = async () => {
    try {
      const res = await fetch('http://localhost:8000/api/logger/toggle', { method: 'POST' });
      const data = await res.json();
      setIsLogging(data.is_logging);
    } catch (err) { alert("Logger bağlantı hatası!"); }
  };

  const markCheckpoint = async () => {
    if (!checkpointNote) return;
    try {
      await fetch('http://localhost:8000/api/logger/checkpoint', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ note: checkpointNote })
      });
      setCheckpointNote("");
      alert("Nokta işaretlendi: " + checkpointNote);
    } catch (err) { alert("Hata oluştu!"); }
  };

  return (
    <div style={{ padding: '15px', backgroundColor: '#0f172a', minHeight: '100vh', color: 'white' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: '10px', marginBottom: '20px' }}>
        <Activity color="#60a5fa" />
        <h2 style={{ margin: 0, fontSize: '1.2rem' }}>Mobil Saha Test Paneli</h2>
      </div>

      <div style={{ 
        display: 'grid', 
        gridTemplateColumns: 'repeat(auto-fit, minmax(340px, 1fr))', 
        gap: '15px', 
        marginBottom: '30px' 
      }}>
        {safeTowers.map((tower: any) => {
          const isCritical = tower.status === 'KRİTİK' || tower.currentData?.status === 'KRİTİK';
          const isSwarmActive = tower.sleep_interval < 500;

          return (
            <div key={tower.id} style={{ 
              backgroundColor: '#1e293b', 
              borderRadius: '12px', 
              padding: '15px', 
              border: isCritical ? '2px solid #ef4444' : '1px solid #334155',
              boxShadow: isCritical ? '0 0 15px rgba(239, 68, 68, 0.3)' : 'none',
              transition: 'all 0.3s ease'
            }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '12px' }}>
                <div style={{ fontWeight: 'bold', fontSize: '1.1rem' }}>{tower.name}</div>
                <div style={{ 
                  padding: '4px 8px', 
                  borderRadius: '6px', 
                  fontSize: '0.75rem', 
                  backgroundColor: tower.is_online ? '#065f46' : '#7f1d1d',
                  color: tower.is_online ? '#34d399' : '#f87171'
                }}>
                  {tower.is_online ? 'BAĞLI' : 'KOPUK'}
                </div>
              </div>

              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: '10px', marginBottom: '12px' }}>
                
                {/* 🔋 PİL KUTUSU */}
                <div style={{ backgroundColor: 'rgba(0,0,0,0.3)', padding: '8px', borderRadius: '6px', textAlign: 'center' }}>
                  <Zap size={16} color="#10b981" style={{ margin: '0 auto 2px auto' }} />
                  <div style={{ fontSize: '1.1rem', fontWeight: 'bold' }}>%{tower.battery || 0}</div>
                </div>
                
                {/* 🔥 SICAKLIK KUTUSU */}
                <div style={{ backgroundColor: 'rgba(0,0,0,0.3)', padding: '8px', borderRadius: '6px', textAlign: 'center' }}>
                  <Flame size={16} color="#f87171" style={{ margin: '0 auto 2px auto' }} />
                  {/* İşlenmiş Sıcaklık */}
                  <div style={{ fontSize: '1.1rem', fontWeight: 'bold' }}>
                    {(tower.currentData?.avg_temp || tower.avg_temp || 0).toFixed(1)}°
                  </div>
                  {/* Ham Sensör Sıcaklığı */}
                  <div style={{ fontSize: '0.65rem', color: '#94a3b8', marginTop: '2px', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '2px' }}>
                    <Eye size={10} /> Ham: {(tower.currentData?.max_temp || tower.max_temp || 0).toFixed(1)}°
                  </div>
                </div>

                {/* 💨 GAZ KUTUSU (GÜNCELLENDİ) */}
                <div style={{ backgroundColor: 'rgba(0,0,0,0.3)', padding: '8px', borderRadius: '6px', textAlign: 'center' }}>
                  <Wind size={16} color="#fbbf24" style={{ margin: '0 auto 2px auto' }} />
                  {/* Duman Anomalisi (%) */}
                  <div style={{ fontSize: '1.1rem', fontWeight: 'bold' }}>
                    %{(tower.currentData?.gas_ppm_impact || tower.gas_ppm_impact || 0).toFixed(1)}
                  </div>
                  {/* Ham Sensör Gaz Direnci */}
                  <div style={{ fontSize: '0.65rem', color: '#94a3b8', marginTop: '2px', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '2px' }}>
                    <Eye size={10} /> Ham: {((tower.currentData?.gas_level || tower.gas_raw_resistance || 0) / 1000).toFixed(1)}kΩ
                  </div>
                </div>
              </div>

              {/* 🧠 ZEKA SKORU */}
              <div style={{ textAlign: 'center', fontSize: '0.75rem', color: '#64748b', marginBottom: '8px' }}>
                Zeka Skoru: <span style={{ color: '#f8fafc', fontWeight: 'bold' }}>%{(tower.currentData?.fire_score || tower.fire_score || 0).toFixed(0)}</span>
              </div>

              {/* 🚀 SWARM DURUMU */}
              <div style={{ 
                display: 'flex', 
                alignItems: 'center', 
                justifyContent: 'center', 
                gap: '6px', 
                padding: '6px', 
                backgroundColor: isSwarmActive ? '#1e3a8a' : 'rgba(0,0,0,0.2)', 
                borderRadius: '6px', 
                fontSize: '0.8rem', 
                fontWeight: 'bold', 
                color: isSwarmActive ? '#93c5fd' : '#94a3b8' 
              }}>
                {isSwarmActive ? <AlertTriangle size={14} /> : <CheckCircle size={14} />}
                {isSwarmActive ? 'SWARM MODU (HIZLI AKIŞ)' : 'STANDART MOD'}
              </div>
            </div>
          );
        })}
      </div>

      {/* 💾 LOGGER KONTROL */}
      <div style={{ backgroundColor: '#1e293b', borderRadius: '12px', padding: '20px', border: '1px solid #334155' }}>
        <h3 style={{ margin: '0 0 15px 0', fontSize: '1rem', display: 'flex', alignItems: 'center', gap: '8px' }}>
          <Database size={18} color="#60a5fa" /> Test Veri Kaydı (CSV)
        </h3>
        
        <button 
          onClick={toggleLogger}
          style={{ 
            width: '100%', 
            padding: '15px', 
            borderRadius: '10px', 
            border: 'none', 
            backgroundColor: isLogging ? '#991b1b' : '#059669',
            color: 'white',
            fontWeight: 'bold',
            fontSize: '1rem',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            gap: '10px',
            marginBottom: '15px'
          }}
        >
          {isLogging ? <><Square size={20} fill="white" /> KAYDI DURDUR</> : <><Activity size={20} /> KAYDI BAŞLAT</>}
        </button>

        {isLogging && (
          <div style={{ animation: 'pulse 2s infinite' }}>
            <div style={{ fontSize: '0.8rem', color: '#94a3b8', marginBottom: '8px' }}>📍 Olay İşaretle (Checkpoint)</div>
            <div style={{ display: 'flex', gap: '8px' }}>
              <input 
                value={checkpointNote}
                onChange={(e) => setCheckpointNote(e.target.value)}
                onKeyDown={(e) => e.key === 'Enter' && markCheckpoint()}
                placeholder="Örn: Çakmak yakıldı..."
                style={{ 
                  flex: 1, 
                  backgroundColor: '#0f172a', 
                  border: '1px solid #334155', 
                  borderRadius: '8px', 
                  padding: '12px', 
                  color: 'white' 
                }}
              />
              <button 
                onClick={markCheckpoint}
                style={{ 
                  backgroundColor: '#f59e0b', 
                  border: 'none', 
                  borderRadius: '8px', 
                  padding: '0 15px', 
                  color: 'white' 
                }}
              >
                <Flag size={20} />
              </button>
            </div>
          </div>
        )}
      </div>

      <style>{`
        @keyframes pulse {
          0% { opacity: 1; }
          50% { opacity: 0.7; }
          100% { opacity: 1; }
        }
      `}</style>
    </div>
  );
}
