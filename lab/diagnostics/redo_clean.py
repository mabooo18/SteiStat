"""Re-acquire the robustness series on a freshly reset board.

Only settings whose step period clears the firmware's settling requirement are
attempted; the step-period limit itself is characterised separately.
"""
import os, time
import numpy as np
from steistat import SteiStat

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
R_S, R_CT, C_DL = 560.0, 10000.0, 33e-9
R_DC_NOM, RCAL = R_S + R_CT, 200.0


def run_cv(hs, tag, v0, v1, estep, scan, cycles=1, rcal=RCAL, write=True):
    hs.send(f"c {rcal}"); hs.drain(0.4)
    hs.ser.reset_input_buffer()
    hs.send(f"D {v0},{v1},{estep},{scan},{cycles}"); hs.drain(0.5)
    hs.send("M", settle=0)
    nsteps = 2 * cycles * abs(v1 - v0) / estep
    budget = 8.0 + nsteps * max(estep / scan, 0.025) * 2.5 + 40.0
    lines = hs.collect(budget, quiet_stop=8.0, min_lines=5)
    hs.drain(1.0)                      # swallow any late stragglers
    rtia = next((float(l.split(",")[1]) for l in lines if l.startswith("RTIACAL")), None)
    errs = [l for l in lines if l.startswith("ERR,")]
    pts = []
    for l in lines:
        if l.startswith("CV,"):
            p = l.split(",")
            if len(p) >= 3:
                try: pts.append((float(p[1]), float(p[2])))
                except ValueError: pass
    R = r2 = float("nan")
    if len(pts) >= 10:
        v = np.array([p[0] for p in pts]); i = np.array([p[1] for p in pts])
        m, b = np.polyfit(v, i, 1); R = 1e3 / m
        r2 = 1 - ((i - (m * v + b)).var() / i.var())
    if write and len(pts) >= 10:
        with open(os.path.join(DATA, f"{tag}.csv"), "w", newline="") as fh:
            fh.write(f"# SteiStat CV | {tag}\n")
            fh.write(f"# v_start_mV={v0} v_stop_mV={v1} estep_mV={estep} "
                     f"scan_rate_mV_s={scan} cycles={cycles}\n")
            fh.write(f"# fRcal_set_ohm={rcal} rtia_calibrated_ohm={rtia}\n")
            fh.write(f"# cell=we-C Randles Rs={R_S} Rct={R_CT} Cdl={C_DL}\n")
            fh.write("potential_mV,current_uA\n")
            for v_, i_ in pts:
                fh.write(f"{v_:.4f},{i_:.6f}\n")
    tp = 1000.0 * estep / scan
    print(f"  {tag:18s} step={tp:6.1f} ms n={len(pts):4d} R={R:9.1f} "
          f"({100*(R-R_DC_NOM)/R_DC_NOM:+7.3f}%) R2={r2:.7f} {errs if errs else ''}")
    return len(pts)


hs = SteiStat("COM8", boot_delay=5.0)
hs.send("@ 0"); hs.drain(0.5)

print("verify baseline reproduces:")
run_cv(hs, "VERIFY_baseline", -200, 200, 5, 200, write=False)

print("\nE4 potential-window series (step period held at 25 ms):")
for w in (100, 200, 400, 600):
    run_cv(hs, f"E4_window{w}", -w, w, 5, 200)

print("\nE3 scan-rate series (upper points):")
for sr, st in ((400, 10), (800, 20)):
    run_cv(hs, f"E3_scan{sr}", -200, 200, st, sr)

hs.close()
