"""Finish the E5 potential-step series left over from the interrupted campaign."""
import os
import numpy as np
from hunstat import HunStat

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
R_S, R_CT, C_DL = 560.0, 10000.0, 33e-9
R_DC_NOM = R_S + R_CT
RCAL = 200.0


def run_cv(hs, tag, v0, v1, estep, scan, cycles=1, rcal=RCAL):
    hs.send(f"c {rcal}"); hs.drain(0.3)
    hs.ser.reset_input_buffer()
    hs.send(f"D {v0},{v1},{estep},{scan},{cycles}"); hs.drain(0.4)
    hs.send("M", settle=0)
    lines = hs.collect(cycles * 2 * abs(v1 - v0) / scan + 60.0,
                       quiet_stop=5.0, min_lines=5)
    rtia = next((float(l.split(",")[1]) for l in lines if l.startswith("RTIACAL")), None)
    pts = []
    for l in lines:
        if l.startswith("CV,"):
            p = l.split(",")
            if len(p) >= 3:
                try: pts.append((float(p[1]), float(p[2])))
                except ValueError: pass
    with open(os.path.join(DATA, f"{tag}.csv"), "w", newline="") as fh:
        fh.write(f"# HunStat2 CV | {tag}\n")
        fh.write(f"# v_start_mV={v0} v_stop_mV={v1} estep_mV={estep} "
                 f"scan_rate_mV_s={scan} cycles={cycles}\n")
        fh.write(f"# fRcal_set_ohm={rcal} rtia_calibrated_ohm={rtia}\n")
        fh.write(f"# cell=we-C Randles Rs={R_S} Rct={R_CT} Cdl={C_DL}\n")
        fh.write("potential_mV,current_uA\n")
        for v, i in pts:
            fh.write(f"{v:.4f},{i:.6f}\n")
    if len(pts) >= 10:
        v = np.array([p[0] for p in pts]); i = np.array([p[1] for p in pts])
        m, b = np.polyfit(v, i, 1)
        R = 1e3 / m
        r2 = 1 - ((i - (m * v + b)).var() / i.var())
        print(f"  {tag}: n={len(pts):3d} RTIA={rtia} R={R:.1f} "
              f"({100*(R-R_DC_NOM)/R_DC_NOM:+.3f}%) R2={r2:.7f}")
    else:
        print(f"  {tag}: only {len(pts)} points")


hs = HunStat("COM8", boot_delay=4.0)
hs.send("@ 0"); hs.drain(0.3)
for st in (5, 10, 20):
    run_cv(hs, f"E5_estep{st}", -200, 200, st, 200)
hs.close()
