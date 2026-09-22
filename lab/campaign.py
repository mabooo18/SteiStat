"""Measurement campaign for the SteiStat validation paper.

Everything is run against the we-C Randles branch of the external dummy cell
(Rs = 560 ohm, Rct = 10 kohm 0.1%, Cdl = 33 nF), with RCAL1 = 200 ohm fitted.

Writes one CSV per sweep into lab/data/ plus a run manifest, so every number
in the manuscript is traceable to a raw file.
"""

from __future__ import annotations

import json
import os
import time

import numpy as np

from steistat import SteiStat

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data")
os.makedirs(DATA, exist_ok=True)

R_S, R_CT, C_DL = 560.0, 10000.0, 33e-9
R_DC_NOM = R_S + R_CT          # 10560 ohm
RCAL_FITTED = 200.0

manifest: list[dict] = []


def run_cv(hs, tag, v0, v1, estep, scan, cycles=1, rcal=RCAL_FITTED):
    hs.send(f"c {rcal}")
    hs.drain(0.3)
    hs.ser.reset_input_buffer()
    hs.send(f"D {v0},{v1},{estep},{scan},{cycles}")
    hs.drain(0.4)
    hs.send("M", settle=0)
    span = abs(v1 - v0)
    budget = cycles * 2 * span / scan + 60.0
    lines = hs.collect(budget, quiet_stop=5.0, min_lines=5)

    rtia = None
    for l in lines:
        if l.startswith("RTIACAL"):
            rtia = float(l.split(",")[1])
    pts = []
    for l in lines:
        if l.startswith("CV,"):
            p = l.split(",")
            if len(p) >= 3:
                try:
                    pts.append((float(p[1]), float(p[2])))
                except ValueError:
                    pass

    path = os.path.join(DATA, f"{tag}.csv")
    with open(path, "w", newline="") as fh:
        fh.write(f"# SteiStat CV | {tag}\n")
        fh.write(f"# v_start_mV={v0} v_stop_mV={v1} estep_mV={estep} "
                 f"scan_rate_mV_s={scan} cycles={cycles}\n")
        fh.write(f"# fRcal_set_ohm={rcal} rtia_calibrated_ohm={rtia}\n")
        fh.write(f"# cell=we-C Randles Rs={R_S} Rct={R_CT} Cdl={C_DL}\n")
        fh.write("potential_mV,current_uA\n")
        for v, i in pts:
            fh.write(f"{v:.4f},{i:.6f}\n")

    rec = dict(tag=tag, n=len(pts), rtia=rtia, v0=v0, v1=v1, estep=estep,
               scan=scan, cycles=cycles, rcal=rcal, file=os.path.basename(path))
    if len(pts) >= 10:
        v = np.array([p[0] for p in pts])
        i = np.array([p[1] for p in pts])
        m, b = np.polyfit(v, i, 1)
        resid = i - (m * v + b)
        rec.update(slope_uA_per_mV=m, intercept_uA=b,
                   R_ohm=1e3 / m, r2=1 - resid.var() / i.var(),
                   resid_rms_uA=float(np.sqrt((resid ** 2).mean())),
                   resid_max_uA=float(np.abs(resid).max()))
    manifest.append(rec)
    return rec


def banner(t):
    print(f"\n{'='*72}\n{t}\n{'='*72}")


hs = SteiStat("COM8", boot_delay=4.0)
hs.send("@ 0")
hs.drain(0.3)
t_start = time.time()

# -- E1: replicate sweeps at the nominal condition (accuracy + repeatability) --
banner("E1  replicates @ -200..+200 mV, 5 mV step, 200 mV/s")
for k in range(10):
    r = run_cv(hs, f"E1_replicate_{k:02d}", -200, 200, 5, 200)
    print(f"  rep {k:02d}: n={r['n']:3d} RTIA={r['rtia']:.1f} "
          f"R={r.get('R_ohm', float('nan')):.1f} ohm "
          f"({100*(r.get('R_ohm', np.nan)-R_DC_NOM)/R_DC_NOM:+.3f}%) "
          f"R2={r.get('r2', float('nan')):.7f}")

# -- E2: RCAL mis-declaration sweep (the defect, reproduced on demand) --------
banner("E2  fRcal mis-declaration (root-cause demonstration)")
for rc in (200, 1000, 4700, 10000):
    for k in range(3):
        r = run_cv(hs, f"E2_rcal{rc}_{k}", -200, 200, 5, 200, rcal=rc)
        print(f"  fRcal={rc:6d} rep{k}: RTIA={r['rtia']:8.1f} "
              f"R={r.get('R_ohm', float('nan')):9.1f} "
              f"({100*(r.get('R_ohm', np.nan)-R_DC_NOM)/R_DC_NOM:+8.2f}%) "
              f"R2={r.get('r2', float('nan')):.7f}")

# -- E3: scan-rate series ----------------------------------------------------
banner("E3  scan-rate series")
for sr in (25, 50, 100, 200, 400, 800):
    r = run_cv(hs, f"E3_scan{sr}", -200, 200, 5, sr)
    print(f"  {sr:4d} mV/s: n={r['n']:3d} R={r.get('R_ohm', float('nan')):.1f} "
          f"({100*(r.get('R_ohm', np.nan)-R_DC_NOM)/R_DC_NOM:+.3f}%) "
          f"R2={r.get('r2', float('nan')):.7f} resid_rms={r.get('resid_rms_uA', float('nan')):.4f} uA")

# -- E4: potential-window series ---------------------------------------------
banner("E4  potential-window series")
for w in (50, 100, 200, 400, 600):
    r = run_cv(hs, f"E4_window{w}", -w, w, max(2, w // 40), 200)
    print(f"  +/-{w:4d} mV: n={r['n']:3d} R={r.get('R_ohm', float('nan')):.1f} "
          f"({100*(r.get('R_ohm', np.nan)-R_DC_NOM)/R_DC_NOM:+.3f}%) "
          f"R2={r.get('r2', float('nan')):.7f}")

# -- E5: step-size series ----------------------------------------------------
banner("E5  potential-step series")
for st in (2, 5, 10, 20):
    r = run_cv(hs, f"E5_estep{st}", -200, 200, st, 200)
    print(f"  {st:3d} mV step: n={r['n']:3d} R={r.get('R_ohm', float('nan')):.1f} "
          f"({100*(r.get('R_ohm', np.nan)-R_DC_NOM)/R_DC_NOM:+.3f}%) "
          f"R2={r.get('r2', float('nan')):.7f}")

hs.close()

with open(os.path.join(DATA, "manifest.json"), "w") as fh:
    json.dump(dict(started=t_start, finished=time.time(),
                   cell=dict(branch="we-C", Rs=R_S, Rct=R_CT, Cdl=C_DL,
                             R_dc_nominal=R_DC_NOM),
                   rcal_fitted=RCAL_FITTED, runs=manifest), fh, indent=2)
print(f"\nwrote {len(manifest)} runs to {DATA} in {time.time()-t_start:.0f}s")
