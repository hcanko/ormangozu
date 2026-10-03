"""Isolated device-registry lifecycle tests."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = r'''
import os, sqlite3
legacy_path = os.environ['DATABASE_URL'].removeprefix('sqlite:///')
with sqlite3.connect(legacy_path) as legacy:
    legacy.execute(
        'CREATE TABLE towers ('
        'id VARCHAR PRIMARY KEY, name VARCHAR NOT NULL, ip VARCHAR, '
        'lat FLOAT NOT NULL, lng FLOAT NOT NULL, bearing FLOAT, '
        'sleep_interval INTEGER, battery_level FLOAT, is_online BOOLEAN)'
    )

from fastapi.testclient import TestClient
from main import app

COLLECTOR = {'X-Client-Token': 'registry-collector'}
OPERATOR = {'X-Client-Token': 'registry-operator'}
NODE = 'NEST-001122334455'
HUB = 'NEST-AABBCCDDEEFF'
HUB2 = 'NEST-FFEEDDCCBBAA'

passed = {'storage': True, 'lora': True, 'thermal': True, 'environmental': True}
node = {
    'device_id': NODE,
    'firmware_version': '0.7.0-test',
    'hardware_revision': 'PILOT-V1',
    'product_model': 'MINI_NEST',
    'network_role': 'NODE',
    'capabilities': {'thermal': True, 'environmental': True, 'relay': True},
    'self_test': passed,
    'backhaul': 'NONE',
}
hub = {
    'device_id': HUB,
    'firmware_version': '0.7.0-test',
    'hardware_revision': 'HUB-V1',
    'product_model': 'NEST_HUB',
    'network_role': 'HUB',
    'capabilities': {'thermal': False, 'environmental': False, 'relay': True, 'lte': True},
    'self_test': {'storage': True, 'lora': True, 'thermal': False, 'environmental': False},
    'backhaul': 'LTE',
}

with TestClient(app) as client:
    with sqlite3.connect(legacy_path) as migrated:
        columns = {row[1] for row in migrated.execute('PRAGMA table_info(towers)')}
        assert {'product_model', 'network_role', 'lifecycle_state', 'capabilities_json'} <= columns
    assert client.post('/api/devices/register', json=node).status_code == 401
    first = client.post('/api/devices/register', json=node, headers=COLLECTOR)
    assert first.status_code == 200, first.text
    assert first.json()['created'] is True
    assert first.json()['lifecycle_state'] == 'READY_FOR_INSTALLATION'

    # Collector discovery without SELFTEST must not downgrade a passed device.
    discovery = dict(node); discovery.pop('self_test')
    again = client.post('/api/devices/register', json=discovery, headers=COLLECTOR)
    assert again.status_code == 200, again.text
    assert again.json()['created'] is False
    assert again.json()['lifecycle_state'] == 'READY_FOR_INSTALLATION'

    assert client.post('/api/devices/register', json=hub, headers=COLLECTOR).status_code == 200
    second_hub = dict(hub); second_hub['device_id'] = HUB2
    assert client.post('/api/devices/register', json=second_hub, headers=COLLECTOR).status_code == 200
    assigned = client.patch('/api/devices/' + NODE,
        json={'primary_hub_id': HUB, 'secondary_hub_id': HUB2}, headers=OPERATOR)
    assert assigned.status_code == 200, assigned.text
    assert assigned.json()['primary_hub_id'] == HUB
    assert assigned.json()['secondary_hub_id'] == HUB2

    bad = client.patch('/api/devices/' + HUB,
        json={'primary_hub_id': NODE}, headers=OPERATOR)
    assert bad.status_code == 422

    devices = client.get('/api/devices', headers=OPERATOR)
    assert devices.status_code == 200 and len(devices.json()) == 3
    dashboard = client.get('/api/towers/live')
    assert dashboard.status_code == 200, dashboard.text
    by_id = {row['id']: row for row in dashboard.json()['towers']}
    assert by_id[NODE]['product_model'] == 'MINI_NEST'
    assert by_id[HUB]['network_role'] == 'HUB'
    assert by_id[HUB]['capabilities']['lte'] is True

    revoked = client.patch('/api/devices/' + NODE,
        json={'lifecycle_state': 'REVOKED'}, headers=OPERATOR)
    assert revoked.status_code == 200
    assert client.post('/api/devices/register', json=node, headers=COLLECTOR).status_code == 403
print('DEVICE_REGISTRY_PASS provisioning + identity + lifecycle + hub assignment')
'''


class DeviceRegistryTests(unittest.TestCase):
    def test_registry_in_isolated_process(self):
        with tempfile.TemporaryDirectory(prefix='og-registry-') as tmp:
            env = os.environ.copy()
            env['DATABASE_URL'] = 'sqlite:///' + str(Path(tmp) / 'registry.sqlite3')
            env['OG_PILOT_DB'] = str(Path(tmp) / 'mesh.sqlite3')
            env['OG_CLIENT_TOKEN'] = 'registry-collector'
            env['OG_OPERATOR_TOKEN'] = 'registry-operator'
            env['ENABLE_HTTP_WHISPER'] = '0'
            env['PYTHONPATH'] = os.pathsep.join((str(ROOT), str(ROOT / 'backend')))
            result = subprocess.run(
                [sys.executable, '-c', SCRIPT], cwd=ROOT / 'backend', env=env,
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + '\n' + result.stderr)
            self.assertIn('DEVICE_REGISTRY_PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
