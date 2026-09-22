import numpy as np
from steistat import SteiStat

hs = SteiStat("COM8")
hs.send("@ 0"); hs.drain(0.3)
pts = hs.cv(-200, 200, 5, 200, cycles=1)
hs.close()

print(f"n = {len(pts)}")
if pts:
    v = np.array([p[0] for p in pts]); i = np.array([p[1] for p in pts])
    print(f"V range: {v.min():.1f} .. {v.max():.1f} mV")
    print(f"I range: {i.min():.4g} .. {i.max():.4g} (firmware units, uA)")
    m, b = np.polyfit(v, i, 1)
    r2 = 1 - np.sum((i-(m*v+b))**2)/np.sum((i-i.mean())**2)
    print(f"fit: I = {m:.6g}*V + {b:.6g}   R2 = {r2:.7f}")
    print(f"implied R = 1/slope = {1.0/m*1e3:.1f} ohm   (V in mV, I in uA -> kohm*1e3)")
    print("\nfirst 6:", pts[:6])
    print("last 6:", pts[-6:])
