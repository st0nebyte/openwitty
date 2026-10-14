#!/usr/bin/env python3
"""Self-contained visual regression check (no third-party images needed).

Renders the app offscreen at 720x401 (28 px banner + 720x373 stage) and verifies
geometry/colour invariants that were *measured* from public screenshots of the
official Control Center 5.x (see docs/GUI_DESIGN.md). Tolerances reflect
screenshot scaling/JPEG error, so this is a guard against regressions, not a claim
of pixel accuracy.
usage: check_geometry.py PATH/TO/lewitt-control-center
"""
import os, subprocess, sys, tempfile
import numpy as np
from PIL import Image

# vertical dividers measured on the 5.x capture (design px, ref 153/307/393/480/566)
DIVIDERS = [154, 309, 394, 479, 564]
DIV_TOL = 4
# region -> (box, expected hex, tolerance per channel); regions chosen free of text/icons
REGIONS = {
    'header':      ((410, 6, 455, 44),   '#1e1d1f', 8),
    'inputs':      ((140, 90, 150, 170), '#3e3e3e', 8),
    'outputs':     ((312, 82, 318, 300), '#1f1d1e', 8),
    'monitoring':  ((568, 82, 575, 150), '#1f1d1e', 8),
    'footer':      ((300, 349, 520, 370), '#1e1d1e', 8),
}

def hexrgb(h): return np.array([int(h[i:i + 2], 16) for i in (1, 3, 5)])

def main(binary):
    out = os.path.join(tempfile.mkdtemp(), 'ours.png')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
    r = subprocess.run([binary, '--mock', '--size', '720x401', '--screenshot', out],
                       env=env, capture_output=True, text=True, timeout=60)
    if r.returncode != 0 or not os.path.exists(out):
        print('render failed', r.stderr[-400:]); return 2
    im = Image.open(out).convert('RGB')
    if im.size != (720, 401):
        print('unexpected size', im.size); return 2
    a = np.asarray(im.crop((0, 28, 720, 401))).astype(int)
    fails = []
    lum = a.mean(axis=2)[90:330].mean(axis=0)
    d = np.abs(np.diff(lum))
    for e in DIVIDERS:
        lo, hi = e - 8, e + 8
        got = int(lo + np.argmax(d[lo:hi]))
        if abs(got - e) > DIV_TOL: fails.append(f'divider {e}: found {got}')
    for k, (box, hx, tol) in REGIONS.items():
        x0, y0, x1, y1 = box
        m = a[y0:y1, x0:x1].reshape(-1, 3).mean(axis=0)
        if np.abs(m - hexrgb(hx)).max() > tol:
            fails.append(f'region {k}: {tuple(int(v) for v in m)} vs {hx}')
    for f in fails: print('FAIL', f)
    print('visual geometry:', 'OK' if not fails else 'FAILED')
    return 1 if fails else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
