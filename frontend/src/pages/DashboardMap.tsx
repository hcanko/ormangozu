import React, { useState, useEffect, useRef } from 'react';
import { MapContainer, TileLayer, Marker, Popup, Circle } from 'react-leaflet'; // 🔥 EKLENDİ: Circle (Çember)
import 'leaflet/dist/leaflet.css';
import L from 'leaflet';
import { useTowers } from '../hooks/useTowers';
import { Battery, Wifi, WifiOff, Thermometer, Wind, Activity, X, Database, Square, Flag, Radar } from 'lucide-react'; // 🔥 EKLENDİ: Radar ikonu
import { apiUrl } from '../lib/api';
import { useNavigate } from 'react-router-dom';

// Vite uyumlu Leaflet İkon Düzeltmesi
import iconRetinaUrl from 'leaflet/dist/images/marker-icon-2x.png';
import iconUrl from 'leaflet/dist/images/marker-icon.png';
import shadowUrl from 'leaflet/dist/images/marker-shadow.png';

delete (L.Icon.Default.prototype as any)._getIconUrl;
L.Icon.Default.mergeOptions({
  iconRetinaUrl: iconRetinaUrl,
  iconUrl: iconUrl,
  shadowUrl: shadowUrl,
});

const fireIcon = new L.Icon({
  iconUrl: 'https://raw.githubusercontent.com/pointhi/leaflet-color-markers/master/img/marker-icon-2x-red.png',
  shadowUrl: 'https://cdnjs.cloudflare.com/ajax/libs/leaflet/0.7.7/images/marker-shadow.png',
  iconSize: [25, 41],
  iconAnchor: [12, 41],
  popupAnchor: [1, -34],
});

export default function DashboardMap() {
  const navigate = useNavigate();
  const { towers, fireLocation } = useTowers();
  const [selectedTowerId, setSelectedTowerId] = useState<string | null>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  
  // Logger & Checkpoint State'leri
  const [isLogging, setIsLogging] = useState(false);
  const [checkpointNote, setCheckpointNote] = useState("");

  // 🔥 YENİ: Kapsama alanı çemberlerini açıp kapatan state
  const [showCoverage, setShowCoverage] = useState(false);

  // 🛠️ ÇÖZÜM BURADA: towers undefined ise boş dizi ([]) kabul et
  const safeTowers = towers || []; 
  const selectedTower = safeTowers.find((t: any) => t.id === selectedTowerId);

  // Sayfa açıldığında Logger durumunu kontrol et
  useEffect(() => {
    fetch(apiUrl('/logger/status'))
      .then(res => res.json())
      .then(data => setIsLogging(data.is_logging))
      .catch(console.error);
  }, []);

  // Termal Çizim (İnterpolasyon / Yumuşatma İşlemi)
  useEffect(() => {
    if (selectedTower && canvasRef.current) {
      const canvas = canvasRef.current;
      const ctx = canvas.getContext('2d');
      if (!ctx) return;

      const pixels = (selectedTower.pixels && selectedTower.pixels.length === 768) 
        ? selectedTower.pixels 
        : Array.from({length: 768}, () => 0);

      // 1. Arka planda 32x24'lük minik bir "Görünmez Tuval" oluştur
      const offCanvas = document.createElement('canvas');
      offCanvas.width = 32;
      offCanvas.height = 24;
      const offCtx = offCanvas.getContext('2d');
      if (!offCtx) return;

      // 2. Piksel verisini (ImageData) hazırla
      const imgData = offCtx.createImageData(32, 24);

      pixels.forEach((temp: number, i: number) => {
        let r=0, g=0, b=255; // Varsayılan: Mavi (Soğuk)
        
        if (temp > 50) { r=255; g=255; b=255; }      // 50+ Beyaz (Aşırı Sıcak)
        else if (temp > 40) { r=255; g=0; b=0; }     // 40-50 Kırmızı (Yangın)
        else if (temp > 32) { r=255; g=165; b=0; }   // 32-40 Turuncu (Canlı Isısı)
        else if (temp > 25) { r=255; g=255; b=0; }   // 25-32 Sarı (Ilık)
        else if (temp > 15) { r=0; g=255; b=0; }     // 15-25 Yeşil (Oda Sıcaklığı)

        const idx = i * 4;
        imgData.data[idx] = r;
        imgData.data[idx+1] = g;
        imgData.data[idx+2] = b;
        imgData.data[idx+3] = 255; // Opaklık
      });

      offCtx.putImageData(imgData, 0, 0);

      // 3. PÜRÜZSÜZLEŞTİREREK ANA EKRANA ÇİZ
      ctx.imageSmoothingEnabled = true;
      ctx.imageSmoothingQuality = 'high';
      ctx.clearRect(0, 0, canvas.width, canvas.height);
      // Minik resmi büyük ekrana sündür (Tarayıcı otomatik yumuşatır)
      ctx.drawImage(offCanvas, 0, 0, canvas.width, canvas.height);
    }
  }, [selectedTower]);

  // API Fonksiyonları
  const toggleLogger = async () => {
    try {
      const res = await fetch(apiUrl('/logger/toggle'), { method: 'POST' });
      const data = await res.json();
      setIsLogging(data.is_logging);
    } catch (err) { alert("Logger bağlantı hatası!"); }
  };

  const markCheckpoint = async (note: string) => {
    if (!note) return;
    try {
      await fetch(apiUrl('/logger/checkpoint'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ note })
      });
      setCheckpointNote(""); 
    } catch (err) { console.error("Checkpoint hatası"); }
  };


  return (
    <div style={{ display: 'flex', height: '100%', width: '100%', position: 'relative' }}>
      
      {/* 🔥 YENİ: Harita üzerinde süzülen "Global Kontrol" Butonları */}
      <div style={{ position: 'absolute', top: '20px', right: '20px', zIndex: 1000 }}>
        <button 
          onClick={() => setShowCoverage(!showCoverage)}
          style={{
            padding: '10px 15px',
            backgroundColor: showCoverage ? '#3b82f6' : 'white',
            color: showCoverage ? 'white' : '#374151',
            border: showCoverage ? 'none' : '1px solid #d1d5db',
            borderRadius: '8px',
            boxShadow: '0 4px 6px rgba(0,0,0,0.1)',
            cursor: 'pointer',
            display: 'flex',
            alignItems: 'center',
            gap: '8px',
            fontWeight: 'bold',
            transition: 'all 0.2s'
          }}
        >
          <Radar size={20} />
          {showCoverage ? 'Kapsama Alanı: AÇIK' : 'Kapsama Alanı: KAPALI'}
        </button>
      </div>

      <div style={{ flex: 1, height: '100%' }}>
        <MapContainer center={[38.4237, 27.1428]} zoom={12} style={{ height: '100%', width: '100%', zIndex: 1 }}>
          <TileLayer url="https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png" />
          
          {/* 🛠️ ÇÖZÜM: safeTowers kullanarak render alıyoruz */}
          {safeTowers.map((tower: any) => (
            <React.Fragment key={tower.id}>
              {/* 🔥 YENİ: Kapsama Alanı Çemberi (Sadece showCoverage true ise çizilir) */}
              {showCoverage && (
                <Circle 
                  center={[tower.lat, tower.lng]} 
                  radius={500} // 500 Metre yarıçaplı kapsama
                  pathOptions={{ color: '#3b82f6', fillColor: '#3b82f6', fillOpacity: 0.1, weight: 1 }} 
                />
              )}
              
              <Marker position={[tower.lat, tower.lng]} eventHandlers={{ click: () => setSelectedTowerId(tower.id) }}>
                <Popup><strong>{tower.name}</strong><br/>{tower.is_online ? '🟢 Online' : '🔴 Offline'}</Popup>
              </Marker>
            </React.Fragment>
          ))}
          
          {fireLocation && (
            <Marker position={[fireLocation.lat, fireLocation.lng]} icon={fireIcon}>
              <Popup><strong style={{ color: 'red' }}>🔥 YANGIN NOKTASI</strong></Popup>
            </Marker>
          )}
        </MapContainer>
      </div>

      {selectedTower && (
        <div style={{ width: '370px', backgroundColor: 'white', borderLeft: '1px solid #e5e7eb', display: 'flex', flexDirection: 'column', padding: '20px', boxShadow: '-4px 0 15px rgba(0,0,0,0.1)', zIndex: 1000, overflowY: 'auto' }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '2px solid #f3f4f6', paddingBottom: '10px', marginBottom: '15px' }}>
            <h2 style={{ margin: 0, fontSize: '1.2rem' }}>{selectedTower.name}</h2>
            <button onClick={() => setSelectedTowerId(null)} style={{ border: 'none', background: 'none', cursor: 'pointer' }}><X color="#6b7280" /></button>
          </div>

          <div style={{ display: 'flex', justifyContent: 'space-around', marginBottom: '15px' }}>
            <div style={{ textAlign: 'center' }}>
              {selectedTower.is_online ? <Wifi color="#10b981" /> : <WifiOff color="#ef4444" />}
              <div style={{ fontSize: '0.75rem', color: '#6b7280' }}>Bağlantı</div>
            </div>
            <div style={{ textAlign: 'center' }}>
              <Battery color={selectedTower.battery > 20 ? "#10b981" : "#ef4444"} />
              <div style={{ fontSize: '0.75rem', color: '#6b7280' }}>%{selectedTower.battery}</div>
            </div>
          </div>

          <div style={{ backgroundColor: '#f9fafb', padding: '15px', borderRadius: '8px', marginBottom: '15px' }}>
            <div style={{ display: 'flex', alignItems: 'center', marginBottom: '10px', color: '#ef4444', fontWeight: 'bold' }}>
              <Thermometer size={18} style={{ marginRight: '8px' }} /> Sıcaklık: {selectedTower.currentData?.avg_temp?.toFixed(1) || 0}°C
            </div>
            <div style={{ display: 'flex', alignItems: 'center', color: '#d97706', fontWeight: 'bold' }}>
              {/* 🔥 TEK DÜZELTİLEN YER BURASI: gas_level yerine gas_ppm_impact, ppm yerine % */}
              <Wind size={18} style={{ marginRight: '8px' }} /> Duman Anomalisi: %{selectedTower.currentData?.gas_ppm_impact?.toFixed(1) || 0}
            </div>
          </div>

          <div style={{ marginBottom: '15px' }}>
            <h3 style={{ fontSize: '1rem', color: '#374151', display: 'flex', alignItems: 'center', gap: '5px' }}>
              <Activity size={18} /> Canlı Önizleme (Smooth)
            </h3>
            <div style={{ backgroundColor: 'black', borderRadius: '8px', overflow: 'hidden', padding: '5px' }}>
              <canvas ref={canvasRef} width={320} height={240} style={{ width: '100%', height: 'auto', display: 'block' }} />
            </div>
          </div>

          <h3 style={{ fontSize: '1rem', color: '#374151', marginTop: '10px', marginBottom: '10px' }}>Veri Kaydı & Test</h3>
          
          <button 
            onClick={toggleLogger} 
            style={{ width: '100%', padding: '12px', backgroundColor: isLogging ? '#ef4444' : '#10b981', color: 'white', border: 'none', borderRadius: '6px', cursor: 'pointer', marginBottom: '10px', display: 'flex', justifyContent: 'center', alignItems: 'center', gap: '8px', fontWeight: 'bold' }}>
            {isLogging ? <><Square size={18} fill="currentColor" /> Kaydı Durdur (CSV Yazılıyor...)</> : <><Database size={18} /> Test Kaydını Başlat (Excel/CSV)</>}
          </button>

          {isLogging && (
            <div style={{ backgroundColor: '#fffbeb', border: '1px solid #fcd34d', padding: '12px', borderRadius: '6px', marginBottom: '15px' }}>
              <div style={{ fontSize: '0.8rem', color: '#b45309', marginBottom: '8px', fontWeight: 'bold' }}>📍 Checkpoint İşaretle</div>
              <div style={{ display: 'flex', gap: '5px' }}>
                <input 
                  value={checkpointNote} 
                  onChange={e => setCheckpointNote(e.target.value)} 
                  onKeyDown={e => e.key === 'Enter' && markCheckpoint(checkpointNote)}
                  placeholder="Örn: Ateş başlatıldı..." 
                  style={{ flex: 1, padding: '8px', fontSize: '0.85rem', borderRadius: '4px', border: '1px solid #d1d5db' }} 
                />
                <button 
                  onClick={() => markCheckpoint(checkpointNote)} 
                  style={{ padding: '8px 12px', backgroundColor: '#f59e0b', color: 'white', border: 'none', borderRadius: '4px', cursor: 'pointer' }}>
                  <Flag size={16} />
                </button>
              </div>
            </div>
          )}

          <h3 style={{ fontSize: '1rem', color: '#374151', marginTop: '10px' }}>Adresli Nest müdahalesi</h3>
          <p style={{ fontSize: '0.85rem', color: '#64748b' }}>Eski IP üzerinden kontrol devre dışı. Ölçüm, hızlı tarama ve servo (takılıysa) için kimlik doğrulamalı kontrol ekranını kullan.</p>
          <button onClick={() => navigate(`/control?target=${encodeURIComponent(selectedTower.id)}`)}
            style={{ width: '100%', padding: '11px', backgroundColor: '#3b82f6', color: 'white', border: 'none', borderRadius: '6px', cursor: 'pointer', display: 'flex', gap: 8, alignItems: 'center', justifyContent: 'center' }}>
            <Activity size={18} /> Seçili Nest'i kontrol et
          </button>
        </div>
      )}
    </div>
  );
}
