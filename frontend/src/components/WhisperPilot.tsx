import { useCallback, useEffect, useMemo, useState } from 'react';
import { Activity, RefreshCw, RadioTower, ShieldCheck, Thermometer, WifiOff } from 'lucide-react';
import { apiUrl } from '../lib/api';

type PilotEvent = {
  id: number;
  received_at: string;
  record_kind: 'mesh' | 'telemetry' | 'fusion';
  device_id: string;
  source_device_id?: string;
  firmware?: string;
  type?: string;
  result?: string;
  peer?: string;
  direction?: string;
  boot?: number;
  origin_boot?: number;
  sequence?: number;
  level?: number;
  score?: number;
  max_temp?: number;
  ambient_temp?: number;
  cluster?: number;
  health?: number;
  battery_mv?: number;
  rssi?: number;
  snr?: number;
  truth?: string;
  notes?: string;
};

const levels = ['NORMAL', 'WATCH', 'WARNING', 'CRITICAL', 'NETWORK_CORROBORATED'];
const labelOptions = [
  ['', 'Etiketsiz'], ['fire', 'Doğrulanmış yangın'], ['no_fire', 'Yangın değil'],
  ['uncertain', 'Belirsiz'], ['controlled_test', 'Kontrollü test'],
  ['equipment_fault', 'Cihaz hatası'],
] as const;

export default function WhisperPilot() {
  // This is a local pilot UI: retain the token in page memory, not localStorage.
  const [token, setToken] = useState('');
  const [events, setEvents] = useState<PilotEvent[]>([]);
  const [error, setError] = useState('');
  const [pending, setPending] = useState(false);
  const [lastRefresh, setLastRefresh] = useState('');
  const [showTelemetry, setShowTelemetry] = useState(false);

  const refresh = useCallback(async () => {
    if (!token.trim()) return;
    try {
      const response = await fetch(apiUrl('/mesh/events?limit=500'), {
        headers: { 'X-Client-Token': token }, cache: 'no-store',
      });
      if (!response.ok) throw new Error(`Bilgisayardaki API yanıtı: HTTP ${response.status}`);
      const rows = await response.json() as PilotEvent[];
      setEvents(rows);
      setLastRefresh(new Date().toLocaleTimeString('tr-TR'));
      setError('');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Bağlantı kurulamadı');
    }
  }, [token]);

  useEffect(() => {
    if (!token.trim()) return;
    void refresh();
    const timer = window.setInterval(() => { void refresh(); }, 5000);
    return () => window.clearInterval(timer);
  }, [refresh, token]);

  const observed = useMemo(() => {
    const latest = new Map<string, PilotEvent>();
    // List is newest-first; RX describes a separate source device, not the observer.
    for (const event of events) {
      if (event.record_kind !== 'telemetry' || latest.has(event.device_id)) continue;
      latest.set(event.device_id, event);
    }
    return [...latest.values()];
  }, [events]);

  const shownEvents = events.filter(e => showTelemetry || e.record_kind !== 'telemetry').slice(0, 150);

  async function saveLabel(id: number, truth: string) {
    if (!truth) return; // Clear-label endpoint deliberately not available in this pilot.
    setPending(true);
    try {
      const response = await fetch(apiUrl('/mesh/labels'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Client-Token': token },
        body: JSON.stringify({ event_id: id, truth, notes: '' }),
      });
      if (!response.ok) throw new Error(`Etiket kaydedilemedi: HTTP ${response.status}`);
      await refresh();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Etiket kaydedilemedi');
    } finally { setPending(false); }
  }

  return (
    <div style={{ padding: 28, height: '100%', overflow: 'auto', color: '#1f2937' }}>
      <h2 style={{ margin: '0 0 5px', display: 'flex', gap: 10, alignItems: 'center' }}><RadioTower size={26} /> Whisper • Pilot İstemcisi</h2>
      <p style={{ color: '#64748b', marginTop: 5 }}>Bir Nest USB ile bilgisayara bağlanır. Whisper kararı cihazların üzerinde alınır; bu ekran yalnızca kayıtları görüntüler.</p>
      <section style={{ background: 'white', borderRadius: 10, padding: 16, marginBottom: 16 }}>
        <label htmlFor="og-client-token" style={{ display: 'block', fontWeight: 600, marginBottom: 7 }}>Operatör anahtarı (OG_OPERATOR_TOKEN)</label>
        <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap' }}>
          <input id="og-client-token" type="password" autoComplete="off" value={token} onChange={e => setToken(e.target.value)} placeholder="Bilgisayarda ayarladığın token" style={{ flex: 1, minWidth: 180, padding: 10, border: '1px solid #cbd5e1', borderRadius: 6 }} />
          <button type="button" onClick={() => void refresh()} disabled={!token || pending} style={{ padding: '10px 16px', border: 0, borderRadius: 6, cursor: 'pointer', display: 'flex', gap: 7, alignItems: 'center' }}><RefreshCw size={16} /> Yenile</button>
        </div>
        <small style={{ color: '#64748b' }}>5 saniyede bir yenilenir · Son okuma: {lastRefresh || 'Henüz yok'} · Bu ekran üretim ortamına açık şekilde dağıtılmamalıdır.</small>
        {error && <p role="alert" style={{ color: '#b91c1c' }}>{error}. Önce USB collector ve yerel backend'i başlat.</p>}
      </section>
      <h3 style={{ marginBottom: 9 }}>Bilgisayarın gördüğü Nest'ler</h3>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 12, marginBottom: 19 }}>
        {observed.length === 0 && <div style={{ background: 'white', padding: 18, borderRadius: 8, color: '#64748b' }}><WifiOff size={18} /> Henüz telemetri gelmedi. Bu bir yangın yokluğu göstergesi değildir.</div>}
        {observed.map(e => <div key={e.device_id} style={{ background: 'white', padding: 16, borderRadius: 9, minWidth: 220 }}>
          <strong>{e.device_id}</strong><div style={{ fontSize: 12, color: '#64748b' }}>{new Date(e.received_at).toLocaleString('tr-TR')}</div>
          <p style={{ margin: '9px 0 3px', display: 'flex', gap: 5, alignItems: 'center' }}><Thermometer size={16} /> {e.max_temp?.toFixed(1) ?? '–'}°C · skor {e.score?.toFixed(1) ?? '–'}</p>
          <small>Durum: {levels[e.level ?? 0] || 'Bilinmiyor'} · pil: {e.battery_mv?.toFixed(0) ?? '–'} mV</small>
        </div>)}
      </div>
      <section style={{ background: 'white', borderRadius: 10, padding: 16 }}>
        <div style={{ display: 'flex', flexWrap: 'wrap', justifyContent: 'space-between', gap: 10, alignItems: 'center', marginBottom: 9 }}>
          <h3 style={{ margin: 0, display: 'flex', gap: 8, alignItems: 'center' }}><Activity size={19} /> Whisper olayları</h3>
          <label style={{ fontSize: 13 }}><input type="checkbox" checked={showTelemetry} onChange={e => setShowTelemetry(e.target.checked)} /> Olağan telemetriyi de göster</label>
        </div>
        <p style={{ margin: '0 0 12px', color: '#64748b', fontSize: 13 }}><ShieldCheck size={14} /> NETWORK_CORROBORATED iki Nest'in anomali desteğidir; doğrulanmış yangın etiketi değildir.</p>
        <div style={{ overflowX: 'auto' }}><table style={{ borderCollapse: 'collapse', width: '100%', textAlign: 'left', fontSize: 13 }}>
          <thead><tr>{['Zaman', 'Kaynak Nest', 'İşlem', 'Olay', 'Skor / sıcaklık', 'Kayıt etiketi'].map(h => <th key={h} style={{ padding: 9, borderBottom: '1px solid #e2e8f0' }}>{h}</th>)}</tr></thead>
          <tbody>
            {shownEvents.map(e => <tr key={e.id}>
              <td style={{ padding: 9, borderBottom: '1px solid #f1f5f9', whiteSpace: 'nowrap' }}>{new Date(e.received_at).toLocaleString('tr-TR')}</td>
              <td style={{ padding: 9, borderBottom: '1px solid #f1f5f9' }}>{e.source_device_id || e.device_id}<div style={{ color: '#94a3b8', fontSize: 11 }}>{e.peer ? `Eş: ${e.peer}` : ''}</div></td>
              <td style={{ padding: 9, borderBottom: '1px solid #f1f5f9' }}>{e.direction || e.record_kind}</td>
              <td style={{ padding: 9, borderBottom: '1px solid #f1f5f9', fontWeight: e.result === 'NETWORK_CORROBORATED' ? 700 : 400 }}>{e.result || e.type || 'SAMPLE'}</td>
              <td style={{ padding: 9, borderBottom: '1px solid #f1f5f9' }}>{e.score == null ? '–' : e.score.toFixed(1)} / {e.max_temp == null ? '–' : `${e.max_temp.toFixed(1)}°C`}</td>
              <td style={{ padding: 9, borderBottom: '1px solid #f1f5f9' }}><select aria-label={`Olay ${e.id} doğrulama etiketi`} disabled={pending} value={e.truth || ''} onChange={x => void saveLabel(e.id, x.target.value)} style={{ maxWidth: 155, padding: 4 }}>
                {labelOptions.map(([value, caption]) => <option value={value} key={value}>{caption}</option>)}
              </select></td>
            </tr>)}
            {shownEvents.length === 0 && <tr><td colSpan={6} style={{ padding: 16, color: '#64748b' }}>Henüz kaydedilmiş olay yok.</td></tr>}
          </tbody>
        </table></div>
        <p style={{ fontSize: 12, color: '#64748b' }}>Model eğitimi için saha etiketleri operatör tarafından sonradan girilir. Doğrulanmamış alarmları yangın gerçeği olarak kullanma.</p>
      </section>
    </div>
  );
}
