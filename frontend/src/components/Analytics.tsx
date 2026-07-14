import { useState, useEffect } from 'react';
import { Activity, RefreshCw, ChevronDown } from 'lucide-react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, Legend, ResponsiveContainer, AreaChart, Area } from 'recharts';

interface Tower {
  id: string;
  name: string;
}

export default function Analytics() {
  const [towers, setTowers] = useState<Tower[]>([]);
  const [selectedTower, setSelectedTower] = useState<string>('');
  const [historyData, setHistoryData] = useState<any[]>([]);
  const [loading, setLoading] = useState(false);

  // Önce sistemdeki direklerin listesini çekiyoruz ki Dropdown (seçim) menüsüne koyalım
  useEffect(() => {
    const fetchTowers = async () => {
      try {
        const response = await fetch('http://localhost:8000/api/towers');
        const data = await response.json();
        
        // 🛠️ ÇÖZÜM: Backend'den saf dizi (Array) veya obje gelme ihtimaline karşı güvenli kontrol
        const towerList = Array.isArray(data) ? data : (data.towers || []);
        
        if (towerList.length > 0) {
          setTowers(towerList);
          setSelectedTower(towerList[0].id); // İlk direği varsayılan seç
        }
      } catch (error) {
        console.error("Direkler çekilemedi:", error);
      }
    };
    fetchTowers();
  }, []);

  // Seçilen direk değiştiğinde veya yenile dendiğinde o direğin geçmişini çek
  const fetchHistory = async () => {
    if (!selectedTower) return;
    setLoading(true);
    try {
      const response = await fetch(`http://localhost:8000/api/towers/${selectedTower}/history?limit=30`);
      const data = await response.json();
      setHistoryData(data || []); // 🛠️ ÇÖZÜM: Boş gelirse diziye çevir
    } catch (error) {
      console.error("Geçmiş veri çekilemedi:", error);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchHistory();
  }, [selectedTower]);

  // Güvenli dizi tanımları
  const safeTowers = towers || [];
  const safeHistory = historyData || [];

  return (
    <div style={{ padding: '30px', maxWidth: '1200px', margin: '0 auto', height: '100%', overflowY: 'auto' }}>
      
      {/* ÜST BİLGİ VE KONTROL ÇUBUĞU */}
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '20px', borderBottom: '2px solid #e5e7eb', paddingBottom: '10px' }}>
        <h2 style={{ fontSize: '1.8rem', color: '#1f2937', display: 'flex', alignItems: 'center', gap: '10px', margin: 0 }}>
          <Activity size={28} color="#8b5cf6" /> Sistem Analitiği
        </h2>
        
        <div style={{ display: 'flex', gap: '15px', alignItems: 'center' }}>
          <div style={{ position: 'relative' }}>
            <select 
              value={selectedTower} 
              onChange={(e) => setSelectedTower(e.target.value)}
              style={{ appearance: 'none', padding: '8px 35px 8px 15px', borderRadius: '6px', border: '1px solid #d1d5db', backgroundColor: 'white', fontSize: '1rem', cursor: 'pointer', outline: 'none' }}
            >
              {safeTowers.map(t => (
                <option key={t.id} value={t.id}>{t.name} ({t.id})</option>
              ))}
            </select>
            <ChevronDown size={16} color="#6b7280" style={{ position: 'absolute', right: '10px', top: '10px', pointerEvents: 'none' }} />
          </div>

          <button onClick={fetchHistory} style={{ padding: '8px 12px', cursor: 'pointer', display: 'flex', alignItems: 'center', gap: '5px', backgroundColor: '#e5e7eb', border: 'none', borderRadius: '6px' }}>
            <RefreshCw size={16} /> Yenile
          </button>
        </div>
      </div>

      {loading ? (
        <p>Veriler işleniyor...</p>
      ) : safeHistory.length === 0 ? (
        <p style={{ color: '#6b7280' }}>Bu direğe ait henüz sensör kaydı bulunmuyor.</p>
      ) : (
        <div style={{ display: 'flex', flexDirection: 'column', gap: '30px' }}>
          
          {/* SICAKLIK GRAFİĞİ */}
          <div style={{ backgroundColor: 'white', padding: '20px', borderRadius: '8px', boxShadow: '0 4px 6px -1px rgba(0,0,0,0.1)' }}>
            <h3 style={{ marginTop: 0, marginBottom: '20px', color: '#374151' }}>🔥 Ortalama Sıcaklık Trendi (°C)</h3>
            <div style={{ width: '100%', height: 300 }}>
              <ResponsiveContainer>
                <AreaChart data={safeHistory} margin={{ top: 10, right: 30, left: 0, bottom: 0 }}>
                  <defs>
                    <linearGradient id="colorTemp" x1="0" y1="0" x2="0" y2="1">
                      <stop offset="5%" stopColor="#ef4444" stopOpacity={0.8}/>
                      <stop offset="95%" stopColor="#ef4444" stopOpacity={0}/>
                    </linearGradient>
                  </defs>
                  <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#e5e7eb" />
                  <XAxis dataKey="time" stroke="#6b7280" fontSize={12} tickMargin={10} />
                  <YAxis stroke="#6b7280" fontSize={12} />
                  <Tooltip contentStyle={{ borderRadius: '8px', border: 'none', boxShadow: '0 4px 6px -1px rgba(0,0,0,0.1)' }} />
                  <Legend />
                  <Area type="monotone" dataKey="temp" name="Sıcaklık (°C)" stroke="#ef4444" strokeWidth={3} fillOpacity={1} fill="url(#colorTemp)" />
                </AreaChart>
              </ResponsiveContainer>
            </div>
          </div>

          {/* GAZ/DUMAN SEVİYESİ GRAFİĞİ */}
          <div style={{ backgroundColor: 'white', padding: '20px', borderRadius: '8px', boxShadow: '0 4px 6px -1px rgba(0,0,0,0.1)' }}>
            <h3 style={{ marginTop: 0, marginBottom: '20px', color: '#374151' }}>💨 Gaz ve Partikül Seviyesi (ppm)</h3>
            <div style={{ width: '100%', height: 300 }}>
              <ResponsiveContainer>
                <LineChart data={safeHistory} margin={{ top: 10, right: 30, left: 0, bottom: 0 }}>
                  <CartesianGrid strokeDasharray="3 3" vertical={false} stroke="#e5e7eb" />
                  <XAxis dataKey="time" stroke="#6b7280" fontSize={12} tickMargin={10} />
                  <YAxis stroke="#6b7280" fontSize={12} />
                  <Tooltip contentStyle={{ borderRadius: '8px', border: 'none', boxShadow: '0 4px 6px -1px rgba(0,0,0,0.1)' }} />
                  <Legend />
                  <Line type="monotone" dataKey="gas" name="Gaz (ppm)" stroke="#d97706" strokeWidth={3} dot={{ r: 4, fill: '#d97706', strokeWidth: 0 }} activeDot={{ r: 8 }} />
                </LineChart>
              </ResponsiveContainer>
            </div>
          </div>

        </div>
      )}
    </div>
  );
}