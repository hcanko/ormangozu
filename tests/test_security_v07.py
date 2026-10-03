"""Separate ingest/operation credentials and locked-down legacy mutating paths."""
from __future__ import annotations
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
CHECK=r'''
from fastapi.testclient import TestClient
from main import app
ING={'X-Client-Token':'collector-secret-only'}
OP={'X-Client-Token':'operator-secret-only'}
payload={'device_id':'NEST-001122AABBCC','record_kind':'mesh','type':'ALERT','boot':1,'sequence':1}
with TestClient(app) as c:
 assert c.post('/api/mesh/events',json=payload,headers=ING).status_code==200
 assert c.post('/api/mesh/events',json=payload,headers=OP).status_code==401
 assert c.get('/api/mesh/events',headers=ING).status_code==401
 assert c.get('/api/mesh/events',headers=OP).status_code==200
 control={'target_id':'NEST-001122AABBCC','opcode':'STATUS'}
 assert c.post('/api/control/commands',json=control,headers=ING).status_code==401
 assert c.post('/api/control/commands',json=control,headers=OP).status_code==200
 assert c.post('/api/import/device-log',headers=ING).status_code==401
 assert c.post('/api/settings/calibration',headers=ING).status_code==401
 assert c.post('/api/sensor-data',headers=ING,json={}).status_code==403
 assert c.get('/openapi.json').status_code==404
print('V07_OPERATOR_COLLECTOR_ISOLATION_PASS')
'''
class ApiHardeningTests(unittest.TestCase):
    def test_operator_and_ingest_isolation(self):
        with tempfile.TemporaryDirectory(prefix='og-sec-') as directory:
            e=os.environ.copy()
            e.update(DATABASE_URL='sqlite:///'+str(Path(directory)/'main.sqlite3'),
                OG_PILOT_DB=str(Path(directory)/'pilot.sqlite3'),
                OG_CLIENT_TOKEN='collector-secret-only',OG_OPERATOR_TOKEN='operator-secret-only',
                OG_ENABLE_LEGACY_SENSOR_HTTP='0',OG_ENABLE_API_DOCS='0')
            e['PYTHONPATH']=os.pathsep.join((str(ROOT),str(ROOT/'backend')))
            p=subprocess.run([sys.executable,'-c',CHECK],cwd=ROOT/'backend',env=e,
                text=True,capture_output=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            self.assertIn('V07_OPERATOR_COLLECTOR_ISOLATION_PASS',p.stdout)
if __name__=='__main__':unittest.main()
