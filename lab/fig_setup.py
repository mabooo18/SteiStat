"""Figure 1: measurement chain + reference cell schematic."""

import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch

HERE = os.path.dirname(os.path.abspath(__file__))
FIGS = os.path.abspath(os.path.join(HERE, "..", "journal", "paper", "figures"))
os.makedirs(FIGS, exist_ok=True)

C1, C2, C3 = "#2a78d6", "#eb6834", "#1baf7a"
INK, INK2, GRID = "#0b0b0b", "#52514e", "#d8d7d2"

plt.rcParams.update({"font.size": 7.5, "text.color": INK})

fig = plt.figure(figsize=(7.0, 3.15))
gs = fig.add_gridspec(1, 2, width_ratios=[1.62, 1.0], wspace=0.13)
axA, axB = fig.add_subplot(gs[0]), fig.add_subplot(gs[1])
for a in (axA, axB):
    a.set_xlim(0, 10); a.set_ylim(0, 10); a.axis("off")


def box(ax, x, y, w, h, label, fc="#ffffff", ec=INK2, fs=7.5, bold=False, lw=0.9):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.10,rounding_size=0.18",
                                fc=fc, ec=ec, lw=lw, zorder=2))
    ax.text(x + w / 2, y + h / 2, label, ha="center", va="center", fontsize=fs,
            zorder=3, fontweight="bold" if bold else "normal", linespacing=1.35)


def arrow(ax, p, q, color=INK2, lw=1.0, ls="-"):
    ax.add_patch(FancyArrowPatch(p, q, arrowstyle="-|>", mutation_scale=8,
                                 color=color, lw=lw, linestyle=ls,
                                 shrinkA=1, shrinkB=1, zorder=1))


# ----------------------------- (a) signal chain ----------------------------
axA.text(0, 9.72, "(a)  AD5941 low-power measurement chain", fontsize=8.5,
         fontweight="bold", ha="left")

box(axA, 0.25, 7.05, 2.35, 1.15, "LPDAC\n$V_{bias}$ / $V_{zero}$")
box(axA, 3.55, 7.05, 1.85, 1.15, "LPPA")
box(axA, 6.75, 7.05, 2.9, 1.15, "cell\n(we-C Randles)", fc="#f2f7fd", ec=C1, lw=1.2)
box(axA, 6.75, 4.35, 2.9, 1.10, "LPTIA\n(gain $\\hat{R}_{TIA}$)")
box(axA, 3.30, 4.35, 2.35, 1.10, "ADC\n16-bit $\\Sigma\\Delta$")
box(axA, 0.25, 4.35, 2.25, 1.10, "$I_{rep}$", fc="#fdf3ee", ec=C2, lw=1.2, bold=True)

arrow(axA, (2.60, 7.63), (3.55, 7.63))
arrow(axA, (5.40, 7.63), (6.75, 7.63))
axA.text(6.05, 7.80, "CE", fontsize=6.8, color=INK2, ha="center")
arrow(axA, (8.20, 7.05), (8.20, 5.45), color=C1, lw=1.2)
axA.text(8.02, 6.25, "SE", fontsize=6.8, color=C1, ha="right", va="center")
arrow(axA, (6.75, 4.90), (5.65, 4.90))
arrow(axA, (3.30, 4.90), (2.50, 4.90))

# RE feedback
axA.plot([7.35, 7.35, 4.48, 4.48], [7.05, 6.42, 6.42, 7.05], color=INK2, lw=0.8,
         zorder=1)
axA.text(5.90, 6.22, "RE feedback", fontsize=6.6, color=INK2, ha="center")

# calibration branch
box(axA, 0.25, 1.68, 3.05, 1.15, "$R_{CAL}$ fitted\n200 $\\Omega$ (RCAL1)",
    fc="#f0faf6", ec=C3, lw=1.2)
box(axA, 4.55, 1.68, 5.10, 1.15,
    "$R_{CAL}^{\\,decl}$  firmware constant\n(was 10 k$\\Omega$)",
    fc="#fdf3ee", ec=C2, lw=1.2)
arrow(axA, (1.78, 2.95), (3.15, 3.38), color=C3, lw=1.1)
arrow(axA, (7.10, 2.95), (5.95, 3.38), color=C2, lw=1.1, ls=(0, (3, 2)))
axA.text(4.55, 3.74,
         r"$\hat{R}_{TIA}=R_{CAL}^{\,decl}\cdot"
         r"\frac{\mathrm{DFT}_{TIA}}{\mathrm{DFT}_{CAL}}$",
         fontsize=8.8, ha="center", va="center")
arrow(axA, (6.15, 3.92), (7.60, 4.35), color=C2, lw=1.1, ls=(0, (3, 2)))
axA.add_patch(FancyBboxPatch((0.30, 0.10), 9.30, 1.18,
                             boxstyle="round,pad=0.06,rounding_size=0.15",
                             fc="#fbfbfa", ec=GRID, lw=0.8, zorder=0))
axA.text(4.95, 0.69,
         "a mismatch between these two rescales every reported\n"
         "current — and leaves $R^2$ unchanged",
         fontsize=7.2, ha="center", va="center", color=INK2, style="italic",
         linespacing=1.4)

# ----------------------------- (b) Randles cell ----------------------------
axB.text(0, 9.72, "(b)  we-C reference branch", fontsize=8.5,
         fontweight="bold", ha="left")

y = 6.75
# Rs
axB.plot([0.7, 2.0], [y, y], color=INK, lw=1.2)
axB.add_patch(plt.Rectangle((2.0, y - 0.42), 1.9, 0.84, fc="white", ec=INK, lw=1.2))
axB.text(2.95, y, "$R_s$", ha="center", va="center", fontsize=8)
axB.text(2.95, y - 0.95, "560 $\\Omega$", ha="center", fontsize=7.2, color=INK2)
axB.plot([3.9, 5.1], [y, y], color=INK, lw=1.2)

# parallel Rct || Cdl
xL, xR = 5.1, 8.5
axB.plot([xL, xL], [y - 1.75, y + 1.75], color=INK, lw=1.2)
axB.plot([xR, xR], [y - 1.75, y + 1.75], color=INK, lw=1.2)
# Rct branch
axB.plot([xL, 5.9], [y - 1.75, y - 1.75], color=INK, lw=1.2)
axB.add_patch(plt.Rectangle((5.9, y - 2.17), 1.8, 0.84, fc="white", ec=INK, lw=1.2))
axB.text(6.8, y - 1.75, "$R_{ct}$", ha="center", va="center", fontsize=8)
axB.text(6.8, y - 2.72, "10 k$\\Omega$  0.1 %", ha="center", fontsize=7.2, color=INK2)
axB.plot([7.7, xR], [y - 1.75, y - 1.75], color=INK, lw=1.2)
# Cdl branch
axB.plot([xL, 6.55], [y + 1.75, y + 1.75], color=INK, lw=1.2)
axB.plot([6.55, 6.55], [y + 1.25, y + 2.25], color=INK, lw=1.4)
axB.plot([7.05, 7.05], [y + 1.25, y + 2.25], color=INK, lw=1.4)
axB.plot([7.05, xR], [y + 1.75, y + 1.75], color=INK, lw=1.2)
axB.text(6.8, y + 2.55, "$C_{dl}$  33 nF", ha="center", fontsize=7.2, color=INK2)

axB.plot([xR, 9.3], [y, y], color=INK, lw=1.2)
axB.plot([xR, xR], [y, y], color=INK, lw=1.2)

axB.plot(0.7, y, "o", ms=5, mfc="white", mec=INK, mew=1.2)
axB.plot(9.3, y, "o", ms=5, mfc="white", mec=INK, mew=1.2)
axB.text(0.55, y + 0.55, "WE", fontsize=7.5, ha="left", color=INK)
axB.text(9.9, y + 0.95, "CE/RE", fontsize=7.5, ha="right", color=INK)

axB.add_patch(FancyBboxPatch((0.4, 0.95), 9.2, 2.05,
                             boxstyle="round,pad=0.10,rounding_size=0.18",
                             fc="#f2f7fd", ec=C1, lw=1.0, zorder=0))
axB.text(5.0, 2.42, "$R_{DC}=R_s+R_{ct}=10\\,560\\ \\Omega$",
         ha="center", fontsize=8.2)
axB.text(5.0, 1.55,
         "$C_{eff}=R_{ct}^{2}C_{dl}/(R_s{+}R_{ct})^{2}=29.6$ nF",
         ha="center", fontsize=8.2)

for ext in ("pdf", "png"):
    fig.savefig(os.path.join(FIGS, f"fig1_setup.{ext}"), dpi=400, bbox_inches="tight")
print("wrote fig1_setup")
