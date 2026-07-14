import { ShieldCheck } from 'lucide-react';

export default function Settings() {
  return (
    <div style={{ padding: '30px', maxWidth: '800px', margin: '0 auto' }}>
      <h2 style={{ fontSize: '1.8rem', color: '#1f2937', marginBottom: '20px', borderBottom: '2px solid #e5e7eb', paddingBottom: '10px' }}>
        ⚙️ Sistem Ayarları
      </h2>
      <div style={{ backgroundColor: 'white', padding: '20px', borderRadius: '8px', boxShadow: '0 4px 6px -1px rgba(0, 0, 0, 0.1)', display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div>
          <h3 style={{ fontSize: '1.2rem', margin: '0 0 5px 0', color: '#374151', display: 'flex', alignItems: 'center', gap: '8px' }}>
            <ShieldCheck color="#10b981" /> Sistem Durumu
          </h3>
          <p style={{ margin: 0, color: '#6b7280', fontSize: '0.9rem' }}>
            Orman Gözü ağı şu anda aktif ve tüm kuleler standart modda çalışıyor.
          </p>
        </div>
      </div>
    </div>
  );
}