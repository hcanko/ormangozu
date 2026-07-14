import { BrowserRouter, Routes, Route } from 'react-router-dom';
import Layout from './components/Layout';
import DashboardMap from './components/DashboardMap';
import Settings from './components/Settings';
import Towers from './components/Towers';
import Alerts from './components/Alerts';
import Analytics from './components/Analytics';
import MobileTest from './components/MobileTest';

// 🔥 EKSİK OLAN SATIRI EKLEDİK (Dosya yolunun components içinde olduğunu varsayıyorum)
import AdminPanel from './components/AdminPanel'; 

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
          <Route path="analytics" element={<Analytics />} /> 
          <Route path="settings" element={<Settings />} /> 
        </Route>
      </Routes>
    </BrowserRouter>
  );
}