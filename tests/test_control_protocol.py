import unittest
from tools.control_protocol import PilotReplay, command, tag, verify

KEY=b'dev-only-pilot-test-mesh-key-32bytes-012345'
A='NEST-001122334455'
B='NEST-66778899AABB'

class ControlFrameTests(unittest.TestCase):
    def test_address_mac_and_replay_reference(self):
        frame=command(A,5,B,1,'STATUS',0,KEY)
        self.assertTrue(frame.startswith('OGC2|CMD|'))
        self.assertEqual(len(frame.rsplit('|',1)[1]),24)
        self.assertLess(len(frame),240)
        decoded=verify(frame,KEY,B)
        self.assertEqual(decoded['opcode'],'STATUS')
        replay=PilotReplay()
        self.assertTrue(replay.accept(decoded))
        self.assertFalse(replay.accept(decoded))
        self.assertTrue(replay.accept(verify(command(A,5,B,2,'PAN',120,KEY),KEY,B)))
        self.assertFalse(replay.accept(verify(command(A,4,B,99,'PAN',120,KEY),KEY,B)))
        with self.assertRaises(ValueError): verify(frame,KEY,A)
        with self.assertRaises(ValueError): verify(frame.replace('STATUS','AUTO'),KEY,B)
        with self.assertRaises(ValueError): command(A,5,B,4,'PAN',190,KEY)
        with self.assertRaises(ValueError): command(A,5,B,4,'AUTO',15,KEY)
    def test_bounded_relay_hop_cannot_be_edited_without_control_key(self):
        frame=command(A,5,B,11,'FAST',0,KEY,hop=0)
        self.assertEqual(verify(frame,KEY,B)['hop'],0)
        with self.assertRaises(ValueError): verify(frame.rsplit('|',2)[0]+'|2|'+frame.rsplit('|',1)[-1],KEY,B)
        with self.assertRaises(ValueError): command(A,5,B,11,'FAST',0,KEY,hop=3)

    def test_result_body_24byte_mac_and_packet_limit(self):
        # Mirrors ESP queueControlResult fields: 17 body fields then signed MAC.
        body=f'OGC2|RESULT|{B}|7|{A}|5|4294967295|SAMPLE|SENSOR_FAULT|99.9|99.9|100|160|160|1|2|0'
        wire=body+'|'+tag(body,KEY)
        self.assertEqual(len(body.split('|')),17)
        self.assertLess(len(wire),240)
        self.assertEqual(tag(body,KEY),wire.rsplit('|',1)[1])

if __name__=='__main__': unittest.main()
