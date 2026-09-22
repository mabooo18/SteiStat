# Calibration Defect, Firmware Fixes and CV Validation — 2026-09-15/16

Hardware live on COM8 (Seeed XIAO RP2040 + AD5941, `STEI_Bstat_v1.2`), external
dummy cell on the **we-C Randles branch** (Rs 560 Ω, Rct 10 kΩ 0.1 %, Cdl 33 nF,
R_DC = 10 560 Ω). **RCAL1 = 200 Ω** fitted on the board (confirmed by the user).

All firmware changes are in `Software/update 6 (15 September 26)/AD5941_25/`.
Builds with `arduino-cli` 1.2.0, core `rp2040:rp2040` 5.6.0, FQBN
`rp2040:rp2040:seeed_xiao_rp2040`. **Build path must not contain spaces** — the
linker cannot quote the linker-script path, so use e.g. `--build-path C:\hsbuild`.

## Headline finding

The instrument recovered a resistance **424 % in error** from CV — reported
currents low by a factor of **5.22** — while fitting Ohm's law at
**R² = 0.99999647**. (Errors are quoted in recovered resistance throughout,
since that is what is compared against the traceable reference; the equivalent
current error is −80.9 %.) The cause was the firmware constant `fRcal`, which declared
the on-board calibration resistor as 10 kΩ when the fitted part is 200 Ω. The
AD5941 measures its own LPTIA gain against that declaration
(`AD5940_LPRtiaCal`, `ad5940.cpp`), so the mismatch scaled every reported
current.

Deliberately sweeping the declared value (3 replicates each) reproduced the
defect on demand:

| declared RCAL | calibrated RTIA | recovered R | error | mean R² |
|---|---|---|---|---|
| **200 Ω** (correct) | 1093.0 Ω | 10 605.6 ± 0.4 Ω | **+0.43 %** | 0.99999861 |
| 1 kΩ | 2673.9 Ω | 25 944.4 ± 4.8 Ω | +145.7 % | 0.99999831 |
| 4.7 kΩ | 2680.5 Ω | 26 010.3 ± 4.6 Ω | +146.3 % | 0.99999831 |
| 10 kΩ (as shipped) | 5703.1 Ω | 55 345.5 ± 11.9 Ω | **+424.1 %** | 0.99999647 |

Absolute error spans ×982; R² moves by 2×10⁻⁶. **A linearity check cannot detect
a gain error** — R² is invariant under affine rescaling of the measured variable.

Note that 1 kΩ and 4.7 kΩ give nearly the same calibrated RTIA despite a 4.7×
difference in the declaration. The calibration excitation is sized as
`2000·0.8·fRcal/RTIA` mV_pp, so over-declaring RCAL over-drives and saturates the
TIA. **The error is therefore not proportional to the mis-declaration and cannot
be corrected after the fact** — it has to be prevented.

## Defects found and fixed

| ID | File | Defect | Effect |
|----|------|--------|--------|
| D1 | `AD5941_25.ino` | `fRcal = 10000.0` vs fitted 200 Ω | +424 % current-scale error |
| D2 | `communication.cpp` | `c` command wrote `C_DataStorage::fRcal`, which **no measurement path reads**; CV/EIS read a separate global written only by unreachable legacy code (`command_processing.cpp:304`) | RCAL not settable at runtime; `?` showed a value that was not in use |
| D3 | `rampTest.cpp:1119` | RTIA calibration gated on `RAMPInited == bFALSE \|\| bParaChanged == bTRUE`; `RAMPInited` latches TRUE after the first sweep | calibration frozen at power-up |
| D4 | `utilities.cpp:208` | `Delay(bool(*)())` — unbounded busy-wait, polled at 100 ms | EIS hung indefinitely while USB still enumerated |
| D5 | `c_ca/c_swv/c_dpv.cpp` | `adc_base.ADCPga = 1` selects `ADCPGA_1P5` (gain **1.5**) while `RawToCurrent()` divides by 1.0 | silent 1.5× error |
| D6 | `c_ca/c_swv/c_dpv.cpp` | `LPDACSW_VZERO2PIN` closed, tying the Vzero buffer to SE0 | cell current sunk by the buffer, not the TIA |
| D7 | `c_ca/c_swv/c_dpv.cpp` | `vbiasCode = vzero*64 + V/LSB` (should subtract; cell V = Vzero − Vbias) | applied potential polarity inverted |
| D8 | `c_ca/c_swv/c_dpv.cpp` | AFE never woken; CV's `AD5940_ShutDownS()` leaves it hibernating with the LP loop off | register writes succeed while the analog front end is powered down |
| D9 | `cv.cpp` `_AD5940_Main` | unbounded `while (!bTestFinished)` | board wedged mid-campaign, needed a power cycle |

D5–D8 each existed in **three verbatim copies** (one per method class) plus a
fourth unreachable copy in `electrochemical_methods.cpp`. Every fix had to be
applied three times — the duplication is itself the amplifier.

New observable output: **`RTIACAL,<magnitude>,<phase>,<fRcal>`** is printed on
every RTIA calibration. This single line turns an invisible defect into an
obvious one and is the basis of validation check V1.

New diagnostic command: **`k <n>`** selects the LPTIA output topology at runtime
(bit0 = close SW13; bits1–3 = LPTIARF code), so the DC signal path can be
characterised without a reflash.

## Validated: cyclic voltammetry

37 sweeps, raw data in `lab/data/`, analysis in `lab/analyse.py` → `lab/results.json`.

- **Accuracy** (n = 10): R = 10 605.2 ± 1.6 Ω vs 10 560 Ω nominal → **+0.428 %**
- **Repeatability**: %RSD **0.0152 %**; calibrated RTIA 1093.02 ± 0.03 Ω (%RSD 0.0023 %)
- **Residuals**: RMS 12.6 nA = **0.25 LSB** (LSB = 50.8 nA) — quantisation-limited, no structure
- **Invariance**: 25–800 mV/s, ±100…±600 mV, 5–20 mV steps → error spans only
  +0.411 % to +0.485 % (0.074 pp total spread), R² ≥ 0.999994

The residual ~+0.4 % exceeds the component tolerance stack (~±0.11 %). The
dominant untested candidate is the fitted 200 Ω RCAL's own tolerance, which
enters the scale directly via the calibration and is not separable through that
same path. **Characterising RCAL1 against a traceable standard is the single
highest-value next measurement.**

## Operating limit discovered

Sweeps fail whenever the step period `Estep/scan_rate` drops below ~19 ms
(20 ms works, 17.5 ms does not) — about **2.7× the 7 ms `SampleDelay` the
firmware configures**, so the achievable scan rate cannot be read off that
parameter. The limit is on step *period*, not scan rate: 800 mV/s is accurate to
+0.435 % in 20 mV steps, while 400 mV/s fails in 5 mV steps. At 5 mV resolution
the usable maximum is **250 mV/s**.

## Caveat: a timeout still needs a power cycle

Bounding the CV wait (D9) stops the instrument wedging, but it does **not** fully
restore the AFE. Sweeps taken right after a `CV_TIMEOUT` are reproducibly
degraded — R² drops from 0.999999 to ~0.9999 at unchanged accuracy — and recover
only after a reset (confirmed twice). **Treat a timeout as requiring a reset.**
All reported data was taken on a freshly reset board.

## Still broken — CA / SWV / DPV / EIS / OCP

Reported honestly in the paper as unvalidated; do not claim these work.

- **CA / SWV / DPV**: after D5–D8, the ADC converts correctly (zero timeouts,
  genuine transient after each step) but the steady-state current does not
  depend on applied potential. ±200 mV across 10.56 kΩ should differ by 37.9 µA;
  measured difference < 3 nA. **All eight LPTIARF settings × both SW13 states
  were swept and changed nothing**, which places the fault upstream of the
  sensing stage — in establishing the LPPA/CE0 cell drive — not in conversion or
  scaling. The sequencer-driven CV path measures the same cell correctly, so
  this is specific to the direct-register classes.
- **EIS**: the DFT ready flag never asserts at any frequency (15 tried,
  100 Hz–100 kHz). After D4 the sweep now reports `EIS_TIMEOUT` and returns in
  50 s instead of hanging forever.
- **OCP**: returns −1820 mV = exactly −V_ref, i.e. saturated.

## Reproducing

```bash
# build + flash (note: space-free build path is required)
arduino-cli compile --fqbn rp2040:rp2040:seeed_xiao_rp2040 --build-path C:\hsbuild "<sketch>"
arduino-cli upload  --fqbn rp2040:rp2040:seeed_xiao_rp2040 --port COM8 --input-dir C:\hsbuild "<sketch>"

cd lab
python campaign.py     # 37 sweeps -> lab/data/
python analyse.py      # -> lab/results.json + journal/paper/figures/
python fig_setup.py    # -> fig1
```

Paper sources: `journal/paper/` (elsarticle; `pdflatex main; bibtex main;
pdflatex main; pdflatex main`).
