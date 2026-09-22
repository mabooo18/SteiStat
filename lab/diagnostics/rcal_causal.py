import numpy as np
from steistat import SteiStat

R_NOM = 10560.0   # Randles we-C: Rs 560 + Rct 10k
hs = SteiStat("COM8", boot_delay=4.0)
hs.send("@ 0"); hs.drain(0.3)

print(f"{'fRcal set':>10} {'RTIA cal':>10} {'slope':>10} {'implied R':>11} {'err vs 10560':>13} {'R2':>10}")
for rc in (200, 4700, 10000, 200):
    hs.send(f"c {rc}"); hs.drain(0.3)
    hs.ser.reset_input_buffer()
    hs.send("D -200,200,5,200,1"); hs.drain(0.4)
    hs.send("M", settle=0)
    lines = hs.collect(120, quiet_stop=4.0, min_lines=5)
    rt = [l.split(",")[1] for l in lines if l.startswith("RTIACAL")]
    pts = [(float(p[1]), float(p[2])) for p in
           (l.split(",") for l in lines if l.startswith("CV,")) if len(p) >= 3]
    if len(pts) < 20:
        print(f"{rc:10d}  n={len(pts)} insufficient"); continue
    v = np.array([p[0] for p in pts]); i = np.array([p[1] for p in pts])
    m, b = np.polyfit(v, i, 1)
    r2 = 1 - np.sum((i-(m*v+b))**2)/np.sum((i-i.mean())**2)
    R = 1e3/m
    print(f"{rc:10d} {float(rt[0]) if rt else float('nan'):10.1f} {m:10.5f} "
          f"{R:11.1f} {100*(R-R_NOM)/R_NOM:+12.2f}% {r2:10.7f}")
hs.close()
