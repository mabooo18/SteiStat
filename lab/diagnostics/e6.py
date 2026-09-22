"""E6: locate the minimum viable potential-step period (Estep / scan rate)."""
import os
import numpy as np
from hunstat import HunStat

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
R_DC_NOM, RCAL = 10560.0, 200.0
ESTEP = 5.0


def probe(hs, scan):
    hs.send(f"c {RCAL}"); hs.drain(0.4)
    hs.ser.reset_input_buffer()
    hs.send(f"D -200,200,{ESTEP},{scan},1"); hs.drain(0.5)
    hs.send("M", settle=0)
    lines = hs.collect(8.0 + 160 * max(ESTEP / scan, 0.025) * 2.5 + 40.0,
                       quiet_stop=8.0, min_lines=5)
    hs.drain(1.0)
    pts = []
    for l in lines:
        if l.startswith("CV,"):
            p = l.split(",")
            if len(p) >= 3:
                try: pts.append((float(p[1]), float(p[2])))
                except ValueError: pass
    errs = [l for l in lines if l.startswith("ERR,")]
    R = r2 = float("nan")
    if len(pts) >= 10:
        v = np.array([p[0] for p in pts]); i = np.array([p[1] for p in pts])
        m, b = np.polyfit(v, i, 1); R = 1e3 / m
        r2 = 1 - ((i - (m * v + b)).var() / i.var())
    return len(pts), R, r2, errs


hs = HunStat("COM8", boot_delay=5.0)
hs.send("@ 0"); hs.drain(0.5)
rows = []
print(f"{'scan mV/s':>10} {'step ms':>8} {'n':>5} {'R ohm':>10} {'err %':>8} {'R2':>11}")
for scan in (200, 250, 286, 333, 400, 500, 625, 1000):
    n, R, r2, errs = probe(hs, scan)
    tp = 1000.0 * ESTEP / scan
    rows.append((scan, tp, n, R, r2))
    print(f"{scan:10d} {tp:8.1f} {n:5d} {R:10.1f} "
          f"{100*(R-R_DC_NOM)/R_DC_NOM:+8.3f} {r2:11.7f}  {errs if errs else ''}")
hs.close()

with open(os.path.join(DATA, "E6_step_period.csv"), "w") as fh:
    fh.write("# HunStat2 CV | E6 minimum viable step period, Estep=5 mV, -200..+200 mV\n")
    fh.write(f"# fRcal_set_ohm={RCAL}\n")
    fh.write("scan_rate_mV_s,step_period_ms,n_points,R_ohm,r2\n")
    for scan, tp, n, R, r2 in rows:
        fh.write(f"{scan},{tp:.3f},{n},{R:.3f},{r2:.8f}\n")
print("wrote E6_step_period.csv")
