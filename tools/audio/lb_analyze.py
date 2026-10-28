#!/usr/bin/env python3
# OpenWitty LB tests (docs/FEATURE_COMPLETION_ROADMAP.md, LB-2). Usage: lb_analyze.py <4-channel WAV>
# Per-channel level of a raw 4-ch WAV in the L-tone window and the R-tone window, with 1 kHz / 2 kHz detection.
import numpy as np, wave, sys
w=wave.open(sys.argv[1]); n=w.getnchannels(); sw=w.getsampwidth(); sr=w.getframerate(); b=w.readframes(w.getnframes())
dt={2:'<i2',4:'<i4'}[sw]; x=np.frombuffer(b,dt).reshape(-1,n).astype(float)/(2**(8*sw-1))
print(f"file: {n} ch, {sw*8} bit, {sr} Hz, {len(x)/sr:.2f} s")
def db(v): return 20*np.log10(max(v,1e-12))
def band(sig,f):
    sp=np.abs(np.fft.rfft(sig*np.hanning(len(sig)))); fr=np.fft.rfftfreq(len(sig),1/sr); k=np.argmin(abs(fr-f)); return db(sp[k-3:k+4].max()*2/np.hanning(len(sig)).sum())
# locate the tone windows by the loudest 1 kHz / 2 kHz energy over time (robust to start offset)
blk=sr//10
def track(f):
    e=[band(x[i:i+blk].sum(1),f) for i in range(0,len(x)-blk,blk)]; return np.array(e)
for f,name in [(1000,'L tone 1 kHz'),(2000,'R tone 2 kHz')]:
    e=track(f); i=int(np.argmax(e)); seg=x[max(0,(i-10)*blk):(i+10)*blk]
    print(f"{name}: strongest at {i*blk/sr:.1f} s")
    for c in range(n): print(f"   ch{c+1}: tone {band(seg[:,c],f):7.1f} dBFS   rms {db(np.sqrt((seg[:,c]**2).mean())):7.1f} dBFS   peak {db(abs(seg[:,c]).max()):7.1f}")
print("whole file per channel: " + "  ".join(f"ch{c+1} rms {db(np.sqrt((x[:,c]**2).mean())):.1f} peak {db(abs(x[:,c]).max()):.1f}" for c in range(n)))
