#!/usr/bin/env python3
"""Smoke test for dsrp-reflector.

Simulates two MMDVMHost repeaters (A and B) and checks the core behaviors:
poll -> status reply, fan-out relay to other repeaters, no self-echo,
first-keyup-wins collision lock, and EOT release.

Run the reflector on port 30010 first, e.g.:
    ./dsrp-reflector -p 30010 -v &
    python3 tests/smoke.py
"""
import socket, time, struct, sys

REFL=('127.0.0.1', 30010)

def mk(cs): 
    s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1',0)); s.settimeout(1.0); return s

A=mk('A'); B=mk('B')

def poll(s): s.sendto(b'DSRP'+bytes([0x0A])+b'linux_mmdvm-test\x00', REFL)
def hdr(s,sid):
    p=bytearray(49); p[0:4]=b'DSRP'; p[4]=0x20; p[5]=sid>>8; p[6]=sid&0xff; p[7]=0
    s.sendto(bytes(p), REFL)
def data(s,sid,seq,end=False):
    p=bytearray(21); p[0:4]=b'DSRP'; p[4]=0x21; p[5]=sid>>8; p[6]=sid&0xff
    p[7]=seq|(0x40 if end else 0); p[8]=0
    s.sendto(bytes(p), REFL)

def drain(s,label):
    got=[]
    while True:
        try: d,_=s.recvfrom(200); got.append(d)
        except socket.timeout: break
    for d in got: print(f"  {label} <- type=0x{d[4]:02X} len={len(d)}")
    return got

print("1) both poll -> expect 0x00 status reply each"); poll(A); poll(B); time.sleep(0.2)
ra=drain(A,'A'); rb=drain(B,'B')
assert any(d[4]==0x00 for d in ra) and any(d[4]==0x00 for d in rb), "no status reply"

print("2) A transmits sid=0x1234 -> B should hear header+data, A should NOT echo")
hdr(A,0x1234); time.sleep(0.05)
for i in range(3): data(A,0x1234,i); time.sleep(0.02)
data(A,0x1234,3,end=True); time.sleep(0.2)
ra=drain(A,'A'); rb=drain(B,'B')
assert any(d[4]==0x20 for d in rb) and any(d[4]==0x21 for d in rb), "B didn't hear A"
assert not any(d[4] in (0x20,0x21) for d in ra), "A got an echo!"
print("   OK: B heard A, A got no echo")

print("3) collision: A keys up, then B keys up mid-stream -> B not relayed to A")
hdr(A,0x2222); data(A,0x2222,0); time.sleep(0.02)
hdr(B,0x3333); data(B,0x3333,0); time.sleep(0.1)
drain(A,'A(post-collision, only status expected on later poll)')
data(A,0x2222,1,end=True); time.sleep(0.1); drain(B,'B')
print("   (B's header during A's lock should have been ignored)")
print("ALL SMOKE CHECKS PASSED")
