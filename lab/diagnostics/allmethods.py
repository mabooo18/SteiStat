import numpy as np, statistics
from hunstat import HunStat

hs = HunStat("COM8", boot_delay=4.0)
hs.send("@ 0"); hs.drain(0.3)
print("rcal now:", [l for l in hs.params() if "rcal" in l][:1])

print("\n--- OCP ---"); print(" ", hs.ocp())

print("\n--- CA (tia_rf sweep, 200 mV step) ---")
for r in (1, 2, 3, 4):
    pts = hs.ca(200.0, 0.4, 100.0, tia_rf=r)
    if not pts: print(f"  r{r}: no data"); continue
    tail = [i for t, i in pts if t > 0.2]
    print(f"  r{r}: n={len(pts):3d} I_ss={statistics.mean(tail):+.4e} A  first={pts[0][1]:+.3e}")

print("\n--- SWV ---")
p = hs.swv(-200, 200, 10, 25, 50, tia_rf=3)
print(f"  n={len(p)}", (f"I {min(x[1] for x in p):.3e}..{max(x[1] for x in p):.3e}") if p else "")

print("\n--- DPV ---")
p = hs.dpv(-200, 200, 10, 50, tia_rf=3)
print(f"  n={len(p)}", (f"I {min(x[1] for x in p):.3e}..{max(x[1] for x in p):.3e}") if p else "")
hs.close()
