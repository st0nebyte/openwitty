#!/usr/bin/env python3
# Mono/stereo WAV (PCM 16/24/32 or IEEE float 32, incl. WAVE_FORMAT_EXTENSIBLE): per channel level, 1 kHz / 2 kHz
# content, and a 0.5 s timeline of RMS. Usage: wavinfo.py file.wav [file.wav ...]
import numpy as np, struct, sys
def read(path):
    b=open(path,'rb').read(); assert b[:4]==b'RIFF' and b[8:12]==b'WAVE'
    i=12; fmt=None; data=None
    while i+8<=len(b):
        cid=b[i:i+4]; n=struct.unpack('<I',b[i+4:i+8])[0]; body=b[i+8:i+8+n]
        if cid==b'fmt ': fmt=body
        if cid==b'data': data=body
        i+=8+n+(n&1)
    tag,ch,sr=struct.unpack('<HHI',fmt[:8]); bits=struct.unpack('<H',fmt[14:16])[0]
    if tag==0xFFFE: tag=struct.unpack('<H',fmt[24:26])[0]
    if tag==3: x=np.frombuffer(data,'<f4' if bits==32 else '<f8').astype(float)
    elif bits==24:
        a=np.frombuffer(data,np.uint8).reshape(-1,3); x=(a[:,0].astype(np.int32)|(a[:,1].astype(np.int32)<<8)|(a[:,2].astype(np.int32)<<16)); x=np.where(x>=1<<23,x-(1<<24),x)/2**23
    else: x=np.frombuffer(data,{16:'<i2',32:'<i4'}[bits]).astype(float)/2**(bits-1)
    return x.reshape(-1,ch),sr,tag,bits
db=lambda v: 20*np.log10(max(v,1e-12))
def tonedb(sig,sr,f):
    if len(sig)<sr//10: return -240
    w=np.hanning(len(sig)); sp=np.abs(np.fft.rfft(sig*w)); fr=np.fft.rfftfreq(len(sig),1/sr); k=np.argmin(abs(fr-f))
    return db(sp[max(0,k-3):k+4].max()*2/w.sum())
for path in sys.argv[1:]:
    x,sr,tag,bits=read(path)
    print(f"{path.split('/')[-1]}: {x.shape[1]} ch, {'float' if tag==3 else 'pcm'}{bits}, {sr} Hz, {len(x)/sr:.2f} s")
    for c in range(x.shape[1]):
        s=x[:,c]
        print(f"   ch{c+1}: rms {db(np.sqrt((s**2).mean())):7.1f}  peak {db(abs(s).max()):7.1f}  1kHz {tonedb(s,sr,1000):7.1f}  2kHz {tonedb(s,sr,2000):7.1f} dBFS")
        blk=sr//2; tl=[db(np.sqrt((s[i:i+blk]**2).mean())) for i in range(0,len(s)-blk+1,blk)]
        print("        timeline (0.5 s RMS dBFS):", " ".join(f"{v:.0f}" for v in tl))
