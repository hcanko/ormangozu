import { Outlet, Link, useLocation } from 'react-router-dom';
import { Map, RadioTower, AlertTriangle, Settings, Activity } from 'lucide-react';

export default function Layout() {
  const location = useLocation();

  const menuItems = [
    { path: '/', name: 'Canlı Harita', icon: <Map size={20} /> },
    { path: '/towers', name: 'Direk Yönetimi', icon: <RadioTower size={20} /> },
    { path: '/alerts', name: 'Alarm Geçmişi', icon: <AlertTriangle size={20} /> },
    { path: '/analytics', name: 'Analitik', icon: <Activity size={20} /> },
    { path: '/settings', name: 'Sistem Ayarları', icon: <Settings size={20} /> },
  ];

  return (
    <div style={{ display: 'flex', height: '100vh', width: '100vw', backgroundColor: '#f3f4f6', overflow: 'hidden' }}>
      
      {/* SOL MENÜ (SIDEBAR) */}
      <div style={{ width: '250px', backgroundColor: '#1f2937', color: 'white', display: 'flex', flexDirection: 'column' }}>
        <div style={{ padding: '20px', fontSize: '1.2rem', fontWeight: 'bold', borderBottom: '1px solid #374151', display: 'flex', alignItems: 'center', gap: '10px' }}>
          🔥 OrmanGozu
        </div>
        <nav style={{ flex: 1, padding: '10px 0' }}>
          {menuItems.map((item) => {
            const isActive = location.pathname === item.path;
            return (
              <Link 
                key={item.path} 
                to={item.path}
                style={{
                  display: 'flex', alignItems: 'center', gap: '10px', padding: '15px 20px',
                  color: isActive ? '#60a5fa' : '#9ca3af',
                  backgroundColor: isActive ? '#374151' : 'transparent',
                  textDecoration: 'none', fontWeight: isActive ? 'bold' : 'normal',
                  transition: 'all 0.2s'
                }}
              >
                {item.icon} {item.name}
              </Link>
            );
          })}
        </nav>
      </div>

      {/* SAĞ İÇERİK EKRANI (DİĞER SAYFALAR BURAYA YÜKLENİR) */}
      <div style={{ flex: 1, position: 'relative', height: '100%' }}>
        <Outlet /> 
      </div>

    </div>
  );
}