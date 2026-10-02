import React, { useState } from 'react';
import { useTowers } from '../hooks/useTowers';
import { Plus, Trash2, Globe, MapPin, Battery, Wifi, RefreshCw } from 'lucide-react';
import { apiUrl } from '../lib/api';

export default function Towers() {
  const { towers } = useTowers();
  const [newTower, setNewTower] = useState({ id: '', name: '', ip: '', lat: 38.42, lng: 27.14, bearing: 0 });

  // 🛠️ ÇÖZÜM: towers undefined ise boş dizi ([]) kabul et
  const safeTowers = towers || [];

  const handleAdd = async (e: React.FormEvent) => {
    e.preventDefault();
    try {
      const res = await fetch(apiUrl('/towers'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ...newTower, sleep_interval: 2 })
      });
      if (res.ok) {
        alert("Direk başarıyla eklendi!");
        setNewTower({ id: '', name: '', ip: '', lat: 38.42, lng: 27.14, bearing: 0 });
      } else {
        const err = await res.json();
        alert("Hata: " + err.detail);
      }
    } catch (err) {
      alert("Sunucuya bağlanılamadı!");
    }
  };

  const handleDelete = async (id: string) => {
    if (!window.confirm(`${id} ID'li direği silmek istediğine emin misin?`)) return;
    try {
      await fetch(apiUrl(`/towers/${id}`), { method: 'DELETE' });
    } catch (err) {
      alert("Silme işlemi başarısız.");
    }
  };

  return (
    <div style={{ padding: '30px', height: '100%', overflowY: 'auto' }}>
      <h2 style={{ fontSize: '1.8rem', color: '#1f2937', marginBottom: '20px' }}>🗼 Direk Yönetimi</h2>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 350px', gap: '30px' }}>
        
        {/* SOL: MEVCUT DİREKLER LİSTESİ */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: '15px' }}>
          {/* 🛠️ ÇÖZÜM: safeTowers kullanıldı */}
          {safeTowers.length === 0 && <p style={{ color: '#6b7280' }}>Sistemde kayıtlı direk bulunamadı.</p>}
          {safeTowers.map((tower: any) => (
            <div key={tower.id} style={{ backgroundColor: 'white', padding: '20px', borderRadius: '12px', boxShadow: '0 2px 4px rgba(0,0,0,0.05)', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
              <div>
                <h3 style={{ margin: '0 0 10px 0', display: 'flex', alignItems: 'center', gap: '10px' }}>
                  {tower.name} <span style={{ fontSize: '0.8rem', color: '#9ca3af' }}>#{tower.id}</span>
                  {tower.is_online ? <span style={{ fontSize: '0.7rem', color: '#10b981', backgroundColor: '#d1fae5', padding: '2px 8px', borderRadius: '10px' }}>ONLINE</span> : <span style={{ fontSize: '0.7rem', color: '#ef4444', backgroundColor: '#fee2e2', padding: '2px 8px', borderRadius: '10px' }}>OFFLINE</span>}
                </h3>
                <div style={{ display: 'flex', gap: '20px', fontSize: '0.9rem', color: '#4b5563' }}>
                  <span style={{ display: 'flex', alignItems: 'center', gap: '5px' }}><Globe size={16}/> {tower.ip}</span>
                  <span style={{ display: 'flex', alignItems: 'center', gap: '5px' }}><MapPin size={16}/> {tower.lat.toFixed(4)}, {tower.lng.toFixed(4)}</span>
                  <span style={{ display: 'flex', alignItems: 'center', gap: '5px' }}><Battery size={16}/> %{tower.battery}</span>
                </div>
              </div>
              <button onClick={() => handleDelete(tower.id)} style={{ padding: '10px', backgroundColor: '#fee2e2', color: '#ef4444', border: 'none', borderRadius: '8px', cursor: 'pointer' }}>
                <Trash2 size={20} />
              </button>
            </div>
          ))}
        </div>

        {/* SAĞ: YENİ DİREK EKLEME FORMU */}
        <form onSubmit={handleAdd} style={{ backgroundColor: 'white', padding: '25px', borderRadius: '12px', boxShadow: '0 4px 6px rgba(0,0,0,0.1)', height: 'fit-content' }}>
          <h3 style={{ margin: '0 0 20px 0', fontSize: '1.1rem' }}>Yeni Direk Kaydet</h3>
          
          <div style={{ marginBottom: '15px' }}>
            <label style={{ display: 'block', fontSize: '0.85rem', marginBottom: '5px', color: '#6b7280' }}>Direk ID (Örn: T1)</label>
            <input required value={newTower.id} onChange={e => setNewTower({...newTower, id: e.target.value})} style={{ width: '100%', padding: '10px', borderRadius: '6px', border: '1px solid #e5e7eb' }} />
          </div>

          <div style={{ marginBottom: '15px' }}>
            <label style={{ display: 'block', fontSize: '0.85rem', marginBottom: '5px', color: '#6b7280' }}>Direk Adı</label>
            <input required value={newTower.name} onChange={e => setNewTower({...newTower, name: e.target.value})} style={{ width: '100%', padding: '10px', borderRadius: '6px', border: '1px solid #e5e7eb' }} />
          </div>

          <div style={{ marginBottom: '15px' }}>
            <label style={{ display: 'block', fontSize: '0.85rem', marginBottom: '5px', color: '#6b7280' }}>IP Adresi (Heltec IP)</label>
            <input required value={newTower.ip} onChange={e => setNewTower({...newTower, ip: e.target.value})} placeholder="192.168..." style={{ width: '100%', padding: '10px', borderRadius: '6px', border: '1px solid #e5e7eb' }} />
          </div>

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '10px', marginBottom: '20px' }}>
            <div>
              <label style={{ display: 'block', fontSize: '0.85rem', marginBottom: '5px', color: '#6b7280' }}>Enlem</label>
              <input type="number" step="any" value={newTower.lat} onChange={e => setNewTower({...newTower, lat: parseFloat(e.target.value)})} style={{ width: '100%', padding: '10px', borderRadius: '6px', border: '1px solid #e5e7eb' }} />
            </div>
            <div>
              <label style={{ display: 'block', fontSize: '0.85rem', marginBottom: '5px', color: '#6b7280' }}>Boylam</label>
              <input type="number" step="any" value={newTower.lng} onChange={e => setNewTower({...newTower, lng: parseFloat(e.target.value)})} style={{ width: '100%', padding: '10px', borderRadius: '6px', border: '1px solid #e5e7eb' }} />
            </div>
          </div>

          <button type="submit" style={{ width: '100%', padding: '12px', backgroundColor: '#3b82f6', color: 'white', border: 'none', borderRadius: '8px', cursor: 'pointer', display: 'flex', justifyContent: 'center', alignItems: 'center', gap: '10px', fontWeight: 'bold' }}>
            <Plus size={20} /> Direği Kaydet
          </button>
        </form>

      </div>
    </div>
  );
}
