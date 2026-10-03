import { BrowserRouter, Routes, Route } from 'react-router-dom';
import Layout from './pages/Layout';
import DashboardMap from './pages/DashboardMap';
import Settings from './pages/Settings';
import Towers from './pages/Towers';
import Alerts from './pages/Alerts';
import Analytics from './pages/Analytics';
import MobileTest from './pages/MobileTest';
import WhisperPilot from './components/WhisperPilot';
import ControlCenter from './pages/ControlCenter';

// 🔥 EKSİK OLAN SATIRI EKLEDİK (Dosya yolunun components içinde olduğunu varsayıyorum)
import AdminPanel from './pages/AdminPanel'; 

export default function App() {
  return (
    <BrowserRouter>
      <Routes>
        {/* ANA UYGULAMA (SOL MENÜLÜ) */}
        <Route path="/" element={<Layout />}>
          <Route index element={<DashboardMap />} />
          <Route path="mobile" element={<MobileTest />} />
          
          {/* 🔥 ADMİN PANELİ ROTASI BURADA */}
          <Route path="admin" element={<AdminPanel />} />
          
          <Route path="towers" element={<Towers />} /> 
          <Route path="alerts" element={<Alerts />} />
          <Route path="whisper" element={<WhisperPilot />} />
          <Route path="control" element={<ControlCenter />} /> 
          <Route path="analytics" element={<Analytics />} /> 
          <Route path="settings" element={<Settings />} /> 
        </Route>
      </Routes>
    </BrowserRouter>
  );
}