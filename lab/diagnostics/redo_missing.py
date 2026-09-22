"""Re-acquire the sweeps lost when the board wedged, and log any recurrence."""
import os, time
import numpy as np
from steistat import SteiStat

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
R_S, R_CT, C_DL = 560.0, 10000.0, 33e-9
R_DC_NOM, RCAL = R_S + R_CT, 200.0


def run_cv(hs, tag, v0, v1, estep, scan, cycles=1, rcal=RCAL):
    hs.send(f"c {rcal}"); hs.drain(0.3)
    hs.ser.reset_input_buffer()
    hs.send(f"D {v0},{v1},{estep},{scan},{cycles}"); hs.drain(0.4)
    hs.send("M", settle=0)
    lines = hs.collect(cycles * 2 * abs(v1 - v0) / scan + 55.0,
                       quiet_stop=5.0, min_lines=5)
    rtia = next((float(l.split(",")[1]) for l in lines if l.startswith("RTIACAL")), None)
    err = [l for l in lines if l.startswith("ERR,")]
    pts = []
    for l in lines:
        if l.startswith("CV,"):
            p = l.split(",")
            if len(p) >= 3:
                try: pts.append((float(p[1]), float(p[2])))
                except ValueError: pass
    with open(os.path.join(DATA, f"{tag}.csv"), "w", newline="") as fh:
        fh.write(f"# SteiStat CV | {tag}\n")
        fh.write(f"# v_start_mV={v0} v_stop_mV={v1} estep_mV={estep} "
                 f"scan_rate_mV_s={scan} cycles={cycles}\n")
        fh.write(f"# fRcal_set_ohm={rcal} rtia_calibrated_ohm={rtia}\n")
        fh.write(f"# cell=we-C Randles Rs={R_S} Rct={R_CT} Cdl={C_DL}\n")
        fh.write("potential_mV,current_uA\n")
        for v, i in pts:
            fh.write(f"{v:.4f},{i:.6f}\n")
    if len(pts) >= 10:
        v = np.array([p[0] for p in pts]); i = np.array([p[1] for p in pts])
        m, b = np.polyfit(v, i, 1); R = 1e3 / m
        r2 = 1 - ((i - (m * v + b)).var() / i.var())
        print(f"  {tag:18s} n={len(pts):3d} RTIA={rtia} R={R:9.1f} "
              f"({100*(R-R_DC_NOM)/R_DC_NOM:+.3f}%) R2={r2:.7f}")
    else:
        print(f"  {tag:18s} n={len(pts)}  ERR={err}")
    return len(pts)


jobs = ([("E3_scan400", -200, 200, 5, 400), ("E3_scan800", -200, 200, 5, 800)]
        + [(f"E4_window{w}", -w, w, max(2, w // 40), 200)
           for w in (50, 100, 200, 400, 600)]
        + [("E5_estep2", -200, 200, 2, 200)])

hs = SteiStat("COM8", boot_delay=4.0)
hs.send("@ 0"); hs.drain(0.3)
t0 = time.time()
for tag, v0, v1, st, sr in jobs:
    run_cv(hs, tag, v0, v1, st, sr)
hs.close()
print(f"done in {time.time()-t0:.0f}s")
