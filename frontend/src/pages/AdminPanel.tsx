import { useState, useEffect } from 'react';
import { Settings, Cpu, Zap, Database, Flag, Square, Play, Save, Terminal } from 'lucide-react';
import { apiUrl } from '../lib/api';
import { Link } from 'react-router-dom';

export default function AdminPanel() {
  const [towers, setTowers] = useState<any[]>([]);
  const [isLogging, setIsLogging] = useState(false);
  const [checkpointNote, setCheckpointNote] = useState("");
  
  // 🔥 YENİ: override_temp state'e eklendi
  const [weights, setWeights] = useState({
    weight_delta_t: 0.40,
    weight_temp: 0.30,
    weight_gas: 0.30,
    override_temp: 90.0 
  });

  useEffect(() => {
    fetchData();
    const interval = setInterval(fetchData, 2000);
    return () => clearInterval(interval);
  }, []);

  const fetchData = async () => {
    try {
      const res = await fetch(apiUrl('/towers/live'));
      const data = await res.json();
      
      // 🛠️ ÇÖZÜM BURADA: Backend direkt [ {..}, {..} ] dizisi dönüyorsa onu al, 
      // obje dönüyorsa data.towers'ı al. Cihaz listesinin boş kalmasını engeller.
      const towersList = Array.isArray(data) ? data : (data.towers || []);
      setTowers(towersList);
      
      const [logRes, calibrationRes] = await Promise.all([
        fetch(apiUrl('/logger/status')),
        fetch(apiUrl('/settings/calibration')),
      ]);
      const logData = await logRes.json();
      setIsLogging(Boolean(logData.is_logging));

      if (calibrationRes.ok) {
        const calibration = await calibrationRes.json();
        setWeights({
          weight_delta_t: Number(calibration.weight_delta_t ?? 0.40),
          weight_temp: Number(calibration.weight_temp ?? 0.30),
          weight_gas: Number(calibration.weight_gas ?? 0.30),
          override_temp: Number(calibration.override_temp ?? 90.0),
        });
      }
    } catch (err) { console.error("Veri çekme hatası", err); }
  };

  const updateWeights = async () => {
    try {
      const response = await fetch(apiUrl('/settings/calibration'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(weights)
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const result = await response.json();
      setWeights(result.new_settings);
      alert("Algoritma ayarları başarıyla güncellendi!");
    } catch (err) { alert("Güncelleme başarısız!"); }
  };

  return (
    <div style={{ padding: '25px', backgroundColor: '#0f172a', minHeight: '100vh', color: '#f8fafc', fontFamily: 'sans-serif' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: '12px', marginBottom: '30px', borderBottom: '1px solid #1e293b', paddingBottom: '15px' }}>
        <Settings size={32} color="#3b82f6" />
        <h1 style={{ margin: 0, fontSize: '1.8rem' }}>Sistem Yönetim Merkezi (Admin)</h1>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px' }}>
        
        {/* 1. ALGORİTMA KALİBRASYONU */}
        <section style={{ backgroundColor: '#1e293b', padding: '20px', borderRadius: '12px', border: '1px solid #334155' }}>
          <h2 style={{ fontSize: '1.1rem', marginBottom: '20px', display: 'flex', alignItems: 'center', gap: '8px' }}>
            <Cpu color="#60a5fa" /> Algoritma Zeka Ağırlıkları
          </h2>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '15px' }}>
            <div>
              <label style={{ display: 'block', fontSize: '0.85rem', color: '#94a3b8', marginBottom: '5px' }}>Delta T (İvme) Ağırlığı: %{Math.round(weights.weight_delta_t * 100)}</label>
              <input type="range" min="0" max="1" step="0.05" value={weights.weight_delta_t} onChange={(e) => setWeights({...weights, weight_delta_t: parseFloat(e.target.value)})} style={{ width: '100%' }} />
            </div>
            <div>
              <label style={{ display: 'block', fontSize: '0.85rem', color: '#94a3b8', marginBottom: '5px' }}>Sıcaklık Ağırlığı: %{Math.round(weights.weight_temp * 100)}</label>
              <input type="range" min="0" max="1" step="0.05" value={weights.weight_temp} onChange={(e) => setWeights({...weights, weight_temp: parseFloat(e.target.value)})} style={{ width: '100%' }} />
            </div>
            <div>
              <label style={{ display: 'block', fontSize: '0.85rem', color: '#94a3b8', marginBottom: '5px' }}>Gaz Anomalisi Ağırlığı: %{Math.round(weights.weight_gas * 100)}</label>
              <input type="range" min="0" max="1" step="0.05" value={weights.weight_gas} onChange={(e) => setWeights({...weights, weight_gas: parseFloat(e.target.value)})} style={{ width: '100%' }} />
            </div>
            {/* 🔥 YENİ: OVERRIDE SICAKLIK GİRİŞİ */}
            <div style={{ backgroundColor: '#334155', padding: '10px', borderRadius: '8px', borderLeft: '4px solid #ef4444' }}>
              <label style={{ display: 'block', fontSize: '0.85rem', color: '#f8fafc', marginBottom: '5px', fontWeight: 'bold' }}>Mutlak Güvenlik Sınırı (Override °C)</label>
              <div style={{ display: 'flex', gap: '10px', alignItems: 'center' }}>
                <input 
                  type="number" 
                  value={weights.override_temp} 
                  onChange={(e) => setWeights({...weights, override_temp: parseFloat(e.target.value)})} 
                  style={{ width: '80px', padding: '8px', borderRadius: '4px', border: 'none', textAlign: 'center', fontWeight: 'bold' }} 
                />
                <span style={{ fontSize: '0.8rem', color: '#cbd5e1' }}>Bu dereceyi geçen her şey anında "KRİTİK" sayılır.</span>
              </div>
            </div>

            <button onClick={updateWeights} style={{ marginTop: '10px', padding: '12px', backgroundColor: '#3b82f6', color: 'white', border: 'none', borderRadius: '8px', cursor: 'pointer', fontWeight: 'bold', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '8px' }}>
              <Save size={18} /> AYARLARI KAYDET VE UYGULA
            </button>
          </div>
        </section>

        {/* 2. VERİ KAYDI */}
        <section style={{ backgroundColor: '#1e293b', padding: '20px', borderRadius: '12px', border: '1px solid #334155' }}>
          <h2 style={{ fontSize: '1.1rem', marginBottom: '20px', display: 'flex', alignItems: 'center', gap: '8px' }}>
            <Database color="#10b981" /> Veri Kaydı (CSV/Logger)
          </h2>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '15px' }}>
            <button onClick={() => fetch(apiUrl('/logger/toggle'), {method: 'POST'}).then(fetchData)} style={{ padding: '15px', backgroundColor: isLogging ? '#991b1b' : '#059669', color: 'white', border: 'none', borderRadius: '8px', fontWeight: 'bold', cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center', gap: '10px' }}>
              {isLogging ? <><Square size={20} fill="white" /> KAYDI DURDUR</> : <><Play size={20} fill="white" /> TEST KAYDINI BAŞLAT</>}
            </button>
            <div style={{ opacity: isLogging ? 1 : 0.5 }}>
              <label style={{ display: 'block', fontSize: '0.85rem', color: '#94a3b8', marginBottom: '5px' }}>Saha Notu (Checkpoint)</label>
              <div style={{ display: 'flex', gap: '10px' }}>
                <input value={checkpointNote} onChange={(e) => setCheckpointNote(e.target.value)} placeholder="Örn: 2. Kule Ateş Testi..." style={{ flex: 1, backgroundColor: '#0f172a', border: '1px solid #334155', borderRadius: '6px', padding: '10px', color: 'white' }} />
                <button onClick={() => { fetch(apiUrl('/logger/checkpoint'), {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({note:checkpointNote})}); setCheckpointNote(""); }} style={{ backgroundColor: '#f59e0b', border: 'none', borderRadius: '6px', padding: '0 15px', color: 'white' }}>
                  <Flag size={20} />
                </button>
              </div>
            </div>
          </div>
        </section>

        {/* 3. DONANIM KONTROLÜ */}
        <section style={{ backgroundColor: '#1e293b', padding: '20px', borderRadius: '12px', border: '1px solid #334155', gridColumn: 'span 2' }}>
          <h2 style={{ fontSize: '1.1rem', marginBottom: '20px', display: 'flex', alignItems: 'center', gap: '8px' }}>
            <Zap color="#f59e0b" /> Global Donanım Yönetimi
          </h2>
          <p style={{ fontSize: 13, color: '#94a3b8' }}>Toplu uyutma/reboot pilotta devre dışı. Yangın algılama her Nest üzerinde bağımsız kalır; müdahaleler hedef cihaz başına kimlik doğrulamalıdır.</p>
          <Link to="/control" style={{ display: 'inline-block', padding: 12, borderRadius: 8, background: '#3b82f6', color: 'white', textDecoration: 'none', fontWeight: 600 }}>Nest Kontrol paneli</Link>
        </section>

        {/* 4. KULE DURUM LİSTESİ */}
        <section style={{ backgroundColor: '#1e293b', padding: '20px', borderRadius: '12px', border: '1px solid #334155', gridColumn: 'span 2' }}>
          <h2 style={{ fontSize: '1.1rem', marginBottom: '15px', display: 'flex', alignItems: 'center', gap: '8px' }}>
            <Terminal color="#94a3b8" /> Aktif Cihaz Listesi ve IP Yönetimi
          </h2>
          <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: '0.9rem' }}>
            <thead>
              <tr style={{ color: '#94a3b8', textAlign: 'left', borderBottom: '1px solid #334155' }}>
                <th style={{ padding: '10px' }}>Cihaz ID</th>
                <th>IP Adresi</th>
                <th>Durum</th>
                <th>Pil</th>
                <th>İşlem</th>
              </tr>
            </thead>
            <tbody>
              {towers.length === 0 ? (
                <tr><td colSpan={5} style={{ padding: '20px', textAlign: 'center', color: '#64748b' }}>Henüz sisteme bağlanan kule yok veya veri bekleniyor...</td></tr>
              ) : (
                towers.map(t => (
                  <tr key={t.id} style={{ borderBottom: '1px solid #1e293b' }}>
                    <td style={{ padding: '12px', fontWeight: 'bold' }}>{t.name || t.id}</td>
                    {/* ESP32 IP göndermiyorsa Bilinmiyor yazsın */}
                    <td style={{ fontFamily: 'monospace', color: '#60a5fa' }}>{t.ip || 'Bilinmiyor'}</td>
                    <td><span style={{ color: t.is_online ? '#10b981' : '#ef4444' }}>● {t.is_online ? 'ONLINE' : 'OFFLINE'}</span></td>
                    <td>%{t.battery || 0}</td>
                    <td>
                      <Link to={`/control?target=${encodeURIComponent(t.id)}`} style={{ backgroundColor: 'transparent', border: '1px solid #60a5fa', color: '#60a5fa', padding: '4px 8px', borderRadius: '4px', textDecoration: 'none', fontSize: '0.75rem' }}>Kontrol</Link>
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </section>

      </div>
    </div>
  );
}
