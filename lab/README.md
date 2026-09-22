# lab/ — acquisition and analysis for the CV validation paper

Hardware: Seeed XIAO RP2040 + AD5941 (`STEI_Bstat_v1.2`) on COM8, external dummy
cell on the **we-C Randles branch** (Rs 560 Ω, Rct 10 kΩ 0.1 %, Cdl 33 nF),
**RCAL1 = 200 Ω** fitted.

| File | Purpose |
|------|---------|
| `steistat.py`   | serial driver for the firmware's ASCII protocol; every wait has a deadline |
| `campaign.py`  | the 37-sweep measurement campaign → `data/` |
| `analyse.py`   | fits, statistics and figures → `results.json`, `../journal/paper/figures/` |
| `fig_setup.py` | Figure 1 (signal chain + reference cell schematic) |
| `check_tex.py` | static checks on the manuscript sources (no LaTeX needed) |
| `data/`        | one CSV per sweep; the header records the settings and the calibrated RTIA |
| `diagnostics/` | the probe scripts used to find each defect; kept as evidence |

## Reproduce

```bash
python campaign.py     # ~35 min, 37 sweeps
python analyse.py      # results.json + figs 2-5
python fig_setup.py    # fig 1
```

**Reset the board before a campaign.** A `CV_TIMEOUT` leaves the AFE degraded
until power-cycled (see `../journal/05_calibration_defect_and_cv_validation.md`).

## Build/flash the firmware

The build path **must not contain spaces** — the linker cannot quote the linker
script path.

```bash
CLI="$LOCALAPPDATA/Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
SK="../Software/update 6 (15 September 26)/AD5941_25"
"$CLI" compile --fqbn rp2040:rp2040:seeed_xiao_rp2040 --build-path C:\hsbuild "$SK"
"$CLI" upload  --fqbn rp2040:rp2040:seeed_xiao_rp2040 --port COM8 --input-dir C:\hsbuild "$SK"
```
