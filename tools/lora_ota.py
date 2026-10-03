#!/usr/bin/env python3
"""Sign and transfer an Orman Gozu firmware via a USB-attached LoRa Nest.

Private key NEVER travels to a Nest. A target will not install an image unless
its SHA256 matches the offline ECDSA P-256 signature bound to that target,
transfer ID and monotonically increasing build number.

This is a conservative STOP-AND-WAIT pilot. RF duty-cycle / regional
requirements MUST be configured and verified before field use. Sending 1 MiB
can take days at a low legally permitted duty cycle, not minutes.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import os
import json
import re
import secrets
import sys
import time
from pathlib import Path

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils
from cryptography.hazmat.primitives.asymmetric.ec import EllipticCurvePublicKey

ID_RE = re.compile(r"NEST-[0-9A-F]{12}\Z")
CHUNK_BYTES = 96
WIRE_LIMIT = 239  # includes the device-added 24-char HMAC and delimiter


def canonical(m: dict) -> bytes:
    return (f"OGOTA1|{m['target']}|{m['tid']}|{int(m['build'])}|"
            f"{int(m['size'])}|{m['sha256']}").encode("ascii")


def point_hex(pub: EllipticCurvePublicKey) -> str:
    return pub.public_bytes(serialization.Encoding.X962,
                            serialization.PublicFormat.UncompressedPoint).hex()


def verify_manifest(m: dict, image: bytes) -> None:
    conditions = (
        (m.get('format') == 'OGOTA1', "Invalid signed manifest format"),
        (bool(ID_RE.fullmatch(m['target'])), "Invalid target ID"),
        (bool(re.fullmatch(r'[0-9a-f]{8}', m['tid'])), "Invalid transfer ID"),
        (0 < len(image) <= 1536 * 1024, "Image exceeds pilot staging cap"),
        (m['size'] == len(image), "Size mismatch"),
        (m['sha256'] == hashlib.sha256(image).hexdigest(), "SHA mismatch"),
        (700 < int(m['build']) < 2**32, "Build must increase and fit uint32"),
    )
    for ok,message in conditions:
        if not ok: raise ValueError(message)
    raw = base64.b64decode(m['signature'], validate=True)
    if len(raw) != 64: raise ValueError("Expected raw ECDSA r||s, 64 bytes")
    r, s = int.from_bytes(raw[:32], 'big'), int.from_bytes(raw[32:], 'big')
    key = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), bytes.fromhex(m['public_key_hex']))
    key.verify(utils.encode_dss_signature(r,s), canonical(m), ec.ECDSA(hashes.SHA256()))


def keygen(args):
    path = Path(args.private_key)
    if path.exists() or path.with_suffix('.pubhex').exists():
        raise ValueError('Refusing to overwrite signing keys')
    password = os.environ.get('OG_SIGN_PASSWORD', '')
    if len(password)<12: raise ValueError('Set OG_SIGN_PASSWORD to a strong 12+ character passphrase')
    key = ec.generate_private_key(ec.SECP256R1())
    data = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                             serialization.BestAvailableEncryption(password.encode('utf8')))
    path.write_bytes(data)
    try: path.chmod(0o600)
    except OSError: pass
    pub = point_hex(key.public_key())
    path.with_suffix('.pubhex').write_text(pub+'\n',encoding='ascii')
    print('Private signing key:',path,'(KEEP OFF NODE, OFF GIT)')
    print('Public point (provision in OG_OTA_PUBLIC_KEY_HEX):',pub)


def sign(args):
    image = Path(args.firmware).read_bytes()
    if not ID_RE.fullmatch(args.target): raise ValueError('Invalid target ID')
    if not 0 < len(image) <= 1536*1024: raise ValueError('Firmware exceeds OTA staging cap')
    password = os.environ.get('OG_SIGN_PASSWORD', '')
    if not password: raise ValueError('Set OG_SIGN_PASSWORD (never place signing key password in CLI history)')
    key=serialization.load_pem_private_key(Path(args.private_key).read_bytes(),
                                           password=password.encode('utf8'))
    if not isinstance(key,ec.EllipticCurvePrivateKey) or not isinstance(key.curve,ec.SECP256R1):
        raise ValueError('Require ECDSA P-256 private key')
    m={'format':'OGOTA1','target':args.target,'tid':secrets.token_hex(4),
       'build':args.build,'size':len(image),'sha256':hashlib.sha256(image).hexdigest(),
       'public_key_hex':point_hex(key.public_key())}
    der=key.sign(canonical(m),ec.ECDSA(hashes.SHA256()))
    r,s=utils.decode_dss_signature(der)
    m['signature']=base64.b64encode(r.to_bytes(32,'big')+s.to_bytes(32,'big')).decode('ascii')
    verify_manifest(m,image)
    destination=Path(args.manifest)
    if destination.exists(): raise ValueError('Manifest exists; will not overwrite signed release')
    destination.write_text(json.dumps(m,indent=2)+'\n',encoding='utf8')
    print('Signed',m['target'],'build',m['build'],'transfer',m['tid'],'size',len(image),'manifest',destination)


def wire(kind: str,bridge: str,m:dict,param:str,data:str)->str:
    body=f"OGU1|{kind}|{bridge}|{m['target']}|{m['tid']}|{param}|{data}|0"
    if len(body)+1+24 >= WIRE_LIMIT:
        raise ValueError(f'{kind} LoRa frame too long: {len(body)+25}')
    return body


def parse_reply(line:str,m:dict,bridge:str)->dict|None:
    if not line.startswith('OGOTA:'): return None
    obj=json.loads(line[len('OGOTA:'):])
    if (obj.get('tid')!=m['tid'] or obj.get('device_id')!=bridge or
        obj.get('source_device_id')!=m['target']):return None
    return obj


def send(args):
    import serial  # optional: signing/keygen do not require a COM port
    m=json.loads(Path(args.manifest).read_text(encoding='utf8'))
    image=Path(args.firmware).read_bytes()
    verify_manifest(m,image)
    if args.min_interval < 180:
        raise ValueError('SF9 pilot requires >=180 sec OTA TX pacing; recalculate for other RF settings')
    print('RF DISCLAIMER: operator must validate frequency, LBT/duty cycle, airtime, antenna and power.')
    print('At low permitted duty cycles a full OTA can take many DAYS. Alerts always take priority.')
    with serial.Serial(args.port,115200,timeout=.25,write_timeout=3) as port:
        bridge=''
        until=time.monotonic()+10
        while time.monotonic()<until and not bridge:
            port.write(b'WHOAMI\n');port.flush()
            raw=port.readline().decode('utf8',errors='replace').strip()
            if raw.startswith('OGIDENT:'):
                candidate=json.loads(raw[len('OGIDENT:'):]).get('device_id','')
                if ID_RE.fullmatch(candidate):bridge=candidate
        if not bridge: raise TimeoutError('USB-connected Nest did not answer WHOAMI')
        if bridge==m['target']: raise ValueError('Target is the USB bridge itself; use local maintenance OTA')
        last_tx=0.0

        def exchange(body:str, statuses:set[str], *,tries=5,timeout=None)->dict:
            nonlocal last_tx
            # A relay must pace forwarding the data AND the reverse response;
            # round-trip can be very long even for a tiny signed control frame.
            if timeout is None:
                timeout=(2*args.relay_hops+2)*args.min_interval+30
            for attempt in range(tries):
                pause=args.min_interval-(time.monotonic()-last_tx)
                if pause>0:time.sleep(pause)
                port.write(('OTA '+body+'\n').encode('ascii'));port.flush()
                last_tx=time.monotonic()
                deadline=time.monotonic()+timeout
                while time.monotonic()<deadline:
                    line=port.readline().decode('utf8',errors='replace').strip()
                    if not line:continue
                    response=parse_reply(line,m,bridge)
                    if response:
                        if response.get('status') in statuses:return response
                        raise RuntimeError('Nest OTA rejected transfer: '+str(response))
                    if line.startswith('[OTA]') or line.startswith('OGMESH:'):continue
                print('Timeout; retry',attempt+1,file=sys.stderr)
            raise TimeoutError('OTA peer unreachable; staging remains resumable on Nest')

        r=exchange(wire('B',bridge,m,str(m['build']),str(m['size'])+','+m['sha256']),
                   {'NEED_SIG','ALREADY_FLASHED','BAD_MANIFEST'})
        if r['status']=='BAD_MANIFEST':
            # If an X reply was lost after a successful reboot, the newly
            # booted firmware MUST reject the now-current (not newer) build.
            verified=exchange(wire('Q',bridge,m,'0','-'),{'CURRENT_'+str(m['build'])},tries=3)
            print('Reconnected after completed OTA. Remote build:',verified['status'])
            return
        if r['status']=='ALREADY_FLASHED':
            offset=len(image)
            print('Previously verified image is already in inactive OTA slot; skip redundant flashing.')
        else:
            r=exchange(wire('S',bridge,m,'0',m['signature']),{'READY'})
            offset=int(r['offset'])
        if offset<0 or offset>len(image):
            raise ValueError('Target reported invalid resume offset')
        print('Resume at',offset,'of',len(image))
        while offset<len(image):
            chunk=image[offset:offset+CHUNK_BYTES]
            encoded=base64.b64encode(chunk).decode('ascii')
            r=exchange(wire('D',bridge,m,str(offset),encoded),{'NEXT','OFFSET'})
            next_off=int(r['offset'])
            if not 0<=next_off<=len(image):raise ValueError('Bad next offset')
            if next_off==offset:raise RuntimeError('Nest did not accept chunk')
            offset=next_off
            if offset%16384 < CHUNK_BYTES:print(f'OTA {offset}/{len(image)} bytes')
        if r['status']!='ALREADY_FLASHED':
            r=exchange(wire('E',bridge,m,'0','-'),{'FLASHED'})
        print('Image written into inactive OTA partition. Reboot command follows.')
        try:
            r=exchange(wire('X',bridge,m,'0','-'),{'REBOOTING'})
            print('Nest confirmed pending reboot.')
        except (TimeoutError,RuntimeError):
            # An X reply can disappear exactly as the target reboots.
            print('Reboot acknowledgement uncertain; query actual firmware build.',file=sys.stderr)
        time.sleep(13)
        r=exchange(wire('Q',bridge,m,'0','-'),{'CURRENT_'+str(m['build']),'FLASHED'},tries=3)
        if r['status']=='FLASHED':
            print('Image verified in OTA slot but target has not rebooted; retry reboot.')
            exchange(wire('X',bridge,m,'0','-'),{'REBOOTING'},tries=3)
            time.sleep(13)
            r=exchange(wire('Q',bridge,m,'0','-'),{'CURRENT_'+str(m['build'])},tries=3)
        print('Post-reboot firmware build verified on remote Nest:',r['status'])


def send_local(args):
    """Signed OTA via maintenance AP, still using exactly the remote manifest rules.

    Connect the computer to the target Nest's OG-MAINT Wi-Fi (MAINT ON physical
    USB) and set OG_MAINT_HTTP_PASSWORD in the environment. HTTPS is not offered
    on the local ESP HTTP server: rely on the short WPA2 maintenance window.
    """
    import urllib.parse
    import urllib.request
    m=json.loads(Path(args.manifest).read_text(encoding='utf8'))
    image=Path(args.firmware).read_bytes()
    verify_manifest(m,image)
    password=os.environ.get('OG_MAINT_HTTP_PASSWORD','')
    if len(password)<12:raise ValueError('Set OG_MAINT_HTTP_PASSWORD for target maintenance AP')
    if args.url.rstrip('/')!='http://192.168.4.1':
        raise ValueError('Signed local OTA is restricted to http://192.168.4.1 maintenance AP')
    authorization='Basic '+base64.b64encode(('nest:'+password).encode()).decode('ascii')
    def exchange(kind, param, data, statuses):
        fields=urllib.parse.urlencode({'kind':kind,'tid':m['tid'],'param':str(param),'data':data})
        req=urllib.request.Request(args.url+'/ota/step',data=fields.encode('ascii'),method='POST',
                headers={'Authorization':authorization,'Content-Type':'application/x-www-form-urlencoded'})
        with urllib.request.urlopen(req,timeout=120 if kind=='E' else 15) as result:
            r=json.load(result)
        if r['status'] not in statuses:raise RuntimeError('Nest rejected signed OTA: '+str(r))
        return r
    r=exchange('B',m['build'],str(m['size'])+','+m['sha256'],
               {'NEED_SIG','ALREADY_FLASHED','BAD_MANIFEST'})
    if r['status']=='BAD_MANIFEST':
        q=exchange('Q',0,'-',{'CURRENT_'+str(m['build'])})
        print('Already booted signed build:',q['status'])
        return
    if r['status']=='ALREADY_FLASHED':offset=len(image)
    else:
        r=exchange('S',0,m['signature'],{'READY'})
        offset=int(r['offset'])
    while offset<len(image):
        encoded=base64.b64encode(image[offset:offset+CHUNK_BYTES]).decode('ascii')
        r=exchange('D',offset,encoded,{'NEXT','OFFSET'})
        nex=int(r['offset'])
        if not offset<nex<=len(image):raise RuntimeError('Bad resume/step offset')
        offset=nex
        if offset%16384<CHUNK_BYTES:print(f'Signed local OTA {offset}/{len(image)}')
    if r['status']!='ALREADY_FLASHED':exchange('E',0,'-',{'FLASHED'})
    exchange('X',0,'-',{'REBOOTING'})
    print('Local signed OTA staged, SHA checked and flashed. Reconnect USB and WHOAMI to verify boot.')


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='cmd',required=True)
    a=sub.add_parser('keygen');a.add_argument('--private-key',required=True);
    a.set_defaults(fn=keygen)
    a=sub.add_parser('sign');a.add_argument('--private-key',required=True);
    a.add_argument('--firmware',required=True);a.add_argument('--target',required=True)
    a.add_argument('--build',required=True,type=int);a.add_argument('--manifest',required=True)
    a.set_defaults(fn=sign)
    a=sub.add_parser('send');a.add_argument('--manifest',required=True);a.add_argument('--firmware',required=True)
    a.add_argument('--port',required=True);a.add_argument('--relay-hops',type=int,choices=(0,1,2),default=0)
    a.add_argument('--min-interval',type=float,default=180,
       help='Minimum spacing of PC-origin transmissions in seconds: adapt only after local RF compliance calculation')
    a.set_defaults(fn=send)
    a=sub.add_parser('send-local');a.add_argument('--manifest',required=True);a.add_argument('--firmware',required=True)
    a.add_argument('--url',default='http://192.168.4.1');a.set_defaults(fn=send_local)
    args=p.parse_args(argv)
    try:args.fn(args)
    except (ValueError,AssertionError,TimeoutError,RuntimeError) as e:
        p.exit(2,'ERROR: '+str(e)+'\n')
if __name__=='__main__':main()
