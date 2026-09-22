from hunstat import HunStat
import statistics

hs = HunStat("COM8")
print("OCP (mV):", hs.ocp())

for v in (0.0, 50.0, 100.0, 200.0, -100.0):
    pts = hs.ca(v, 0.5, 200.0, tia_rf=3)
    if not pts:
        print(f"CA {v:+7.1f} mV : NO DATA")
        continue
    tail = [i for t, i in pts if t > 0.25]
    print(f"CA {v:+7.1f} mV : n={len(pts):3d}  I_ss={statistics.mean(tail):+.4e} A  "
          f"sd={statistics.pstdev(tail):.2e}  I_first={pts[0][1]:+.3e}")
hs.close()
