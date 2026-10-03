"""Run the merged API in an isolated subprocess: avoid touching any operator DB."""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BACKEND = ROOT / 'backend'

INTEGRATION = r'''
import csv, io
from fastapi.testclient import TestClient
from main import app
from models.database import SessionLocal
from models.orm import SensorLogDB

headers = ['protocol_version','firmware_version','boot_id','tower_id','sequence','uptime_ms','trigger',
    'max_temp','ambient_temp','hotspot_threshold','top5_temp','largest_hotspot_cluster',
    'persistence_count','delta_t','gas_raw','gas_ema','gas_drop_pct','fire_score','fire_level',
    'fire_status','health_level','network_confirmed','battery_mv','battery_pct',
    'solar_voltage_mv','solar_current_ma','mlx_ok','gas_ok','error_counter','free_heap','lat','lng']
row = {'protocol_version':4, 'firmware_version':'0.6.0', 'boot_id':'11',
    'tower_id':'NEST-001122334455','sequence':99,'uptime_ms':123456,'trigger':'BASELINE',
    'max_temp':67.5,'ambient_temp':29.0,'hotspot_threshold':45.0,'top5_temp':64.2,
    'largest_hotspot_cluster':7,'persistence_count':3,'delta_t':0.5,'gas_raw':85000,
    'gas_ema':88000,'gas_drop_pct':5,'fire_score':65.5,'fire_level':2,
    'fire_status':'WARNING','health_level':0,'network_confirmed':0,'battery_mv':3940,
    'battery_pct':77,'solar_voltage_mv':-1,'solar_current_ma':-1,'mlx_ok':1,'gas_ok':1,
    'error_counter':0,'free_heap':180000,'lat':0,'lng':0}
text = io.StringIO(); writer = csv.DictWriter(text, fieldnames=headers)
writer.writeheader(); writer.writerow(row)
with TestClient(app) as client:
    headers_operator={'X-Client-Token':'isolated-operator-token'}
    headers_auth={'X-Client-Token':'isolated-test-token'}
    assert app.version == '0.7.0-pilot'
    assert client.get('/api/health').status_code == 200
    first = client.post('/api/import/device-log', files={'file':('nest.csv', text.getvalue(), 'text/csv')},headers=headers_operator)
    assert first.status_code == 200, first.text
    assert first.json()['imported_rows'] == 1, first.text
    again = client.post('/api/import/device-log', files={'file':('nest.csv', text.getvalue(), 'text/csv')},headers=headers_operator)
    assert again.json()['skipped_duplicates'] == 1, again.text
    db = SessionLocal()
    try:
        sample = db.query(SensorLogDB).filter(SensorLogDB.tower_id == 'NEST-001122334455').one()
        assert sample.protocol_version == 4 and sample.sequence == 99
        assert sample.device_fire_score == 65.5 and sample.gas_level == 85000
        assert sample.device_status == 'WARNING' and sample.fire_level == 2
        assert sample.largest_hotspot_cluster == 7 and sample.persistence_count == 3
        assert sample.ambient_temp == 29 and sample.sample_trigger == 'BASELINE'
        assert sample.solar_voltage_mv is None and sample.solar_current_ma is None
        assert sample.device_timestamp == 123456  # uptime only, not UTC wall clock
    finally:
        db.close()
    mesh = {'device_id':'NEST-001122334455','record_kind':'fusion','type':'FUSION',
            'result':'NETWORK_CORROBORATED','boot':11,'origin_boot':11,'sequence':99,
            'peer':'NEST-66778899AABB','score':65.5,'peer_score':62.0,
            'received_at':'2026-10-02T12:00:00+00:00'}
    assert client.post('/api/mesh/events', json=mesh).status_code == 401
    one=client.post('/api/mesh/events',json=mesh,headers=headers_auth)
    assert one.status_code == 200, one.text
    duplicate=client.post('/api/mesh/events',json=mesh,headers=headers_auth)
    assert duplicate.json()['duplicate'] is True and duplicate.json()['id']==one.json()['id']
    labelled=client.post('/api/mesh/labels',json={'event_id':one.json()['id'],
        'truth':'controlled_test','notes':'electric heat target'},headers=headers_operator)
    assert labelled.status_code == 200, labelled.text
    fetched=client.get('/api/mesh/events',headers=headers_operator)
    assert fetched.status_code == 200 and fetched.json()[0]['peer_score']==62.0
    assert fetched.json()[0]['truth']=='controlled_test'
    exported=client.get('/api/mesh/export.csv',headers=headers_operator)
    assert 'controlled_test' in exported.text and exported.status_code==200
    settings=client.post('/api/settings/calibration',json={'weight_delta_t':.4,
        'weight_temp':.3,'weight_gas':.3,'override_temp':90},headers=headers_operator)
    assert settings.status_code == 200, settings.text
    assert client.get('/api/settings/calibration').status_code == 200
    assert client.get('/api/towers/live').status_code == 200
print('MERGE_INTEGRATION_PASS old API + OG4 CSV + migration + mesh + label + export')
'''

class MergeIntegrationTests(unittest.TestCase):
    def test_old_and_new_backend_in_same_process(self):
        with tempfile.TemporaryDirectory(prefix='og060-') as d:
            env = os.environ.copy()
            env['DATABASE_URL'] = 'sqlite:///' + str(Path(d) / 'old_model.sqlite3')
            env['OG_PILOT_DB'] = str(Path(d) / 'mesh.sqlite3')
            env['OG_CLIENT_TOKEN'] = 'isolated-test-token'
            env['OG_OPERATOR_TOKEN'] = 'isolated-operator-token'
            env['ENABLE_HTTP_WHISPER'] = '0'
            proc = subprocess.run([sys.executable, '-c', INTEGRATION], cwd=BACKEND,
                                  env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(proc.returncode, 0, proc.stdout + '\n' + proc.stderr)
            self.assertIn('MERGE_INTEGRATION_PASS', proc.stdout)

if __name__ == '__main__':
    unittest.main()
