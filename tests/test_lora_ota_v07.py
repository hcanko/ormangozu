"""PC release signing tests. Physical ESP flash/radio tests are separate."""
import json
import os
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from tools import lora_ota
from tools.rf_budget import airtime_seconds, minimum_maintenance_gap

NEST='NEST-001122AABBCC'
BRIDGE='NEST-66778899AABB'

class SignedOtaTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory(prefix='ogota-')
        self.addCleanup(self.folder.cleanup)
        self.d=Path(self.folder.name)
        self.private=self.d/'offline-ota-root.pem'
        self.img=self.d/'firmware.bin'
        self.img.write_bytes(bytes(range(256))*16)
        self.manifest=self.d/'signed-manifest.json'
        patcher=patch.dict(os.environ,{'OG_SIGN_PASSWORD':'test-only-strong-passphrase-42'})
        patcher.start();self.addCleanup(patcher.stop)
        lora_ota.keygen(SimpleNamespace(private_key=str(self.private)))
        lora_ota.sign(SimpleNamespace(private_key=str(self.private),firmware=str(self.img),
                   target=NEST,build=701,manifest=str(self.manifest)))
        self.m=json.loads(self.manifest.read_text())

    def test_authorized_manifest_and_chunk_wire_length(self):
        lora_ota.verify_manifest(self.m,self.img.read_bytes())
        body=lora_ota.wire('D',BRIDGE,self.m,'12345',
                        lora_ota.base64.b64encode(bytes(range(96))).decode('ascii'))
        self.assertLessEqual(len(body)+1+24,239)
        self.assertEqual(len(self.m['signature']),88)
        self.assertEqual(len(bytes.fromhex(self.m['public_key_hex'])),65)

    def test_refuse_modified_firmware_target_build_or_signature(self):
        original=self.img.read_bytes()
        for image in (original+b'!',original[:-1]+b'Z'):
            with self.assertRaises(ValueError):lora_ota.verify_manifest(self.m,image)
        for key,value in [('target','NEST-AAAAAAAAAAAA'),('build',702),('sha256','0'*64),('size',123)]:
            corrupt={**self.m,key:value}
            with self.assertRaises(Exception):lora_ota.verify_manifest(corrupt,original)
        signature={**self.m,'signature':lora_ota.base64.b64encode(bytes(64)).decode()}
        with self.assertRaises(Exception):lora_ota.verify_manifest(signature,original)

    def test_release_keys_are_not_overwritten(self):
        with self.assertRaises(ValueError):lora_ota.keygen(SimpleNamespace(private_key=str(self.private)))
        with self.assertRaises(ValueError):lora_ota.sign(SimpleNamespace(private_key=str(self.private),
                 firmware=str(self.img),target=NEST,build=701,manifest=str(self.manifest)))

    def test_rf_budget_for_96_byte_chunk(self):
        toa=airtime_seconds(216)
        self.assertGreater(toa,1)
        self.assertLess(toa,2)
        self.assertLess(minimum_maintenance_gap(216),180)
        self.assertGreater(minimum_maintenance_gap(216,sf=12),180)

if __name__=='__main__':unittest.main()
