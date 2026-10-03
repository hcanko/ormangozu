import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "backend"))
from collect_usb import parse_serial_line
from decode_frames import HEADER, HEADER_V3, PIXELS, MAGIC, crc16, read_frames
from whisper_protocol import Packet, ReplayWindow, encode, decode, ProtocolError, matches_ack

KEY = "PILOT_ONLY_TEST_SHARED_KEY_32_CHARACTERS"


class ProtocolTests(unittest.TestCase):
    def test_alert_ack_report_round_trip(self):
        a = Packet("ALERT", "NEST-001122334455", 9, "*", 9, 34, level=2, score=71.2,
                   max_temp=67.4, cluster=8, persistence=3)
        wire = encode(a, KEY)
        self.assertEqual(decode(wire, KEY), a)
        self.assertLess(len(wire), 240)
        ack = Packet("ACK", "NEST-66778899AABB", 5, "NEST-001122334455", 9, 34)
        self.assertTrue(matches_ack(decode(encode(ack, KEY), KEY), a))
        report = Packet("REPORT", "NEST-66778899AABB", 5, "NEST-001122334455", 9, 34, level=1, score=41.0)
        self.assertEqual(decode(encode(report, KEY), KEY).event_boot, a.event_boot)
        # ACK for a REPORT echoes ORIGINAL event boot, not the reporter's boot.
        report_ack = Packet("ACK", "NEST-001122334455", 9, "NEST-66778899AABB", 9, 34)
        self.assertTrue(matches_ack(report_ack, report))

    def test_mac_tamper_and_old_protocol_rejected(self):
        msg = encode(Packet("ALERT", "NEST-001122334455", 3, "*", 3, 7), KEY)
        with self.assertRaises(ProtocolError): decode(msg.replace("|7|", "|9|"), KEY)
        with self.assertRaises(ProtocolError): decode("OG3|ALERT|NEST-A", KEY)
        with self.assertRaises(ProtocolError): decode(msg, "X"*32)
        with self.assertRaises(ProtocolError):
            encode(Packet("ALERT", "NEST-UNVERIFIED", 2, "*", 2, 1), KEY)

    def test_og4_authenticated_bounded_hop(self):
        p=Packet("ALERT", "NEST-001122334455", 2, "*", 2, 5, hop=1)
        msg=encode(p,KEY)
        self.assertEqual(decode(msg,KEY).hop,1)
        fields=msg.split('|')
        fields[-2]='2'  # authenticated hop must be MAC-covered
        with self.assertRaises(ProtocolError):decode('|'.join(fields),KEY)
        with self.assertRaises(ProtocolError):encode(Packet("ALERT", "NEST-001122334455", 2, "*", 2, 5,hop=3),KEY)
        with self.assertRaises(ProtocolError):decode(msg.replace('OG4|','OG3|',1),KEY)

    def test_replay_and_reset(self):
        replay = ReplayWindow()
        a = Packet("ALERT", "NEST-001122334455", 10, "*", 10, 90)
        self.assertTrue(replay.fresh(a))
        self.assertFalse(replay.fresh(a))
        self.assertFalse(replay.fresh(Packet("ALERT", "NEST-001122334455", 9, "*", 9, 500)))
        self.assertTrue(replay.fresh(Packet("ALERT", "NEST-001122334455", 11, "*", 11, 1)))
        # B is still boot 5, but A restarts its event counter on boot 12.
        r1 = Packet("REPORT", "NEST-66778899AABB", 5, "NEST-001122334455", 11, 125)
        r2 = Packet("REPORT", "NEST-66778899AABB", 5, "NEST-001122334455", 12, 1)
        self.assertTrue(replay.fresh(r1))
        self.assertTrue(replay.fresh(r2))
        self.assertFalse(replay.fresh(r1))

    def test_usb_line(self):
        telemetry = parse_serial_line('OGTELEM:{"device_id":"NEST-001122334455","boot":12,"sequence":43,"max_temp":50.4}')
        self.assertEqual(telemetry["record_kind"], "telemetry")
        self.assertEqual(telemetry["sequence"], 43)
        out = parse_serial_line('OGMESH:{"device_id":"NEST-001122334455","type":"REPORT","sequence":23}')
        self.assertEqual(out["record_kind"], "mesh")
        self.assertIsNotNone(out["received_at"])
        self.assertIsNone(parse_serial_line("[CORE0] boot"))

    def test_backend_record_and_ground_truth(self):
        from api import mesh_events as m
        from fastapi import HTTPException
        from fastapi.routing import APIRoute
        from unittest.mock import patch
        envpatch=patch.dict(os.environ, {"OG_CLIENT_TOKEN":"only-local-dev-test-token",
                                        "OG_OPERATOR_TOKEN":"only-local-dev-test-operator"})
        envpatch.start()
        self.addCleanup(envpatch.stop)
        original_path=m.DB_PATH
        self.addCleanup(setattr,m,"DB_PATH",original_path)
        with tempfile.TemporaryDirectory() as d:
            m.DB_PATH = Path(d) / "pilot.sqlite3"
            event = m.MeshEvent(device_id="NEST-001122334455", record_kind="mesh", type="ALERT",
                                received_at="2026-10-02T12:00:00+00:00", boot=9, sequence=23)
            with self.assertRaises(HTTPException): m.ingest(event, x_client_token="wrong")
            first = m.ingest(event, x_client_token=os.environ["OG_CLIENT_TOKEN"])
            second = m.ingest(event, x_client_token=os.environ["OG_CLIENT_TOKEN"])
            self.assertFalse(first["duplicate"])
            self.assertTrue(second["duplicate"])
            self.assertEqual(first["id"], second["id"])
            m.set_truth(m.EventLabel(event_id=first["id"], truth="controlled_test", notes="heated object"),
                        x_client_token=os.environ["OG_OPERATOR_TOKEN"])
            exported = m.export_csv(x_client_token=os.environ["OG_OPERATOR_TOKEN"]).body.decode()
            self.assertIn("controlled_test", exported)
            self.assertIn("NEST-001122334455", exported)

    def test_thermal_frame_decode_and_integrity(self):
        payload = PIXELS.pack(*([253] * 768))
        header = HEADER.pack(MAGIC, 2, 768, 42, 30000, 280, 230, 6, 15, 10, 1, 2, crc16(payload))
        decoded = list(read_frames(header + payload, include_pixels=True))
        self.assertEqual(len(decoded), 1)
        self.assertEqual(decoded[0]["pixels_c"][10], 25.3)
        self.assertEqual(decoded[0]["format_version"], 2)
        self.assertIsNone(decoded[0]["boot"])
        newer = HEADER_V3.pack(MAGIC, 3, 768, 12, 43, 30200, 286, 235, 7, 15, 10, 2, 3, crc16(payload))
        newer_frames = list(read_frames(header + payload + newer + payload, include_pixels=False))
        self.assertEqual(len(newer_frames), 2)
        self.assertEqual(newer_frames[1]["boot"], 12)
        self.assertEqual(newer_frames[1]["sequence"], 43)
        self.assertEqual(newer_frames[1]["format_version"], 3)
        with self.assertRaises(ValueError): list(read_frames((header + payload)[:-1]))
        damaged = bytearray(header + payload)
        damaged[-1] ^= 1
        with self.assertRaises(ValueError): list(read_frames(bytes(damaged)))

    def test_fusion_usb_and_forward_compatible_storage(self):
        from api import mesh_events as m
        sample = parse_serial_line(
            'OGFUSION:{"device_id":"NEST-AAA","boot":9,"sequence":34,'
            '"origin_boot":9,"type":"FUSION","result":"NETWORK_CORROBORATED",'
            '"peer":"NEST-BBB","score":67.5,"peer_score":72.2}'
        )
        self.assertEqual(sample["record_kind"], "fusion")
        self.assertEqual(sample["result"], "NETWORK_CORROBORATED")
        self.assertEqual(sample["peer_score"], 72.2)
        evt = m.MeshEvent(**sample)
        self.assertEqual(evt.model_dump()["peer_score"], 72.2)
        from unittest.mock import patch
        envpatch=patch.dict(os.environ, {"OG_CLIENT_TOKEN":"only-local-dev-test-token",
                                        "OG_OPERATOR_TOKEN":"only-local-dev-test-operator"})
        envpatch.start()
        self.addCleanup(envpatch.stop)
        original_path=m.DB_PATH
        self.addCleanup(setattr,m,"DB_PATH",original_path)
        with tempfile.TemporaryDirectory() as d:
            m.DB_PATH = Path(d) / "pilot.sqlite3"
            first = m.ingest(evt, x_client_token=os.environ["OG_CLIENT_TOKEN"])
            again = m.ingest(evt, x_client_token=os.environ["OG_CLIENT_TOKEN"])
            self.assertFalse(first["duplicate"])
            self.assertTrue(again["duplicate"])
            rows = m.list_events(limit=100, x_client_token=os.environ["OG_OPERATOR_TOKEN"])
            self.assertEqual(rows[0]["peer_score"], 72.2)
            self.assertEqual(rows[0]["truth"], "")

    def test_replay_preserves_offline_file_and_retries(self):
        from tools.collect_usb import replay_existing
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as d:
            f = Path(d) / "usb.jsonl"
            f.write_text('{"device_id":"NEST-AAA","record_kind":"telemetry",'
                         '"boot":7,"sequence":3,"received_at":"2026-10-02T00:00:00+00:00"}\n')
            sent_items = []
            with patch('tools.collect_usb.forward', side_effect=lambda r, u, t: sent_items.append(r)):
                sent, failed = replay_existing(f, 'http://127.0.0.1:8000', 'demo-token')
                self.assertEqual((sent, failed), (1, 0))
            self.assertEqual(sent_items[0]["sequence"], 3)
            self.assertIn('NEST-AAA', f.read_text())  # NEVER erase a local history on replay

    def test_partition_sizes(self):
        for filename, flash in [("partitions_8mb.csv", 0x800000), ("partitions_16mb.csv", 0x1000000)]:
            partitions = []
            for line in (ROOT / "OrmanGozu_Node" / filename).read_text().splitlines():
                if not line or line.startswith("#"): continue
                fields = [f.strip() for f in line.split(",")]
                partitions.append((fields[0], int(fields[3], 0), int(fields[4], 0)))
            for (_, a, n), (_, b, _) in zip(partitions, partitions[1:]):
                self.assertLessEqual(a + n, b)
            self.assertLessEqual(partitions[-1][1] + partitions[-1][2], flash)


if __name__ == "__main__":
    unittest.main()
