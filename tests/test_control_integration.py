"""Isolated local PC control API test; never touches operator SQLite."""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = r'''
import os
from fastapi.testclient import TestClient
from main import app
from api.mesh_events import connection
from tools.collect_usb import parse_serial_line
H={'X-Client-Token':'local-test-secret'}
HO={'X-Client-Token':'local-test-operator'}
A='NEST-001122334455'; B='NEST-66778899AABB'; C='NEST-AAAAAAAAAAAA'
with TestClient(app) as c:
    assert c.post('/api/control/commands',json={'target_id':A,'opcode':'STATUS'}).status_code==401
    assert c.post('/api/towers/'+A+'/command?command=reboot&value=1').status_code==410
    assert c.post('/api/control/commands',json={'target_id':'*','opcode':'STATUS'},headers=HO).status_code==422
    assert c.post('/api/control/commands',json={'target_id':B,'opcode':'PAN','argument':181},headers=HO).status_code==422
    assert c.post('/api/control/commands',json={'target_id':B,'opcode':'STATUS','argument':1},headers=HO).status_code==422
    j=c.post('/api/control/commands',json={'target_id':B,'opcode':'STATUS'},headers=HO)
    assert j.status_code==200,j.text
    ident=j.json()['id']; assert ident==1
    assert c.post('/api/control/commands',json={'target_id':A,'opcode':'SAMPLE'},headers=HO).status_code==409
    assert c.post('/api/control/next?bridge_device_id=BAD',json={},headers=H).status_code==422
    job=c.post(f'/api/control/next?bridge_device_id={A}',json={},headers=H)
    assert job.status_code==200 and job.json()['id']==ident,job.text
    assert c.post(f'/api/control/next?bridge_device_id={C}',json={},headers=H).json() is None
    result={'command_id':ident,'device_id':A,'target_id':B,'opcode':'STATUS','stage':'SENT','status':'ACKED'}
    assert c.post('/api/control/result',json={**result,'device_id':C},headers=H).status_code==409
    first=c.post('/api/control/result',json=result,headers=H)
    assert first.status_code==200 and first.json()['state']=='sent',first.text
    completed=c.post('/api/control/result',json={**result,'stage':'RESULT','status':'OK',
        'max_temp':49.1,'score':33.7,'battery_pct':68,'pan':90,'tilt':90,'manual':False},headers=H)
    assert completed.status_code==200 and completed.json()['state']=='completed',completed.text
    again=c.post('/api/control/result',json=result,headers=H)
    assert again.json()['duplicate_or_late'] is True and again.json()['state']=='completed'
    listing=c.get('/api/control/commands',headers=HO)
    assert listing.json()[0]['result_status']=='OK'
    assert c.post(f'/api/control/next?bridge_device_id={A}',json={},headers=H).json() is None
    rec=parse_serial_line('OGCTRL:{"device_id":"'+A+'","command_id":1,"target_id":"'+B+'","stage":"RESULT","opcode":"STATUS","status":"OK"}')
    assert rec['record_kind']=='control' and rec['command_id']==1
    assert c.post('/api/mesh/events',json=rec,headers=H).status_code==200
    assert c.post('/api/mesh/events',json=rec,headers=H).json()['duplicate'] is True
    j2=c.post('/api/control/commands',json={'target_id':B,'opcode':'MANUAL'},headers=HO)
    assert j2.status_code==200
    with connection() as db:
        db.execute("UPDATE control_command SET created_at='2000-01-01T00:00:00+00:00' WHERE id=?",(j2.json()['id'],))
    rows=c.get('/api/control/commands',headers=HO).json()
    assert rows[0]['state']=='timeout' and rows[0]['result_status']=='EXPIRED_OFFLINE'
    assert c.post(f'/api/control/next?bridge_device_id={A}',json={},headers=H).json() is None
    j3=c.post('/api/control/commands',json={'target_id':B,'opcode':'PAN','argument':100},headers=HO)
    assert j3.status_code==200
    assert c.post(f'/api/control/next?bridge_device_id={A}',json={},headers=H).json()['id']==j3.json()['id']
    with connection() as db:
        db.execute("UPDATE control_command SET claimed_at='2000-01-01T00:00:00+00:00' WHERE id=?",(j3.json()['id'],))
    assert c.get('/api/control/commands',headers=HO).json()[0]['state']=='timeout'
print('REMOTE_CONTROL_API_PASS auth + addressed jobs + dedup + expiry + USB JSON')
'''

class RemoteControlTests(unittest.TestCase):
    def test_pilot_commands_and_usb(self):
        with tempfile.TemporaryDirectory(prefix='og061-') as tmp:
            env=os.environ.copy()
            env['DATABASE_URL']='sqlite:///'+str(Path(tmp)/'core.sqlite3')
            env['OG_PILOT_DB']=str(Path(tmp)/'mesh.sqlite3')
            env['OG_CLIENT_TOKEN']='local-test-secret'
            env['OG_OPERATOR_TOKEN']='local-test-operator'
            env['ENABLE_HTTP_WHISPER']='0'
            env['PYTHONPATH']=os.pathsep.join((str(ROOT),str(ROOT/'backend')))
            run=subprocess.run([sys.executable,'-c',SCRIPT],cwd=ROOT/'backend',env=env,
                capture_output=True,text=True,timeout=35)
            self.assertEqual(run.returncode,0,run.stdout+'\n'+run.stderr)
            self.assertIn('REMOTE_CONTROL_API_PASS',run.stdout)
