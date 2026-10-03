import { useState } from 'react';
import { Battery, MapPin, Power, Radio, ShieldCheck } from 'lucide-react';
import { useTowers } from '../hooks/useTowers';
import { apiUrl } from '../lib/api';
import type { Tower } from '../types';

const PRODUCT_LABELS: Record<string, string> = {
  MINI_NEST: 'Mini Nest',
  NEST_VISION: 'Nest Vision',
  NEST_INDUSTRIAL: 'Nest Industrial',
  NEST_HUB: 'Nest Hub',
  NEST_RELAY: 'Nest Relay',
};

const STATE_COLORS: Record<string, { background: string; color: string }> = {
  READY_FOR_INSTALLATION: { background: '#dcfce7', color: '#166534' },
  ACTIVE: { background: '#d1fae5', color: '#047857' },
  DISCOVERED: { background: '#e0f2fe', color: '#0369a1' },
  LOCATION_PENDING: { background: '#fef3c7', color: '#92400e' },
  TEST_FAILED: { background: '#fee2e2', color: '#991b1b' },
  MAINTENANCE: { background: '#ffedd5', color: '#9a3412' },
  DISABLED: { background: '#f3f4f6', color: '#4b5563' },
  REVOKED: { background: '#fee2e2', color: '#7f1d1d' },
};

export default function Towers() {
  const { towers = [] } = useTowers();
  const [operatorToken, setOperatorToken] = useState('');
  const [message, setMessage] = useState('');

  const disableDevice = async (device: Tower) => {
    if (!operatorToken) {
      setMessage('Cihaz durumunu değiştirmek için operatör tokenı gerekli.');
      return;
    }
    if (!window.confirm(`${device.id} cihazını devre dışı bırakmak istiyor musunuz?`)) return;
    const response = await fetch(apiUrl(`/devices/${device.id}`), {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json', 'X-Client-Token': operatorToken },
      body: JSON.stringify({ lifecycle_state: 'DISABLED' }),
    });
    setMessage(response.ok ? `${device.id} devre dışı bırakıldı.` : `İşlem başarısız: ${response.status}`);
  };

  return (
    <div style={{ padding: 30, height: '100%', overflowY: 'auto' }}>
      <h2 style={{ marginTop: 0 }}>Nest Filosu</h2>
      <p style={{ color: '#4b5563', maxWidth: 900 }}>
        Firmware yüklenen cihazı USB ile bağlayıp <code>python tools/provision_device.py --port COM5</code>
        {' '}komutunu çalıştırın. Gerçek ID otomatik kaydedilir; GNSS etkinleşene kadar konum beklemede kalır.
      </p>

      <div style={{ display: 'flex', gap: 12, alignItems: 'center', margin: '18px 0' }}>
        <input
          type="password"
          value={operatorToken}
          onChange={(event) => setOperatorToken(event.target.value)}
          placeholder="Operatör tokenı (yalnız bu sayfanın belleğinde)"
          style={{ width: 360, padding: 10, border: '1px solid #d1d5db', borderRadius: 8 }}
        />
        {message && <span style={{ color: '#4b5563' }}>{message}</span>}
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(360px, 1fr))', gap: 16 }}>
        {towers.length === 0 && <p style={{ color: '#6b7280' }}>Henüz kayıtlı Nest bulunmuyor.</p>}
        {towers.map((device) => {
          const lifecycle = device.lifecycle_state ?? 'DISCOVERED';
          const stateStyle = STATE_COLORS[lifecycle] ?? STATE_COLORS.DISCOVERED;
          const locationPending = !device.location_status || device.location_status === 'PENDING';
          return (
            <article key={device.id} style={{ background: 'white', padding: 20, borderRadius: 12, boxShadow: '0 2px 6px rgba(0,0,0,.08)' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', gap: 16 }}>
                <div>
                  <h3 style={{ margin: 0 }}>{device.name}</h3>
                  <code style={{ color: '#6b7280' }}>{device.id}</code>
                </div>
                <span style={{ ...stateStyle, alignSelf: 'flex-start', padding: '4px 9px', borderRadius: 999, fontSize: 12, fontWeight: 700 }}>
                  {lifecycle}
                </span>
              </div>

              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10, marginTop: 18, color: '#374151', fontSize: 14 }}>
                <span><Radio size={15} /> {PRODUCT_LABELS[device.product_model ?? 'MINI_NEST']} / {device.network_role ?? 'NODE'}</span>
                <span><ShieldCheck size={15} /> FW {device.firmware_version ?? 'bilinmiyor'}</span>
                <span><Battery size={15} /> %{Math.round(device.battery ?? 0)}</span>
                <span><MapPin size={15} /> {locationPending ? 'GNSS bekleniyor' : `${device.lat.toFixed(5)}, ${device.lng.toFixed(5)}`}</span>
              </div>

              <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 14 }}>
                {Object.entries(device.capabilities ?? {}).filter(([, enabled]) => enabled).map(([name]) => (
                  <span key={name} style={{ background: '#f3f4f6', color: '#374151', padding: '3px 7px', borderRadius: 6, fontSize: 12 }}>{name}</span>
                ))}
              </div>

              {(device.primary_hub_id || device.secondary_hub_id) && (
                <p style={{ margin: '12px 0 0', color: '#4b5563', fontSize: 13 }}>
                  Hub: {[device.primary_hub_id, device.secondary_hub_id].filter(Boolean).join(' + ')}
                </p>
              )}

              {!['DISABLED', 'REVOKED'].includes(lifecycle) && (
                <button onClick={() => void disableDevice(device)} style={{ marginTop: 18, border: '1px solid #dc2626', color: '#dc2626', background: 'white', borderRadius: 8, padding: '8px 10px', cursor: 'pointer' }}>
                  <Power size={15} /> Devre dışı bırak
                </button>
              )}
            </article>
          );
        })}
      </div>
    </div>
  );
}
