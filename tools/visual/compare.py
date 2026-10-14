#!/usr/bin/env python3
"""Visual comparison of our Qt rendering against reference screenshots (local, git-ignored).

usage: compare.py OURS.png REF.jpg REF_KIND OUTPREFIX
  OURS.png : screenshot taken with `lewitt-control-center --mock --size 720x401 --screenshot`
             (28 px simulation banner on top is cropped; stage = 720x373)
  REF_KIND : guide (720x373 official guide, CC 4.0.18) | bonedo (5.x retina capture)
             | delamar (5.2.1 capture)
Reports region mean colours and vertical divider positions in the 720x373 design space and
writes OUTPREFIX_side.png (ref | ours | abs-diff). Third-party references are never
committed; this tool only reads them.
"""
import sys, json
import numpy as np
from PIL import Image, ImageChops

W, H = 720, 373
CROPS = {   # content area (x0,y0,x1,y1) of each reference, excluding window chrome
    'guide':  (0, 0, 720, 373),
    'bonedo': (0, 90, 1548, 880),
    'delamar': (0, 42, 1500, 803),
}
REGIONS = {  # design-space rectangles (x0,y0,x1,y1) chosen away from text/icons
    'header':        (270, 4, 440, 46),
    'inputs_panel':  (4, 82, 20, 300),
    'outputs_panel': (318, 82, 330, 300),
    'monitoring':    (570, 82, 580, 300),
    'footer':        (200, 349, 520, 370),
    'section_bar_in':(240, 54, 300, 74),
    'section_bar_out':(330, 54, 400, 74),
}
DIVIDERS_EXPECTED = [155, 310, 395, 480, 565]

def load_ours(path):
    im = Image.open(path).convert('RGB')
    return im.crop((0, 28, 720, 28 + 373)).resize((W, H)) if im.size == (720, 401) else \
           im.crop((0, 28, im.width, im.height)).resize((W, H), Image.LANCZOS)

def load_ref(path, kind):
    im = Image.open(path).convert('RGB').crop(CROPS[kind])
    return im.resize((W, H), Image.LANCZOS)

def mean(a, r):
    x0, y0, x1, y1 = r
    return a[y0:y1, x0:x1].reshape(-1, 3).mean(axis=0)

def dividers(a):
    lum = a.mean(axis=2)
    band = lum[90:330].mean(axis=0)
    d = np.abs(np.diff(band))
    # candidate columns near the expected positions: strongest gradient within +-6 px
    out = []
    for e in DIVIDERS_EXPECTED:
        lo, hi = max(1, e - 6), min(W - 2, e + 6)
        out.append(int(lo + np.argmax(d[lo:hi])))
    return out

def hexc(c): return '#%02x%02x%02x' % tuple(int(round(v)) for v in c)

def main(ours_p, ref_p, kind, outp):
    ours, ref = load_ours(ours_p), load_ref(ref_p, kind)
    ao, ar = np.asarray(ours).astype(float), np.asarray(ref).astype(float)
    rep = {'regions': {}, 'dividers': {'expected': DIVIDERS_EXPECTED, 'ref': dividers(ar), 'ours': dividers(ao)}}
    for k, r in REGIONS.items():
        mo, mr = mean(ao, r), mean(ar, r)
        rep['regions'][k] = {'ref': hexc(mr), 'ours': hexc(mo), 'max_channel_delta': float(np.abs(mo - mr).max())}
    diff = ImageChops.difference(ours, ref)
    rep['mean_abs_diff_0_255'] = float(np.asarray(diff).mean())
    side = Image.new('RGB', (W * 3, H))
    side.paste(ref, (0, 0)); side.paste(ours, (W, 0)); side.paste(diff, (2 * W, 0))
    side.save(outp + '_side.png')
    print(json.dumps(rep, indent=2))

if __name__ == '__main__':
    main(*sys.argv[1:5])
