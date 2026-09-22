"""Analysis + figures for the HunStat2 validation paper.

Reads lab/data/*.csv (written by campaign.py) and emits
  lab/results.json          every number quoted in the manuscript
  journal/paper/figures/*   publication figures (PDF + PNG)
"""

from __future__ import annotations

import glob
import json
import os
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
FIGS = os.path.abspath(os.path.join(HERE, "..", "journal", "paper", "figures"))
os.makedirs(FIGS, exist_ok=True)

# --- dummy cell (we-C Randles branch), nominal component values -------------
R_S, R_CT, C_DL = 560.0, 10000.0, 33e-9
R_DC_NOM = R_S + R_CT                                  # 10560 ohm
C_EFF = R_CT ** 2 * C_DL / (R_S + R_CT) ** 2           # ramp-response capacitance
V_REF, ADC_FS = 1.82, 32768.0

# --- validated categorical palette (slots 1-3, all-pairs clean) -------------
C1, C2, C3 = "#2a78d6", "#eb6834", "#1baf7a"
INK, INK2, GRID = "#0b0b0b", "#52514e", "#d8d7d2"

plt.rcParams.update({
    "font.size": 8, "axes.labelsize": 8, "axes.titlesize": 8.5,
    "xtick.labelsize": 7.5, "ytick.labelsize": 7.5, "legend.fontsize": 7.5,
    "axes.edgecolor": INK2, "axes.linewidth": 0.6,
    "xtick.color": INK2, "ytick.color": INK2,
    "text.color": INK, "axes.labelcolor": INK,
    "grid.color": GRID, "grid.linewidth": 0.5,
    "figure.dpi": 200, "savefig.dpi": 400, "savefig.bbox": "tight",
    "legend.frameon": False, "axes.spines.top": False, "axes.spines.right": False,
})


def load(path):
    meta, rows = {}, []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                for k, v in re.findall(r"(\w+)=([^\s]+)", line):
                    meta[k] = v
                continue
            if line[0].isalpha():          # column-header row
                continue
            a, _, b = line.partition(",")
            try:
                rows.append((float(a), float(b)))
            except ValueError:
                pass
    if not rows:
        return meta, np.array([]), np.array([])
    arr = np.asarray(rows, dtype=float)
    return meta, arr[:, 0], arr[:, 1]


def fit(v, i):
    """Ohm's-law fit. Returns slope (uA/mV), intercept (uA), R, R^2, residuals."""
    m, b = np.polyfit(v, i, 1)
    resid = i - (m * v + b)
    r2 = 1.0 - resid.var() / i.var()
    return dict(slope=m, intercept=b, R=1e3 / m, r2=r2,
                resid=resid, resid_rms=float(np.sqrt((resid ** 2).mean())),
                resid_max=float(np.abs(resid).max()))


def branch_split(v, i):
    """Split a cyclic sweep into forward (increasing E) and reverse branches."""
    d = np.diff(v)
    fwd = np.concatenate([[d[0] > 0], d > 0])
    return fwd, ~fwd


def loop_width(v, i):
    """Mean forward-minus-reverse current over the overlapping potential span."""
    f, r = branch_split(v, i)
    if f.sum() < 5 or r.sum() < 5:
        return float("nan")
    lo = max(v[f].min(), v[r].min())
    hi = min(v[f].max(), v[r].max())
    if hi - lo < 10:
        return float("nan")
    grid = np.linspace(lo, hi, 60)
    fi = np.interp(grid, *zip(*sorted(zip(v[f], i[f]))))
    ri = np.interp(grid, *zip(*sorted(zip(v[r], i[r]))))
    return float(np.mean(fi - ri))


def collect(pattern):
    out = []
    for p in sorted(glob.glob(os.path.join(DATA, pattern))):
        meta, v, i = load(p)
        if v.size < 10:
            continue
        rec = fit(v, i)
        rec.update(file=os.path.basename(p), meta=meta, v=v, i=i,
                   rtia=float(meta.get("rtia_calibrated_ohm", "nan")),
                   rcal=float(meta.get("fRcal_set_ohm", "nan")),
                   scan=float(meta.get("scan_rate_mV_s", "nan")),
                   estep=float(meta.get("estep_mV", "nan")),
                   v0=float(meta.get("v_start_mV", "nan")),
                   v1=float(meta.get("v_stop_mV", "nan")),
                   loop=loop_width(v, i))
        out.append(rec)
    return out


def err(R):
    return 100.0 * (R - R_DC_NOM) / R_DC_NOM


res = {"cell": dict(Rs=R_S, Rct=R_CT, Cdl=C_DL, R_dc_nominal=R_DC_NOM,
                    C_eff_ramp=C_EFF)}

# ============================ E1 accuracy / repeatability ===================
e1 = collect("E1_replicate_*.csv")
if e1:
    R = np.array([r["R"] for r in e1])
    r2 = np.array([r["r2"] for r in e1])
    rt = np.array([r["rtia"] for r in e1])
    res["E1"] = dict(
        n=len(e1), R_mean=R.mean(), R_sd=R.std(ddof=1),
        R_rsd_pct=100 * R.std(ddof=1) / R.mean(),
        err_mean_pct=err(R.mean()), err_min_pct=err(R.min()), err_max_pct=err(R.max()),
        r2_min=r2.min(), r2_median=float(np.median(r2)),
        rtia_mean=rt.mean(), rtia_sd=rt.std(ddof=1),
        rtia_rsd_pct=100 * rt.std(ddof=1) / rt.mean(),
        resid_rms_median=float(np.median([r["resid_rms"] for r in e1])),
        lsb_uA=1e6 * V_REF / (ADC_FS * rt.mean()),
    )

# ============================ E2 RCAL mis-declaration =======================
e2 = collect("E2_rcal*.csv")
if e2:
    by = {}
    for r in e2:
        by.setdefault(int(r["rcal"]), []).append(r)
    res["E2"] = []
    for rc in sorted(by):
        g = by[rc]
        R = np.array([x["R"] for x in g])
        res["E2"].append(dict(
            rcal=rc, n=len(g), rtia_mean=float(np.mean([x["rtia"] for x in g])),
            R_mean=R.mean(), R_sd=R.std(ddof=1) if len(R) > 1 else 0.0,
            err_pct=err(R.mean()),
            r2_min=float(min(x["r2"] for x in g)),
            r2_mean=float(np.mean([x["r2"] for x in g])),
            gain_ratio=float(np.mean([x["rtia"] for x in g]))
            / float(np.mean([x["rtia"] for x in by[200]])) if 200 in by else float("nan"),
        ))

# ============================ E3/E4/E5 robustness ===========================
for key, pat, field in (("E3", "E3_scan*.csv", "scan"),
                        ("E4", "E4_window*.csv", "v1"),
                        ("E5", "E5_estep*.csv", "estep")):
    g = collect(pat)
    if g:
        res[key] = [dict(x=r[field], R=r["R"], err_pct=err(r["R"]), r2=r["r2"],
                         n=int(r["v"].size), resid_rms=r["resid_rms"],
                         loop_uA=r["loop"], rtia=r["rtia"]) for r in g]

# ---- E6 minimum viable step period (own file format) -----------------------
p6 = os.path.join(DATA, "E6_step_period.csv")
if os.path.exists(p6):
    rows = []
    for line in open(p6):
        line = line.strip()
        if not line or line.startswith("#") or line[0].isalpha():
            continue
        f = line.split(",")
        rows.append(dict(scan=float(f[0]), step_ms=float(f[1]), n=int(f[2]),
                         R=float(f[3]), r2=float(f[4])))
    ok = [r for r in rows if r["n"] >= 10]
    bad = [r for r in rows if r["n"] < 10]
    res["E6"] = dict(
        rows=rows,
        min_ok_step_ms=min(r["step_ms"] for r in ok) if ok else None,
        max_fail_step_ms=max(r["step_ms"] for r in bad) if bad else None,
        max_scan_at_5mV=max(r["scan"] for r in ok) if ok else None,
        configured_sample_delay_ms=7.0,
    )

# capacitive-resolution budget: can CV see Cdl at all?
if e1:
    lsb = res["E1"]["lsb_uA"]
    res["capacitance"] = [
        dict(scan_mV_s=s, predicted_loop_uA=2 * (s / 1000.0) * C_EFF * 1e6,
             lsb_uA=lsb, fraction_of_lsb=2 * (s / 1000.0) * C_EFF * 1e6 / lsb)
        for s in (25, 50, 100, 200, 400, 800)]

with open(os.path.join(HERE, "results.json"), "w") as fh:
    json.dump(res, fh, indent=2, default=float)

# ================================ FIGURES ===================================
def save(fig, name):
    for ext in ("pdf", "png"):
        fig.savefig(os.path.join(FIGS, f"{name}.{ext}"))
    plt.close(fig)
    print("  wrote", name)


# --- Fig 2: the linearity trap ---------------------------------------------
if e2:
    fig, ax = plt.subplots(1, 2, figsize=(7.0, 2.7))
    vv = np.linspace(-210, 195, 50)
    ax[0].axhline(0, color=GRID, lw=0.6)
    ax[0].axvline(0, color=GRID, lw=0.6)
    ax[0].plot(vv, vv / R_DC_NOM * 1e3, ls=(0, (4, 2)), lw=1.4, color=INK2,
               label="nominal (Ohm's law), 10 560 $\\Omega$", zorder=4)
    sel = [(200, C1, "declared 200 $\\Omega$  (correct)"),
           (4700, C2, "declared 4.7 k$\\Omega$"),
           (10000, C3, "declared 10 k$\\Omega$  (as shipped)")]
    for rc, col, lab in sel:
        g = [r for r in e2 if int(r["rcal"]) == rc]
        if not g:
            continue
        r = g[0]
        ax[0].plot(r["v"], r["i"], lw=1.5, color=col, label=lab, zorder=3)
    g1k = [r for r in e2 if int(r["rcal"]) == 1000]
    if g1k:
        ax[0].plot(g1k[0]["v"], g1k[0]["i"], lw=3.2, color=C2, alpha=0.30,
                   zorder=2, label="declared 1 k$\\Omega$ (lands on 4.7 k$\\Omega$)")
    ax[0].set_xlabel("applied potential (mV)")
    ax[0].set_ylabel("reported current (µA)")
    ax[0].set_title("(a) every trace is a straight line", loc="left")
    ax[0].grid(True, axis="y", alpha=0.7)
    ax[0].legend(loc="upper left", fontsize=6.4, labelspacing=0.35,
                 handlelength=1.5)
    ax[0].set_ylim(-26, 26)

    rc = np.array([d["rcal"] for d in res["E2"]])
    er = np.array([d["err_pct"] for d in res["E2"]])
    r2m = np.array([1 - d["r2_mean"] for d in res["E2"]])
    ax[1].plot(rc, np.abs(er), "o-", color=C2, lw=1.6, ms=6, zorder=3,
               label="absolute current-scale error")
    ax[1].plot(rc, r2m * 100, "s--", color=C1, lw=1.4, ms=6, zorder=3,
               label=r"unexplained variance $100(1-R^2)$")
    for x, y, dy in zip(rc, np.abs(er), (-14, 8, 8, 8)):
        ax[1].annotate(f"{y:.3g} %", (x, y), color=C2, fontsize=7.2,
                       xytext=(0, dy), textcoords="offset points", ha="center")
    ax[1].set_xscale("log"); ax[1].set_yscale("log")
    ax[1].set_xlim(130, 17000)
    ax[1].set_ylim(4e-5, 4e3)
    ax[1].set_xticks([200, 1000, 4700, 10000])
    ax[1].set_xticklabels(["200", "1 k", "4.7 k", "10 k"])
    ax[1].minorticks_off()
    ax[1].set_xlabel(r"declared $R_{CAL}$ ($\Omega$)   [fitted resistor: 200 $\Omega$]")
    ax[1].set_ylabel("magnitude (%)")
    ax[1].set_title(r"(b) error spans 3 decades; $R^2$ does not move", loc="left")
    ax[1].grid(True, which="major", alpha=0.6)
    ax[1].legend(loc="center left", bbox_to_anchor=(0.02, 0.40),
             fontsize=6.8, labelspacing=0.4)
    save(fig, "fig2_linearity_trap")

# --- Fig 3: corrected accuracy + residual structure ------------------------
if e1:
    r = e1[0]
    fig, ax = plt.subplots(2, 1, figsize=(3.5, 3.6), sharex=True,
                           gridspec_kw=dict(height_ratios=[2.1, 1], hspace=0.12))
    ax[0].plot(r["v"], r["i"], lw=1.3, color=C1, label="measured")
    ax[0].plot(r["v"], r["v"] / R_DC_NOM * 1e3, ls=(0, (4, 2)), lw=1.1,
               color=INK2, label="nominal 10 560 $\\Omega$")
    ax[0].set_ylabel("current (µA)")
    ax[0].grid(True, alpha=0.7)
    ax[0].legend(loc="upper left")
    ax[0].set_title(f"corrected: $R$ = {res['E1']['R_mean']:.0f} $\\Omega$ "
                    f"({res['E1']['err_mean_pct']:+.2f}%)", loc="left")
    ax[1].axhline(0, color=INK2, lw=0.7)
    ax[1].plot(r["v"], r["resid"] * 1e3, "o", color=C2, ms=2.4,
               mew=0, alpha=0.85)
    lsb_nA = 1e3 * res["E1"]["lsb_uA"]
    for sgn in (-1, 1):
        ax[1].axhline(sgn * lsb_nA / 2, color=INK2, lw=0.7,
                      ls=(0, (2, 2)))
    ax[1].annotate(r"$\pm\frac{1}{2}$ LSB", (0.985, lsb_nA / 2),
                   xycoords=("axes fraction", "data"), ha="right",
                   fontsize=6.4, color=INK2, xytext=(0, 3),
                   textcoords="offset points")
    ax[1].set_xlabel("applied potential (mV)")
    ax[1].set_ylabel("residual (nA)")
    ax[1].grid(True, alpha=0.7)
    save(fig, "fig3_corrected_fit")

# --- Fig 4: repeatability ---------------------------------------------------
if e1 and len(e1) > 2:
    R = np.array([x["R"] for x in e1])
    fig, ax = plt.subplots(figsize=(3.5, 2.3))
    ax.axhline(R_DC_NOM, color=INK2, ls=(0, (4, 2)), lw=1.0)
    ax.annotate("nominal 10 560 $\\Omega$", (0.02, R_DC_NOM), color=INK2,
                fontsize=7, xytext=(0, 3), textcoords="offset points",
                xycoords=("axes fraction", "data"))
    ax.axhspan(R.mean() - R.std(ddof=1), R.mean() + R.std(ddof=1),
               color=C1, alpha=0.13, lw=0)
    ax.plot(np.arange(1, len(R) + 1), R, "o-", color=C1, lw=1.3, ms=5)
    ax.set_xlabel("replicate sweep")
    ax.set_ylabel(r"recovered $R$ ($\Omega$)")
    ax.set_title(f"%RSD = {res['E1']['R_rsd_pct']:.3f}%", loc="left")
    ax.grid(True, axis="y", alpha=0.7)
    save(fig, "fig4_repeatability")

# --- Fig 5: robustness + operating limit ------------------------------------
panels = [(k, lab) for k, lab in (("E3", "scan rate (mV s$^{-1}$)"),
                                  ("E4", "potential limit $\\pm$ (mV)"),
                                  ("E5", "potential step (mV)")) if k in res]
if panels:
    ncol = len(panels) + (1 if "E6" in res else 0)
    fig, axes = plt.subplots(1, ncol, figsize=(1.85 * ncol, 2.5))
    axes = np.atleast_1d(axes)
    band = res["E1"]["err_mean_pct"] if "E1" in res else 0.43
    for a, (k, lab) in zip(axes, panels):
        d = sorted(res[k], key=lambda z: z["x"])
        x = [z["x"] for z in d]; y = [z["err_pct"] for z in d]
        a.axhline(0, color=GRID, lw=0.8)
        a.axhline(band, color=INK2, ls=(0, (4, 2)), lw=0.9)
        a.plot(x, y, "o-", color=C1, lw=1.4, ms=5, zorder=3)
        a.set_xscale("log")
        a.set_xticks(x); a.set_xticklabels([f"{int(v)}" for v in x], fontsize=6.8)
        a.minorticks_off()
        a.set_ylim(-0.05, 0.75)
        a.set_xlabel(lab, fontsize=7.4)
        a.grid(True, axis="y", alpha=0.6)
    axes[0].set_ylabel("error vs nominal (%)")
    axes[0].annotate("replicate mean", (0.97, band), xycoords=("axes fraction", "data"),
                     fontsize=6.6, color=INK2, xytext=(0, -10), ha="right",
                     textcoords="offset points")

    if "E6" in res:
        a = axes[-1]
        rows = sorted(res["E6"]["rows"], key=lambda z: z["step_ms"])
        thr = 0.5 * (res["E6"]["min_ok_step_ms"] + res["E6"]["max_fail_step_ms"])
        a.axvspan(0, thr, color=C2, alpha=0.10, lw=0)
        a.axvline(thr, color=INK, lw=1.0, ls=(0, (3, 2)), zorder=2)
        a.axvline(res["E6"]["configured_sample_delay_ms"], color=INK2, lw=0.9,
                  zorder=2)
        for r in rows:
            passed = r["n"] >= 10
            a.plot(r["step_ms"], 0.50, "o" if passed else "X",
                   color=C3 if passed else C2, ms=6.5, zorder=4,
                   mew=0 if passed else 1.2)
        a.annotate(f"fails below\n{thr:.1f} ms", (thr, 0.20), ha="right",
                   fontsize=6.5, color=INK, xytext=(-4, 0),
                   textcoords="offset points")
        a.annotate("configured\nSampleDelay\n7 ms", (7.0, 0.80), ha="left",
                   fontsize=6.3, color=INK2, xytext=(3, 0),
                   textcoords="offset points", linespacing=1.25)
        a.plot([], [], "o", color=C3, ms=6, label="sweep returns data")
        a.plot([], [], "X", color=C2, ms=6, label="no data returned")
        a.set_xlim(2, 30); a.set_ylim(0, 1.0); a.set_yticks([])
        a.set_xlabel("step period (ms)", fontsize=7.4)
        a.legend(loc="lower right", fontsize=6.2, handlelength=1.0,
                 handletextpad=0.3, borderpad=0.25)
        a.spines["left"].set_visible(False)
    save(fig, "fig5_robustness")

print("\nresults.json written. Key numbers:")
print(json.dumps({k: v for k, v in res.items() if k in ("E1", "E2")},
                 indent=2, default=float)[:1400])
