# Project Audit — SteiStat / AD5941_25 Potentiostat

**Scope of this audit:** `Software/update 4 (8 Agustus 26)/AD5941_25/` (the most recent firmware revision) plus repo-root documentation (`README.md`, `Hardware/`, `laporan/`). Older `update 2`/`update 3` folders exist but are not separately audited — they appear to be prior snapshots of the same codebase.

All facts below are cited to a source file. Anything not verifiable from the repository is marked `[DATA REQUIRED]`, `[EXPERIMENT REQUIRED]`, `[REFERENCE REQUIRED]`, `[CODE VERIFICATION REQUIRED]`, or `[FIGURE REQUIRED]`.

---

## 1. Hardware Platform

- **MCU (as built/compiled):** Seeed Studio XIAO **RP2040**, per `README.md`, `laporan/ARCHITECTURE_DOCUMENT.md`, `laporan/TECHNICAL_REPORT_EN.md`, and confirmed by the actual compiler include paths in `AD5941_25/.vscode/c_cpp_properties.json` (Arduino `rp2040` core / pico-sdk / RP2350 hardware structs) and by RP2040-specific code (`analogWriteResolution(16)`, 12-bit `analogRead`, XIAO pin macros `D4`/`D6`–`D10`/`A0`–`A3`).
- **⚠ Discrepancy:** `Hardware/Schematic_AD5941 corr. circ. diag._2025-12-14.pdf` labels the MCU module as **"XIAO ESP32"**, not RP2040. Every narrative document and the actual firmware target RP2040. `[DATA REQUIRED]`: confirm which MCU is populated on the physical board used for the experiments reported in the paper — this must be resolved before the paper states a board name, since a schematic/firmware mismatch is the kind of thing a reviewer will catch.
- **SPI to AD5941:** bit-banged (not native RP2040 SPI0), implemented in `AD5941_25/XIAOPort.cpp`, because MISO/MOSI wiring does not match RP2040's native pin-mux (per `laporan/ARCHITECTURE_DOCUMENT.md` §3.6). Effective SPI clock is in the "hundreds of kHz," well under the AD5941's rated 12 MHz.

## 2. Potentiostat IC / Analog Front End

- IC: **Analog Devices AD5940/AD5941** (electrochemical AFE with integrated potentiostat loop, ADC, DACs, and DFT engine).
- Vendor driver: `AD5941_25/src/ad5940/ad5940.cpp` / `ad5940.h` — plain C register-level driver (confirmed by grep: zero `class`, `virtual`, or inheritance). This is third-party/vendor code, not original project code, and should be cited as such, not claimed as a contribution.
- ADC: 16-bit Sigma-Delta, with SINC3/SINC2+Notch filter chain and a hardware DFT accelerator used for EIS. PGA gain steps: **1.0×, 1.5×, 2.0×, 4.0×, 9.0×** (`AD5941_25/src/hardware/adc_control.cpp`; `laporan/ARCHITECTURE_DOCUMENT.md` §4.1).
- DAC: Low-Power 12-bit DAC (Vzero 6-bit / Vbias 12-bit split) and High-Speed 12-bit DAC (`Config_LPLOOP()` / `SetDACLevel()` in `AD5941_25/AD5941_25.ino`).
- TIA feedback resistor ladder (current-to-voltage gain stage): **200 Ω, 1 kΩ, 5 kΩ, 10 kΩ, 20 kΩ, 40 kΩ, 80 kΩ, 160 kΩ** — 8 selectable codes (`AD5941_25/src/hardware/gain_control.cpp`, function `Hardware_FindOptimum_Rf_PGA`; same table duplicated in `RawToCurrent()` in `c_ca.cpp`/`c_dpv.cpp`/`c_swv.cpp`).
- ADC reference used in conversion math: `Vref_mV = 1820.0` (OCP path also branches to 1835.0 mV depending on a gain bit — see `CalculateOCP()` in `AD5941_25.ino`).
- Waveform generator range referenced in documentation: 0.2 Hz – 200 kHz (`laporan/FLOWCHART.md`).
- A "≈54 pA resolution" figure appears in `laporan/FLOWCHART.md` Part 2 §4 as a bare, undocumented claim with no derivation shown in that file. **Do not reuse this number in the paper without independently re-deriving it** — see `07_resolution_analysis.md` (to be produced in a later phase) for the correct treatment. `[CODE VERIFICATION REQUIRED]`.

## 3. Signal Chain (Current and Voltage Measurement)

Applied potential path: host command → `C_DataStorage` field (e.g. `CA_Voltage_mV`) → `SetDACLevel()` computes a 12-bit HSDAC code from mV using the AD5941's gain/attenuation register bits → `AD5940_WGCfgS()` writes the code → HSDAC → excitation buffer/switch matrix → working/counter electrode.

Current measurement path: cell current → AD5941 TIA (selectable Rf) → PGA (selectable gain) → 16-bit Sigma-Delta ADC → SINC2/SINC3 decimation filters → raw ADC code read via `AD5940_TakeMeasurement()` → converted to amperes in software by `RawToCurrent()`:

```
I = (signed16(code) × Vref_mV/1000) / (PGA_gain × Rf_ohm × 32768)
```
(`AD5941_25/src/electrochemical_methods/c_ca.cpp`, duplicated verbatim in `c_dpv.cpp` and `c_swv.cpp`).

This formula, the TIA table, and the PGA table are the inputs needed for the theoretical resolution derivation in a later phase — see `07_resolution_analysis.md`.

## 4. Communication Interface

- USB-Serial, ASCII single-letter command protocol, default **1,000,000 baud** (`laporan/ARCHITECTURE_DOCUMENT.md`, confirmed in `g_Comm.Begin(1000000, &g_Data)` in `AD5941_25.ino`).
- Two independent, non-interoperating host GUIs both speak this protocol: `AD5941_25/steistat_gui.py` (production) and `AD5941_25/python_ui/steistat_test_ui.py` (diagnostic console).

## 5. Firmware — Project Structure

```
AD5941_25/
├── AD5941_25.ino                  # main sketch: setup()/loop(), OCP + EIS scan orchestration, globals
├── XIAOPort.cpp                   # RP2040 pin mapping + bit-banged SPI to AD5941 (live)
├── cv.cpp / rampTest.cpp / RampTest.h   # legacy CV engine (live, NOT migrated into src/)
├── utilities.h/.cpp               # LED colors, Log()/Info() diagnostics, marker pulses (live)
├── steistat_data_storage.h, steistat_status_utils.h, status_utils.h, ad5940.h, debug.h  # thin wrapper headers (one, status_utils.h, appears unreferenced/dead)
├── steistat_gui.py, mock_serial.py, fix_structs.py   # production GUI + serial mock + one stale one-off script
├── python_ui/                     # second, independent diagnostic GUI + test/parsing scripts
├── ad5941_register_test/          # separate standalone sketch for SPI/chip-ID smoke testing
├── hasil/                         # existing result plots: CA.png, CV.png, DPV.png, SWV.png, "dummy cell.png"
└── src/
    ├── ad5940/                    # vendor AD5940 register driver (C, not original code)
    ├── command_processing/        # free-function command parser — PARALLEL DUPLICATE of communication/
    ├── communication/             # C_Communication class — class-based command parser + dispatcher
    ├── data_storage/              # C_DataStorage (parameter struct-as-class) + measurement_buffer (dup. buffer)
    ├── electrochemical_methods/   # C_CA, C_DPV, C_SWV, C_EIS, C_OCP classes + a free-function duplicate file
    ├── hardware/                  # adc_control, gain_control, wave_gen — free functions, hardware config
    ├── interface/                 # led_interface — free functions, NeoPixel control (has a live bug)
    ├── setup/                     # C_AD5941_Setup class + empty ad5941_calibration.h
    └── utils/                     # status_utils — thin re-wrap of interface/ (redundant indirection)
```

Notably: **CV (Cyclic Voltammetry) has no class in `src/electrochemical_methods/`** — it still runs entirely through the legacy top-level `cv.cpp`/`rampTest.cpp`/`RampTest.h` engine, unlike CA/DPV/SWV/EIS/OCP which were migrated into `src/`. This is an architectural inconsistency the paper's Section 2 must either explain or fix before claiming a uniform OOP method layer.

## 6. GUI / Application

| File | Framework | Role |
|---|---|---|
| `steistat_gui.py` | tkinter/ttk + matplotlib (`FigureCanvasTkAgg`) | Production GUI: OCP/CV/EIS/CA tabs, serial control, live plotting, CSV export, class `SteiStatGUI` |
| `python_ui/mock_serial.py` | — | `MockSerial`: synthesizes fake CA/CV/EIS/OCP data so the GUI runs without hardware |
| `python_ui/steistat_test_ui.py` | tkinter (raw `Canvas`, no matplotlib) | Diagnostic console: `SerialReader` (threaded), `TestUI`, own built-in dummy-data generator (duplicate of `mock_serial.py`) |
| `python_ui/test_parser_offline.py` | — | `SerialDataParser` + `unittest` cases — a **third, independent copy** of the line-parsing logic already in `steistat_gui.py` and `steistat_test_ui.py` |
| `python_ui/test_dummy_simulation.py`, `test_hardware_ca_swv_dpv.py`, `ad5941_detect_test.py` | — | Ad hoc test/diagnostic scripts, procedural |

Dependencies declared: only `pyserial>=3.5` in `requirements.txt`; `matplotlib`/`numpy` are imported but **undeclared**. `[CODE VERIFICATION REQUIRED]`: pin exact versions used for reproducibility before publication.

## 7. Programming Languages

- Firmware: C++ (Arduino dialect, `.ino` + `.cpp`/`.h`), compiled against the `arduino-pico` RP2040 core.
- Host application: Python 3 (version unpinned — `[DATA REQUIRED]`).

## 8. Main Entry Points

- Firmware: `AD5941_25/AD5941_25.ino` (`setup()` / `loop()`).
- GUI: `AD5941_25/steistat_gui.py` (`if __name__ == "__main__"`) and, separately, `AD5941_25/python_ui/steistat_test_ui.py`.

## 9. Configuration Files

- `AD5941_25/.vscode/c_cpp_properties.json` — IntelliSense/compiler include paths (RP2040 core), not a build config in the CMake/PlatformIO sense.
- No `platformio.ini`, `library.properties`, or `boards.txt` anywhere in `Software/` — this is a plain **Arduino IDE** sketch, not a packaged library or PlatformIO project.

## 10. Build System / Deployment Process

- Arduino IDE, manual library installation. `Software/Arduino_Libraries/` ships zipped copies of: `Adafruit_NeoPixel`, the AD5940 vendor driver library, and `arduino-printf` (`LibPrintf.h`, used by `cv.cpp`).
- `Software/Arduino_Libraries/README.txt` exists but is **empty** — no documented install steps despite `Software/README.md` referencing it.
- No documented board-manager URL, port/baud selection instructions, or upload command were found anywhere in the repository. `[DATA REQUIRED]` before writing §2.6/Phase 12 deployment documentation — the actual flashing procedure must be captured from whoever built the hardware, not assumed.
- Windows GUI installer `SteiStat-v700.exe` is referenced in `Software/README.md` as a packaged distributable, but the packaging process (PyInstaller? cx_Freeze?) is not documented in-repo. `[DATA REQUIRED]`.

## 11. Calibration System

- `C` (ASCII command) → `AD5940_PGA_Calibration()` (offset/gain PGA calibration).
- `Z` → full chip re-init (`AD5941_InitAll`).
- HSDAC range calibration: `Calibrate_HSDAC()` in `AD5941_25.ino`, run once per EIS sweep before frequency stepping.
- No automated multi-point calibration-verification procedure exists; `laporan/TECHNICAL_REPORT_EN.md` §9 lists "Calibration Automation" as a **future recommendation**, i.e. not yet implemented. This directly affects Phase 6/8 — accuracy results must be reported against whatever manual calibration procedure was actually followed, and that procedure needs to be written down. `[EXPERIMENT REQUIRED]` / `[DATA REQUIRED]`.

## 12. Electrochemical Methods Implemented

| Method | Status | Where implemented |
|---|---|---|
| OCP | Implemented, used for host-side cross-check via RP2040 native ADC pins | `AD5941_25.ino` (`Do_AD5941_OCP_Measurement`, `CalculateOCP`), `src/electrochemical_methods/c_ocp.*` |
| CV | Implemented (legacy engine only) | `cv.cpp`, `rampTest.cpp` |
| EIS | Implemented | `AD5941_25.ino` (`eisScan`), `src/electrochemical_methods/c_eis.*` |
| CA | Implemented; **dummy-cell test showed flat/noisy output with no discernible decay**, attributed in `laporan/UPDATE_2026-08-01.md` to a hardcoded working-electrode channel (`SWN_SE0`) — status: open/unresolved | `src/electrochemical_methods/c_ca.*` (+ duplicate free-function version in `electrochemical_methods.cpp`) |
| SWV | Implemented; same open dummy-cell issue as CA | `src/electrochemical_methods/c_swv.*` (+ duplicate) |
| DPV | Implemented; a plotting bug (spurious diagonal line) is reported fixed at the UI level | `src/electrochemical_methods/c_dpv.*` (+ duplicate) |

This status directly bounds what Phase 8/9 of the paper can honestly claim: CA/SWV results against the dummy cell were **not yet validated as physically sensible** as of the last documented session. `[EXPERIMENT REQUIRED]` — this must be re-run and confirmed (or the root cause fixed) before it can appear as a results figure.

## 13. Data Processing / Storage

- In-firmware: `C_DataStorage` centralizes ~50 public parameter fields (see `02_oop_analysis.md` for critique); a separate `measurement_buffer.cpp` and a `Measurements[]` array inside `C_DataStorage` are two **divergent** copies of the same buffering concept.
- No on-device file storage (no SD card / flash logging found) — data is streamed over serial and saved host-side (CSV, per `steistat_gui.py`).
- No structured data format (e.g., JSON/HDF5) — CSV only, and export logic is duplicated across the two GUIs.

## 14. Existing Experimental Data / Plots

- `AD5941_25/hasil/` contains **CA.png, CV.png, DPV.png, SWV.png, "dummy cell.png"** — screenshots from a prior test-console session against a 3-branch dummy cell (documented in `laporan/UPDATE_2026-08-01.md` §2): 
  - we-A: anti-parallel diodes (BAS16) ‖ 33 nF, in series with 560 Ω
  - we-B: 10 kΩ (0.1%) resistor only
  - we-C: 10 kΩ ‖ 33 nF, in series with 560 Ω
- These images are **diagnostic screenshots, not publication-ready figures** — they show the test console UI, not clean plotted results, and the session log documents unresolved issues (CV stuck at 2 points, CA/SWV flat/noisy). `[FIGURE REQUIRED]`: none of these can be used directly as paper figures; new, clean measurement runs are needed once the known bugs are addressed. `[DATA REQUIRED]` for the exact resistor/capacitor tolerances if precision numbers are to be quoted (informal "0.1%" is stated only for we-B's 10 kΩ resistor).

## 15. Existing Documentation

Already in the repo (repo-relative):
- `README.md`, `Hardware/README.md`, `Software/README.md`
- `laporan/ARCHITECTURE_DOCUMENT.md` — system + software architecture narrative, some Mermaid diagrams
- `laporan/ELECTROCHEMICAL_METHODS_EXPLANATION.md`
- `laporan/FLOWCHART.md` — call-flow and signal-path diagrams
- `laporan/PARAMETER_DAN_PERHITUNGAN_METODE_ELEKTROKIMIA.md` — parameters and formulas (Indonesian)
- `laporan/TECHNICAL_REPORT_EN.md` — English technical report, most paper-ready of the existing docs
- `laporan/UPDATE_2026-08-01.md` — session log documenting the dummy-cell test and open bugs

These are a strong starting point — much of Phase 3/4 (architecture description, diagrams) can adapt content already drafted here rather than starting from zero, provided the OOP claims are corrected per `02_oop_analysis.md`.

## 16. Existing Schematics / PCB Information

- `Hardware/Schematic_AD5941 corr. circ. diag._2025-12-14.pdf` (EasyEDA export, drawn by "istvan.vamos", dated 2024-02-21, Rev 1.0). Contains the MCU-label discrepancy noted in §1.
- `Hardware/README.md` references Gerber files and a BOM/Pick-and-Place file set for PCB fabrication/assembly ordering — exact file locations/contents not independently verified in this pass. `[CODE VERIFICATION REQUIRED]` if PCB specs are to be cited in the paper's hardware section.
- No PCB layout image suitable as a figure was located. `[FIGURE REQUIRED]`.

## 17. Version History

Three dated snapshots exist under `Software/`: `update 2 (18 Juli 26)`, `update 3 (1 Agustus 26)`, `update 4 (8 Agustus 26)` (current), plus a top-level `update (1 august 26)/` containing the same `laporan/` set. Only the 2026-08-01 session is narrated in a changelog (`UPDATE_2026-08-01.md`); no changelog entries exist specifically for update 3 → update 4. `[DATA REQUIRED]` if the paper needs to state what changed in the most recent revision.

## 18. Authorship / Licensing

- `AD5941_25.ino` (top of file): MIT-style permissive license, author "Kent".
- `SteiStat/SteiStat.ino`: MIT License, author Richard Morrison (Instruments4Chem, Melbourne, Australia) — this is the original/reference EIS sketch this project was adapted from.
- No repository-wide LICENSE file was located. `[DATA REQUIRED]` — needed for any open-source/reproducibility claim in Phase 5.

---

## Summary Table — What Exists vs. What's Missing for the Paper

| Category | Status |
|---|---|
| Hardware description | Mostly available; MCU discrepancy must be resolved |
| Firmware architecture description | Available in `laporan/`, needs correction per OOP audit |
| GUI description | Available from source inspection |
| Deployment/build steps | Largely undocumented — needs to be captured from the person who has flashed the board |
| Calibration procedure | Partially implemented, not automated, not documented step-by-step |
| Dummy-cell experimental data | Exists only as informal diagnostic screenshots with known unresolved bugs (CA/SWV) — not usable as-is |
| Resolution/accuracy/noise data | None exists — all `[EXPERIMENT REQUIRED]` |
| PCB/schematic figures | Exists but has an unresolved MCU-labeling discrepancy |
| License | Per-file MIT notices only, no repo-level LICENSE |
