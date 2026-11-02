#!/usr/bin/env python3
# OpenWitty LB tests (docs/dev/FEATURE_COMPLETION_ROADMAP.md, LB-2). Usage: lb_make_tones.py <out.wav> [rate, default 48000]
# 48 kHz stereo float WAV: 1 s silence, 4 s L-only 1 kHz, 1 s silence, 4 s R-only 2 kHz, 1 s silence; -30 dBFS peak.
import numpy as np, wave, sys
sr=int(sys.argv[2]) if len(sys.argv) > 2 else 48000; a=10**(-30/20)
def tone(f,s): t=np.arange(int(sr*s))/sr; x=a*np.sin(2*np.pi*f*t); r=int(0.01*sr); w=np.ones_like(x); w[:r]=np.linspace(0,1,r); w[-r:]=np.linspace(1,0,r); return x*w
z=lambda s: np.zeros(int(sr*s))
L=np.concatenate([z(1),tone(1000,4),z(1),z(4),z(1)]); R=np.concatenate([z(1),z(4),z(1),tone(2000,4),z(1)])
x=np.stack([L,R],1); pcm=(x*32767).astype('<i2')
with wave.open(sys.argv[1],'wb') as w: w.setnchannels(2); w.setsampwidth(2); w.setframerate(sr); w.writeframes(pcm.tobytes())
