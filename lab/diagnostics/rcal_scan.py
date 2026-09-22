import numpy as np
from steistat import SteiStat

hs = SteiStat("COM8")
hs.send("@ 0"); hs.drain(0.3)
print(f"{'fRcal':>8} {'slope uA/mV':>13} {'implied R':>12} {'R2':>10}")
for rc in (10000, 4700, 1000, 200):
    hs.send(f"c {rc}"); hs.drain(0.3)
    pts = hs.cv(-200, 200, 10, 400, cycles=1)
    if len(pts) < 20:
        print(f"{rc:8d}   (n={len(pts)}) insufficient"); continue
    v = np.array([p[0] for p in pts]); i = np.array([p[1] for p in pts])
    m, b = np.polyfit(v, i, 1)
    r2 = 1 - np.sum((i-(m*v+b))**2)/np.sum((i-i.mean())**2)
    print(f"{rc:8d} {m:13.6g} {1e3/m:12.1f} {r2:10.6f}")
hs.send("c 10000"); hs.drain(0.3)
hs.close()
