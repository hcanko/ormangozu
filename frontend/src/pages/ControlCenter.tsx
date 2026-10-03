import { useCallback, useEffect, useMemo, useState } from 'react';
import type { CSSProperties } from 'react';
import { RadioTower, RefreshCw, ShieldAlert, Camera, Move, Activity } from 'lucide-react';
import { apiUrl } from '../lib/api';

type Job = {
  id: number;
  created_at: string;
  target_id: string;
  opcode: string;
  argument: number;
  state: string;
  bridge_device_id?: string;
  result_status?: string;
  result_json?: string;
};
type Reading = { max_temp?: number; score?: number; battery_pct?: number;
  pan?: number; tilt?: number; manual?: boolean; health?: number; target_id?: string };
const idPattern = /^NEST-[0-9A-F]{12}$/;
const buttonStyle: CSSProperties = { padding: '10px 13px', border: '1px solid #cbd5e1', borderRadius: 7, cursor: 'pointer', background: 'white' };

export default function ControlCenter() {
  const [token, setToken] = useState('');
  const [target, setTarget] = useState(() => {
    const query = new URLSearchParams(window.location.search).get('target')?.toUpperCase() ?? '';
    return /^NEST-[0-9A-F]{12}$/.test(query) ? query : '';
  });
  const [jobs, setJobs] = useState<Job[]>([]);
  const [error, setError] = useState('');
  const [pending, setPending] = useState(false);
  const [pan, setPan] = useState(90);
  const [tilt, setTilt] = useState(90);
  const validTarget = idPattern.test(target);
  const busy = pending || jobs.some(j => ['queued', 'claimed', 'sent'].includes(j.state));
  const latest = useMemo(() => jobs.find(j => j.target_id === target && j.state === 'completed' && j.result_json), [jobs, target]);
  const reading: Reading | null = useMemo(() => {
    try { return latest?.result_json ? JSON.parse(latest.result_json) as Reading : null; }
    catch { return null; }
  }, [latest]);
  const manual = Boolean(reading?.manual);

  const refresh = useCallback(async () => {
    if (!token.trim()) return;
    try {
      const response = await fetch(apiUrl('/control/commands?limit=25'), {
        headers: { 'X-Client-Token': token }, cache: 'no-store',
      });
      if (!response.ok) throw new Error(`Komut listesi: HTTP ${response.status}`);
      setJobs(await response.json() as Job[]);
      setError('');
    } catch (e) { setError(e instanceof Error ? e.message : 'Bağlantı hatası'); }
  }, [token]);
  useEffect(() => {
    if (!token.trim()) return;
    void refresh();
    const timer = window.setInterval(() => { void refresh(); }, 2000);
    return () => window.clearInterval(timer);
  }, [refresh, token]);

  async function issue(opcode: string, argument = 0) {
    if (!validTarget || !token.trim() || busy) return;
    setPending(true);
    try {
      const response = await fetch(apiUrl('/control/commands'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Client-Token': token },
        body: JSON.stringify({ target_id: target, opcode, argument }),
      });
      if (!response.ok) {
        const message = await response.text();
        throw new Error(`Komut kabul edilmedi (HTTP ${response.status}): ${message}`);
      }
      await refresh();
    } catch (e) { setError(e instanceof Error ? e.message : 'Komut gönderilemedi'); }
    finally { setPending(false); }
  }

  return (
    <main style={{ padding: 25, overflowY: 'auto', height: '100%', color: '#172033' }}>
      <h2 style={{ display: 'flex', alignItems: 'center', gap: 10, marginTop: 0 }}><RadioTower /> Nest Uzaktan Kontrol <small style={{ color: '#64748b', fontWeight: 400 }}>v0.7.0 pilot</small></h2>
      <p style={{ color: '#475569' }}>Bilgisayara USB ile bağlı bir Nest, hedef Nest'e kimlik doğrulamalı LoRa komutu iletir. Her seferinde yalnız bir komut işlenir; internet veya sürekli açık Wi-Fi gerekmez.</p>
      <section style={{ padding: 17, background: '#fff', borderRadius: 9, marginBottom: 16, border: '1px solid #e2e8f0' }}>
        <label htmlFor="control-token">Operatör anahtarı (OG_OPERATOR_TOKEN)</label>
        <input id="control-token" type="password" autoComplete="off" value={token}
          onChange={e => setToken(e.target.value)} placeholder="Yalnızca bu oturumda tutulur"
          style={{ width: '100%', maxWidth: 420, display: 'block', padding: 10, margin: '7px 0 14px', border: '1px solid #cbd5e1', borderRadius: 7 }} />
        <label htmlFor="nest-target">Hedef cihaz kimliği</label>
        <input id="nest-target" value={target} onChange={e => setTarget(e.target.value.trim().toUpperCase())}
          placeholder="NEST-001122AABBCC" style={{ display: 'block', maxWidth: 420, width: '100%', padding: 10, marginTop: 7, border: `1px solid ${target && !validTarget ? '#ef4444' : '#cbd5e1'}`, borderRadius: 7 }} />
        <small style={{ color: '#64748b' }}>Nest'in USB çıktısındaki gerçek 12 haneli MAC-türevi kimliği gir. Hedefe toplu yayın komutu gönderilmez.</small>
      </section>
      <section style={{ padding: 17, background: '#fff', borderRadius: 9, marginBottom: 16, border: '1px solid #e2e8f0' }}>
        <h3 style={{ marginTop: 0, display: 'flex', gap: 8, alignItems: 'center' }}><Activity size={19} /> Sensör ve işletim</h3>
        <div style={{ display: 'flex', gap: 9, flexWrap: 'wrap' }}>
          <button style={buttonStyle} disabled={!validTarget || !token || busy} onClick={() => void issue('STATUS')}>Cihaz durumunu sor</button>
          <button style={buttonStyle} disabled={!validTarget || !token || busy} onClick={() => void issue('SAMPLE')}>Şimdi ölçüm al</button>
          <button style={buttonStyle} disabled={!validTarget || !token || busy} onClick={() => void issue('FAST')}>Hızlı tarama başlat</button>
          <button style={buttonStyle} disabled={!validTarget || !token || busy} onClick={() => void issue('AUTO')}>Otomatiğe dön</button>
        </div>
        <p style={{ fontSize: 13, color: '#64748b', marginBottom: 0 }}>SAMPLE/FAST yanıtı termal özet içerir; 768 piksellik görüntü LoRa'dan aktarılmaz. Ayrıntılı kareleri bakım Wi-Fi'si üzerinden indir.</p>
      </section>
      <section style={{ padding: 17, background: '#fff', borderRadius: 9, marginBottom: 16, border: '1px solid #e2e8f0' }}>
        <h3 style={{ marginTop: 0, display: 'flex', gap: 8, alignItems: 'center' }}><Camera size={19} /> Manuel kamera / Pan–Tilt</h3>
        <p style={{ fontSize: 13, color: '#64748b' }}>Servo takılı ve GPIO/power doğrulanmışsa firmware'de etkinleştirilir. Mevcut güvenli varsayılan kapalıdır; bu durumda hareket komutları UNSUPPORTED döner. Yangın şüphesi ve süre aşımı otomatik moda döndürür.</p>
        <div style={{ display: 'flex', gap: 9, flexWrap: 'wrap', marginBottom: 13 }}>
          <button style={buttonStyle} disabled={!validTarget || !token || busy} onClick={() => void issue('MANUAL')}><Move size={15} /> Manuel moda geç</button>
          <button style={buttonStyle} disabled={!validTarget || !token || busy} onClick={() => void issue('HOME')}>Kamerayı merkeze al (90°)</button>
        </div>
        <div style={{ display: 'grid', gridTemplateColumns: 'minmax(160px,300px) auto', gap: 11, alignItems: 'center' }}>
          <label htmlFor="pan-degree">Yatay: {pan}°</label><span />
          <input type="range" id="pan-degree" min={20} max={160} step={5} value={pan} onChange={e => setPan(Number(e.target.value))} disabled={!manual} />
          <button style={buttonStyle} disabled={!manual || busy || !validTarget} onClick={() => void issue('PAN', pan)}>Yatay konumu uygula</button>
          <label htmlFor="tilt-degree">Dikey: {tilt}°</label><span />
          <input type="range" id="tilt-degree" min={20} max={160} step={5} value={tilt} onChange={e => setTilt(Number(e.target.value))} disabled={!manual} />
          <button style={buttonStyle} disabled={!manual || busy || !validTarget} onClick={() => void issue('TILT', tilt)}>Dikey konumu uygula</button>
        </div>
      </section>
      {reading && <section style={{ background: 'white', borderRadius: 9, border: '1px solid #e2e8f0', padding: 16, marginBottom: 16 }}>
        <strong>Son başarılı yanıt · {target}</strong>
        <p>Max: {reading.max_temp ?? '–'}°C · Fire Score: {reading.score ?? '–'} · Sağlık: {reading.health ?? '–'} · Pil: {reading.battery_pct ?? '–'}% · Yatay: {reading.pan ?? '–'}° · Dikey: {reading.tilt ?? '–'}° · Mod: {reading.manual ? 'MANUAL' : 'AUTO'}</p>
      </section>}
      <section style={{ padding: 17, background: '#fff', borderRadius: 9, border: '1px solid #e2e8f0' }}>
        <h3 style={{ display: 'flex', alignItems: 'center', gap: 8, marginTop: 0 }}><RefreshCw size={18} /> Komut geçmişi</h3>
        {error && <p role="alert" style={{ color: '#b91c1c' }}><ShieldAlert size={16} /> {error}</p>}
        <small style={{ color: '#64748b' }}>2 saniyede bir yenilenir. “queued” görünüyorsa bağlı USB collector yok veya komut henüz alınmadı. Bekleyen komut 45 saniyede otomatik sona erer.</small>
        <div style={{ overflowX: 'auto', marginTop: 12 }}><table style={{ width: '100%', textAlign: 'left', borderCollapse: 'collapse', fontSize: 13 }}>
          <thead><tr>{['İş ID', 'Hedef', 'Komut', 'Durum', 'Cevap'].map(h => <th key={h} style={{ padding: 8, borderBottom: '1px solid #cbd5e1' }}>{h}</th>)}</tr></thead>
          <tbody>{jobs.map(j => <tr key={j.id}>
            <td style={{ padding: 8 }}>#{j.id}</td><td>{j.target_id}</td><td>{j.opcode}{j.argument ? ` ${j.argument}°` : ''}</td>
            <td>{j.state}</td><td>{j.result_status ?? '–'}</td>
          </tr>)}{jobs.length === 0 && <tr><td colSpan={5} style={{ padding: 12 }}>Henüz gönderilmiş komut yok.</td></tr>}</tbody>
        </table></div>
      </section>
    </main>
  );
}
