import cmath, sys
from hunstat import HunStat

f_lo, f_hi, n, rcal = 10.0, 200000.0, 25, 10000.0
hs = HunStat("COM8")
pts = hs.eis(f_lo, f_hi, n, rcal)
hs.close()
print(f"got {len(pts)} points\n")
print(f"{'f (Hz)':>12} {'Zre':>12} {'Zim':>12} {'|Z|':>12} {'phase':>8}")
for f, re, im in pts:
    z = complex(re, im)
    print(f"{f:12.3f} {re:12.2f} {im:12.2f} {abs(z):12.2f} {cmath.phase(z)*180/cmath.pi:8.2f}")
