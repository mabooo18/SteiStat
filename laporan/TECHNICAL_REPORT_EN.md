# Comprehensive Technical Report: Portable Electrochemical Potentiostat Based on the AD5941 AFE and XIAO RP2040

---

## 1. Introduction and Background

Electrochemistry is a fundamental science bridging chemical reactions and electrical signals. Investigating redox reactions, corrosion, sensors, and bio-impedance requires a **potentiostat**—an instrument that controls the voltage difference between a Working Electrode (WE) and a Reference Electrode (RE) while measuring the resulting current flowing through a Counter Electrode (CE).

Historically, potentiostats were bulky, expensive laboratory instruments. Recent demands for Point-of-Care (PoC) medical diagnostics, environmental field testing, and wearable sensors have driven research into **portable, low-cost, and handheld potentiostats**. 

This report details the implementation, architecture, hardware design, and software refactoring of the **HunStat2**, a portable potentiostat based on the **Analog Devices AD5941** Analog Front End (AFE) and the **Seeed Studio XIAO RP2040** microcontroller.

---

## 2. Literature Review and Design Frameworks

### 2.1. Electrochemical Fundamentals
An electrochemical cell contains three electrodes:
* **Working Electrode (WE)**: The reactive surface where the redox reaction of interest occurs.
* **Reference Electrode (RE)**: An electrode with a stable, known potential (e.g., Ag/AgCl) used as a voltage reference.
* **Counter Electrode (CE)**: A conductor that supplies the current necessary to balance the reaction at the WE.

The system relies on a feedback control loop (potentiostat loop) to force the WE potential ($V_{WE}$) to match a target value relative to the RE ($V_{RE}$). Current is measured at the WE using a Transimpedance Amplifier (TIA).

### 2.2. Handheld Potentiostat Frameworks: FreiStat and HELPStat
Modern handheld designs draw inspiration from open-source potentiostat frameworks:
* **FreiStat**: A modular framework emphasizing clean separation between hardware-specific setups, communication protocols, and electrochemical methods.
* **HELPStat**: A handheld, EIS-enabled potentiostat demonstrating that high-precision Impedance Spectroscopy can be achieved on a battery-powered micro-platform using the AD5940/AD5941 family.

Our refactoring of the HunStat2 firmware adopts the **FreiStat modular paradigm**, transitioning from a monolithic code structure to a layered, maintainable software design.

---

## 3. AD5941 Architecture and Register Configurations

The AD5941 is a high-precision, low-power impedance and electrochemical front end. 

```
                                AD5941 BLOCK DIAGRAM
  +---------------------------------------------------------------------------------+
  |  +------------------------+      +-------------+                                |
  |  | Low-Power Loop (LPTIA) | <--- | 12-Bit DAC  | <----+                         |
  |  +------------------------+      +-------------+        |                         |
  |                                                         v                         |
  |  +------------------------+      +-------------+   +---------+   +-------------+  |
  |  | High-Speed Loop (HSTIA)| <--- | 12-Bit DAC  |   | Switch  |-->|  Electrodes |  |
  |  +------------------------+      +-------------+   | Matrix  |   | (WE, RE, CE)|  |
  |                                  +-------------+   |  (MMR)  |   +-------------+  |
  |  +------------------------+      | Wave Gen    |   +---------+          |         |
  |  | 16-Bit SD ADC          | <--- | (Sinusoidal)| <------+               |         |
  |  +------------------------+      +-------------+                        v         |
  |              |                                                     +---------+    |
  |              v                                                     |   TIA   |    |
  |  +------------------------+                                        +---------+    |
  |  | DFT Accelerator Engine |                                             |         |
  |  +------------------------+                                             v         |
  |              |                                                    [ADC Input]     |
  |              +------------------------> [ FIFO Buffer ] ---------------------------> SPI
  +---------------------------------------------------------------------------------+
```

### 3.1. Internal Subsystems
1. **Dual Control Loops**:
   * **Low-Power (LP) Loop**: Features a low-power potentiostat amplifier and a Low-Power TIA (LPTIA). Designed for slow DC sweeps (e.g., CV, CA, OCP, DPV, SWV) to minimize power draw.
   * **High-Speed (HS) Loop**: Optimized for high-frequency AC excitation, featuring a high-bandwidth amplifier and a High-Speed TIA (HSTIA) for Impedance Spectroscopy (EIS).
2. **Excitation Signal Generator**: Supports DC bias levels and AC sinusoidal signal generation via the High-Speed DAC (HSDAC).
3. **16-Bit Sigma-Delta ADC**: Configurable Oversampling Rates (OSR) with digital Sinc3, Sinc2, and Notch filters.
4. **DFT Hardware Accelerator**: Calculates real and imaginary Fourier coefficients internally for EIS, saving MCU overhead.
5. **Programmable Switch Matrix**: Internal Multiplexer switches configured via Memory-Mapped Registers (MMR) to route internal amplifiers to external pins.

### 3.2. Key Register Mappings
* **`REG_AFE_ADCCON`**: Configures the ADC, positive/negative multiplexer inputs, and the Programmable Gain Amplifier (PGA) gains ($1.0\times, 1.5\times, 2.0\times, 4.0\times, 9.0\times$). Also where `C_OCP::Calculate()` reads back the `GNPGA` (PGA gain-in-effect) and `MUXSELN` bitfields after a conversion to know which reference voltage and gain to un-apply when decoding the raw code.
* **`REG_AFE_DATAFIFORD`**: Direct read access to the FIFO buffer containing conversion results. For EIS this is read twice per frequency step (real, then imaginary DFT component); for CA/SWV/DPV the ADC's Sinc2 filter output is read via `AFERESULT_SINC2` instead of the FIFO.
* **HSTIA feedback block (`HpLoopCfg.HsTiaCfg`)**: not a single register but a cluster (`HstiaRtiaSel`, `HstiaBias`, `HstiaCtia`, `HstiaDeRtia`, `HstiaDeRload`, `DiodeClose`) written together via `AD5940_HSLoopCfgS()`. This is the transimpedance amplifier that turns cell current into a measurable voltage for CA/SWV/DPV/EIS — see the electrochemical methods document §2.1 for a full field-by-field breakdown, including a bug found and fixed this session where three of the five method classes never wrote this block at all.
* **Switch Matrix Register Codes**:
  * `SWD_CE0` / `SWP_RE0` / `SWN_SE0`: Connects Counter Electrode 0, Reference Electrode 0, and Sensor Electrode (Working) 0 — the standard 3-electrode routing used by CA/SWV/DPV and EIS mode 0.
  * `SWD_RCAL0` / `SWP_RCAL0` / `SWN_RCAL1`: Routes the excitation and TIA input across the on-board calibration resistor instead of the electrodes — used by EIS mode 1 (Rcal sweep) to establish a known-impedance reference for the Nyquist math (§6).
  * `SWT_TRTIA`: Routes current through the Transimpedance Amplifier feedback loops.

### 3.3. ADC Filter Chain and DFT Sizing (`Hardware_Init_AD5940_ADC`, EIS only)

CA/SWV/DPV always use one fixed filter chain (Sinc3 OSR=4, Sinc2 OSR=1333, 16-sample averaging — see §2.1/§6 in the electrochemical methods document). EIS instead re-selects the filter chain and DFT window size *every frequency step*, because a fixed configuration cannot simultaneously satisfy the Nyquist sampling requirement at 0.01 Hz and at 100 kHz. `Hardware_Init_AD5940_ADC(freq)` picks one of six bands:

| Frequency band | Sinc2 OSR | Sinc3 OSR | Sinc2 notch | DFT size | DFT source |
|---|---|---|---|---|---|
| < 0.11 Hz | 1067 | 4 | enabled | 16384 | Sinc2+Notch |
| 0.11–0.51 Hz | 267 | 5 | enabled | 8192 | Sinc2+Notch |
| 0.51–5 Hz | 178 | 4 | enabled | 8192 | Sinc2+Notch |
| 5–450 Hz | 44 | 4 | enabled | 4096 | Sinc2+Notch |
| 450 Hz–80 kHz | 178 | 4 | **bypassed** | 16384 | Sinc3 (raw) |
| > 80 kHz | 178 | 2 | **bypassed** | 16384 | Sinc3 (raw) |

Lower frequencies get a larger DFT window (more cycles captured per bin, better frequency resolution and noise rejection) at the cost of longer acquisition time per point; the notch/Sinc2 stage is bypassed above 450 Hz because at those speeds the Sinc2 decimation stage's own bandwidth becomes the limiting factor rather than mains-frequency noise rejection. All bands apply a Hanning window (`HanWinEn = bTRUE`) before the DFT to reduce spectral leakage from non-integer-cycle capture windows.

### 3.4. Automatic Gain Selection (`Hardware_FindOptimum_Rf_PGA`)

When `use_variable_gain` (`s` command) is enabled, the firmware doesn't use a fixed TIA resistor / PGA gain pair for EIS — it computes a **target combined gain** ($CG = R_{TIA} \times PGA$) as a function of frequency, log-interpolated between a user-set `CGMax` (`t` command, at the low-frequency end) and `CGMin` (`u` command, at the high-frequency end):

$$\log_{10}(CG_{target}) = m \cdot \log_{10}(f) + c, \quad m = \frac{\log_{10}(CG_{min}) - \log_{10}(CG_{max})}{6}, \quad c = \frac{5\log_{10}(CG_{max}) + \log_{10}(CG_{min})}{6}$$

The constant `6` in the slope comes from the frequency sweep implicitly spanning roughly 6 decades of standard AD5941 use (0.1 Hz–100 kHz territory); it is a fixed calibration constant, not derived from the actual `FreqLo`/`FreqHi` the user configured. The firmware then linearly scans all $8 \times 5 = 40$ combinations of the 8 TIA resistor steps ($200\ \Omega$ … $160\ \text{k}\Omega$) and 5 PGA gains ($1\times$ … $9\times$), and picks whichever combination's product is closest to the interpolated target — a low frequency (near `CGMax`) biases toward a larger resistor/gain (more amplification, since cell impedance and expected currents are typically smaller at DC-like conditions), and a high frequency biases toward a smaller one (avoiding TIA output saturation as impedance drops and current rises).

---

## 4. Hardware Potentiostat Design

### 4.1. Schematic & Microcontroller Wiring
The XIAO RP2040 talks to the AD5941 over SPI, but **not through the RP2040's hardware SPI0 peripheral** — the board's physical wiring assigns MISO and MOSI the opposite way around from what the RP2040 silicon's fixed pin-mux expects for those pins, so the peripheral cannot be used as-is. The current pin layout is:

| XIAO RP2040 Pin | AD5941 Pin | Description |
|---|---|---|
| **D8** | SCK | SPI Clock — driven directly via `digitalWrite`, no peripheral involved |
| **D9** | MOSI | SPI Master Out Slave In — bit-banged |
| **D10** | MISO | SPI Master In Slave Out — bit-banged, read via `digitalRead` |
| **D7 (CS)** | CS | SPI Chip Select (Active Low) |
| **A1 (RESET)** | Reset | Hardware Chip Reset (active low) |
| **A2 (INT)** | GP0 / INT | External Interrupt Input (Falling Edge MCU ISR) |

Firmware-side, `XIAOPort.cpp` implements a small `BitBangSPI` class (SPI Mode 0: CPOL=0, CPHA=0, MSB-first) that shifts each byte out by toggling `D8` and reading `D10` in an 8-iteration loop, called from `AD5940_ReadWriteNBytes()`. This replaces what used to be a direct `SPI.begin()`/`SPI.transfer()` call through Arduino's hardware SPI library. The trade-off is throughput: bit-banging via `digitalWrite`/`digitalRead` is bounded by GPIO toggle latency (order of hundreds of kHz effective clock), well under the AD5941's rated 12 MHz SPI ceiling — acceptable here since none of CA/SWV/DPV/EIS/OCP are SPI-bandwidth-bound (the bottleneck is always the electrochemical settling delay or the DFT compute time, not register I/O). The one general symptom to watch for with any SPI wiring problem — hardware or bit-banged — is `CHIPID` reading back as `0x0000` or `0xFFFF`, which halts firmware initialization (see §7.2).

### 4.2. Power and Decoupling
To achieve low noise floor measurements down to the pico-ampere range:
* Separate analog power ($AV_{DD}$) and digital power ($DV_{DD}$) domains are created, decoupled using $100\text{ nF}$ and $10\text{ }\mu\text{F}$ ceramic capacitors placed close to the AFE pins.
* An external low-drift calibration resistor ($R_{cal} = 10\text{ k}\Omega, 0.1\%$) is placed across the `RCAL0` and `RCAL1` pins to calibrate TIA gain errors.

---

## 5. Software Design and Modular Architecture

The refactored software structure separates orchestrators, parsers, and hardware drivers:

```
                               SOFTWARE DIRECTORY WALK
  AD5941_25/
  ├── AD5941_25.ino           <-- Main orchestrator: initializes hardware and runs loop()
  ├── cv.cpp                  <-- Cyclic Voltammetry configuration and launcher
  ├── rampTest.cpp            <-- Low-level Analog Devices sequencer setup
  ├── XIAOPort.cpp            <-- SPI interface, hardware pin maps, and MCU ISR
  ├── utilities.cpp           <-- Binary-to-float math (ToFloat) and NeoPixel status
  └── src/
      ├── setup/              <-- PGA calibrations and chip resets
      ├── data_storage/       <-- Global parameter storage and measurement caches
      ├── communication/      <-- Command parser and serial dispatcher (LIVE path)
      ├── command_processing/ <-- DEAD CODE: pre-refactor duplicate parser, never called (see 5.1 note)
      ├── hardware/           <-- ADC clock, switch matrix, and TIA gain optimizers
      ├── interface/          <-- Low-level RGB NeoPixel color configurations
      ├── utils/              <-- Status LED translation utilities
      └── electrochemical_methods/ <-- Isolated technique implementations (CA, SWV, EIS, OCP, DPV);
                                      also contains a DEAD CODE duplicate (electrochemical_methods.cpp's
                                      free-function RunCA/RunSWV/RunDPV) alongside the live C_CA/C_SWV/C_DPV classes
```

> **Live vs. dead code paths.** Arduino compiles every `.cpp` under the sketch tree regardless of whether it's called, so this project currently ships two independent, fully-formed implementations of the serial protocol and CA/SWV/DPV: the class-based one (`communication/` + the `C_*` classes in `electrochemical_methods/`), which is what `AD5941_25.ino`'s `loop()` actually invokes via `g_Comm.ReadAndProcess()`; and an older, pre-refactor procedural one (`command_processing/` + the free functions `RunCA`/`RunSWV`/`RunDPV`/`Config_AD5941_DCMeasurement` at the top of `electrochemical_methods.cpp`) operating on plain global variables instead of `C_DataStorage`, which nothing in the call graph reaches. The dead copy is also functionally *stale* — it still has the short 100-tick timeout and missing HSTIA config described in §7.5/§7.6, since only the live classes were patched. Anyone extending this firmware should confirm which file they're editing before assuming a change will take effect.

### 5.1. Command Delimiter & Tokenizer Parser
The `C_Communication::ReadAndProcess()` function reads incoming characters from the USB serial buffer. It separates commands using delimiters (`;`, `|`, `\r`, `\n`) via `strtok` and dispatches tokens to type-specific parsers:
* **Single parameter floats**: Parse commands like `b-50.00` (sets bias voltage to -50 mV).
* **Multiple parameter commands**: E.g. `D 100,200,5,10,2` (configures CV sweep parameters: $V_{start}$, $V_{stop}$, $E_{step}$, $ScanRate$, $Cycles$).
* **Execution triggers**: Single character commands such as `O` (OCP), `E` (EIS), `A` (CA), `W` (SWV), `D` (DPV), `M` (CV).

### 5.2. Dynamic AFE Sequencer (`rampTest.cpp`)
Voltammetric sweeps (like CV and LSV) require fast, precisely timed steps. The AD5941's internal hardware sequencer is programmed dynamically:
* `AppRAMPInit()`: Translates physical parameters (limits, scan rates) into LP-DAC voltage step registers. It compiles command sequences into the AFE SRAM memory.
* `AppRAMPISR()`: Triggered on the falling edge of pin `A2` (GP0 interrupt). It reads data from the AFE FIFO, averages samples, converts raw registers, and sends coordinate data to the serial port.

---

## 6. Electrochemical Working Mechanisms in Code

### 6.1. Open Circuit Potential (OCP)
Measures the cell voltage without applying current. The AFE routes the positive input multiplexer to the working electrode and the negative multiplexer to the reference electrode. No bias DAC is active (high-impedance mode).

### 6.2. Cyclic Voltammetry (CV)
Sweeps potential linearly in a triangular waveform. Code in `rampTest.cpp` handles the step updates and schedules ADC reads using the hardware sequencer:
$$\text{Update LP-DAC} \rightarrow \text{Settling Delay} \rightarrow \text{ADC Conversion}$$

### 6.3. Chronoamperometry (CA)
Applies a potential step to the cell and measures current over time:
1. `SetDACLevel(CA_Voltage_mV)` is called.
2. The ADC runs continuously, capturing current at the configured sampling rate.
3. Coordinates are output as `CA,<time>,<current>`.

### 6.4. Square Wave Voltammetry (SWV)
Excitation potential consists of a staircase waveform with a high-frequency square wave.
For each step:
1. Apply $V_{forward} = V_{step} + V_{amplitude}$, sample current $I_{forward}$.
2. Apply $V_{reverse} = V_{step} - V_{amplitude}$, sample current $I_{reverse}$.
3. Output the differential current: $\Delta I = I_{forward} - I_{reverse}$.

### 6.5. Differential Pulse Voltammetry (DPV)
Excitation potential consists of periodic pulses on a staircase waveform.
For each step:
1. Measure baseline current $I_{base}$ before the pulse.
2. Apply a pulse voltage $V_{pulse} = V_{step} + V_{amplitude}$, measure current $I_{pulse}$ at the end of the pulse.
3. Output the differential current: $\Delta I = I_{pulse} - I_{base}$.

### 6.6. Electrochemical Impedance Spectroscopy (EIS)
Applies a low-amplitude AC sine wave voltage over a range of frequencies:
1. Configure sinusoidal waveform generator via `wave_gen.cpp` at frequency $f$.
2. Route excitation through the cell (or $R_{cal}$ in calibration mode).
3. Enable the on-chip DFT engine:
   * Real component: $I_{real} = \text{DFT\_REAL}$
   * Imaginary component: $I_{imag} = \text{DFT\_IMAG}$
4. Compute complex impedance and project coordinates onto the Nyquist plane.

---

## 7. Iterative Debugging Processes

Throughout development, several integration issues were identified and resolved:

### 7.1. Empty Plot Rendering in the Python UI
* **Symptom**: The Python GUI connected to the MCU serial port, but the live graph remained empty.
* **Root Cause**: The UI parser expected strict tag prefixes (e.g. `CA,x,y`) to map coordinate points. The older firmware output raw un-tagged values or logs that caused parsing exceptions.
* **Solution**: Standardized all technique print logs (e.g., `CA,%.4f,%.4e`, `SWV,%.2f,%.4e`) and updated the Tkinter UI parsing regular expressions in `hunstat2_test_ui.py`.

### 7.2. SPI Bus and Chip ID Invalidation
* **Symptom**: The AFE failed to initialize; calling the read-chip-ID function returned `0x0000` or `0xFFFF`.
* **Root Cause**: Poor SPI bus signal integrity or incorrect pin mapping.
* **Solution**: Introduced a command line register probe `I 0x0404`. If `CHIPID` reads `0x0000` or `0xFFFF`, the firmware isolates it as a wiring/power supply error, preventing software debugging of non-responsive hardware.

### 7.3. High-Frequency Clock Phase Shift in EIS
* **Symptom**: Distortion in EIS Nyquist plots at frequencies above $80\text{ kHz}$.
* **Solution**: Configured the code to automatically switch the internal system clock to 32MHz mode (`HfOSC32MHzMode = bTRUE`) and activate High-Power AFE mode (`AD5940_HPModeEn(bTRUE)`) when frequency exceeds $80\text{ kHz}$.

### 7.4. Board Wiring Required Bit-Banged SPI
* **Symptom**: The AFE's `SPI.begin()`/`SPI.transfer()` path (Arduino hardware SPI0) could not talk to the chip at all, or produced the same `CHIPID` failure described in §7.2.
* **Root Cause**: This board's actual PCB wiring connects the AD5941's MISO/MOSI to the XIAO RP2040's `D10`/`D9` pins in the *opposite* role from the RP2040's fixed hardware SPI0 pin-mux (which expects MISO on `D9`, MOSI on `D10`). The silicon can't remap that, so no combination of `SPISettings` fixes it.
* **Solution**: Replaced the hardware SPI calls in `XIAOPort.cpp`'s `AD5940_ReadWriteNBytes()` with a minimal bit-banged `BitBangSPI` class driving `D8` (SCK), `D9` (MOSI), `D10` (MISO) directly via `digitalWrite`/`digitalRead`. See §4.1 for the corrected pin table.

### 7.5. CA/SWV/DPV Current Readings Silently Wrong or Zero
* **Symptom**: Running CA/SWV/DPV against a live dummy test cell produced measurements, but with values that didn't correspond to expected cell behavior (or, in earlier states of this bug, a flat zero regardless of applied voltage).
* **Root Cause**: `C_CA::ConfigDCMeasurement()`, `C_SWV::ConfigDCMeasurement()`, and `C_DPV::ConfigDCMeasurement()` configured the switch matrix but never wrote `HpLoopCfg.HsTiaCfg` — the HSTIA feedback resistor/bias/routing block (§3.2) — before calling `AD5940_HSLoopCfgS()`. Since `HpLoopCfg` is a shared global struct, the TIA feedback path was left at whatever state a previous call (or chip reset) left it in.
* **Solution**: All three `ConfigDCMeasurement()` implementations now explicitly populate `HstiaBias`, `HstiaCtia`, `HstiaDeRload`, `HstiaDeRtia`, and `HstiaRtiaSel` (keyed off the same `tia_rf` index used by `RawToCurrent()`'s resistor lookup) before every measurement config.

### 7.6. `MeasureCurrentRaw()` Could Silently Time Out
* **Symptom**: Related to §7.5 — even after fixing the HSTIA config, readings could occasionally read as a stale/zero value.
* **Root Cause**: The same three classes never routed the ADC's `SINC2RDY` interrupt source into `AFEINTC_1` via `AD5940_INTCCfg()`. `AD5940_TakeMeasurement()`'s internal ready-flag poll checks that interrupt controller, so without the routing it could never observe a "ready" state and would spin until its timeout counter (previously a very short `100` ticks × 10 µs) expired, returning whatever stale value was left in the result register.
* **Solution**: Added `AD5940_INTCCfg(AFEINTC_1, AFEINTSRC_ALLINT, bTRUE)` + `AD5940_INTCClrFlag(AFEINTSRC_ALLINT)` to all three `ConfigDCMeasurement()` implementations, and raised the timeout budget to `1000` ticks as a safety margin.

### 7.7. DPV Step Size Was Not Adjustable Over Serial
* **Symptom**: `DPV_Step_mV` could only be changed by editing `DEFAULT_DPV_STEP` in `data_storage.h` and reflashing — every other DPV parameter (`Start`, `End`, `Amplitude`) had a serial command, but step size didn't.
* **Solution**: Added `case '!'` to `C_Communication::ProcessCommand1Float()`, mapping `!<float>` to `DPV_Step_mV`. Because `!` alone (no digits) is also the existing "print command history" trigger, both behaviors coexist safely due to the tokenizer's match order — see the architecture document §4.4.

### 7.8. `Calibrate_HSDAC()` Left a Stale Clock Flag for Later EIS Steps
* **Symptom**: Under specific sequences (calibration immediately followed by a low-frequency EIS sweep), a subsequent EIS step could run at an unexpectedly fast clock rate.
* **Root Cause**: `Calibrate_HSDAC()` wrote `HfOSC32MHzMode = bTRUE` directly into the shared, file-scope `clk_cfg` struct to speed up calibration itself. `C_EIS::Run()` later reads that same struct per-frequency-step to decide whether to enable 32 MHz mode (only meant for `> 80 kHz`, see §7.3) — leaving the flag `bTRUE` from a prior calibration pass could make the *next* EIS run start in the wrong clock mode before its own per-step logic corrected it on the first step past the threshold check.
* **Solution**: `Calibrate_HSDAC()` now builds its clock settings in a local `local_clk_cfg` struct instead of the shared global, and separately re-asserts only `clk_cfg.HFOSCEn = bTRUE` (needed because `AD5940_CLKCfg()` only honors `HfOSC32MHzMode` when `HFOSCEn` is true) without touching the shared struct's `HfOSC32MHzMode` field.

### 7.9. Python UI Silently Misread DPV Parameters as Volts Instead of Millivolts
* **Symptom**: DPV sweeps launched from the Tkinter UI produced a tiny, flat, or nonsensical voltage range even when the on-screen fields showed reasonable millivolt values (e.g. `0` to `1400`).
* **Root Cause**: `hunstat2_test_ui.py`'s DPV parameter builder called `_coerce_to_volt()` on every field unconditionally, assuming the UI's entry values were always in the same unit the rest of the app used internally. For a `0`–`1400` mV sweep this collapsed the whole range into roughly a 1.4 V span mis-scaled against the firmware's mV-based protocol (§4.2 of the architecture document).
* **Solution**: The DPV builder now inspects the actual magnitude of the entered start/end values — if their difference exceeds `5.0`, it assumes the user typed millivolts directly (the common case) and skips the extra scale conversion; otherwise it treats the input as volts and converts. This is a heuristic, not a unit-aware input field, so it is worth revisiting if a future UI redesign adds explicit unit selectors instead.

---

## 8. Expected vs. Actual Results

Here is a summary of expected results for a Randles cell model (standard electrical equivalent model of an electrochemical cell):

| Method | Expected Plot Profile | Physical Significance |
|---|---|---|
| **CV** | Duck-shaped voltammetric curve | Shows oxidation and reduction peaks. Peak separation indicates reaction reversibility. |
| **EIS** | Nyquist plot: Semi-circle at high frequencies, straight line ($45^\circ$ Warburg impedance) at low frequencies | Semi-circle diameter represents charge transfer resistance ($R_{ct}$). Intercept represents solution resistance ($R_s$). |
| **CA** | Cottrell decay curve ($I \propto t^{-1/2}$) | Demonstrates diffusion-controlled mass transport at the electrode surface. |
| **SWV / DPV** | Bell-shaped differential current peak | Provides high sensitivity by subtracting charging currents, isolating faradaic reaction peaks. |

---

## 9. Conclusion and Future Recommendations

The modular refactoring of the HunStat2 firmware has successfully separated concerns, creating a maintainable, extensible code base. The implementation of the Python Test Console allows rapid testing via both simulated dummy profiles and real-time board measurements.

### Recommendations for Future Work:
1. **Calibration Automation**: Implement automatic $R_{cal}$ sweeps before each EIS run to correct for ambient temperature drifts.
2. **Metadata Integration**: Include parameters (step size, amplitude, date/time) in the header of exported CSV files.
3. **Firmware Hardening**: Add watchdog timer resets in the main loop to recover from communication hangs during long OCP monitoring sessions.
4. **Remove dead code**: Delete `src/command_processing/` and the free-function half of `electrochemical_methods.cpp` (§5's directory note, §7.5/§7.6) — they cannot be reached at runtime and only risk being edited by mistake.
5. **Fix the status LED**: `Interface_SetLed()` currently ignores its color argument and always shows white (architecture document §6) — every call site already passes the semantically-correct color, so the fix is isolated to one function.
6. **Add a working-electrode channel selector**: `C_CA`/`C_SWV`/`C_DPV`'s `ConfigDCMeasurement()` all hardcode `SWN_SE0` — there is no way to select which physical WE pin a measurement senses. This blocked conclusive dummy-cell verification of CA/SWV in §11.2 below; confirming/adding channel selection is the natural next step before trusting those two methods on real samples.
7. **Unit-label every parameter field in the Python UI**: §11.3.1's CV bug was a silent volts-vs-millivolts mismatch between the UI and firmware. The CA/SWV/DPV panels already avoid this; a quick pass over EIS/OCP for the same class of issue is cheap insurance against a repeat.

---

## 11. Update Log — Sketch Restructuring, Dummy-Cell Verification, and Python UI Fixes (2026-08-01)

This session began with a new project location, `Software/update 2 (18 Juli 26)/AD5941_25/`, that failed to compile out of the box, and ended with two confirmed-and-fixed bugs in the Python test UI after a live dummy-cell verification run. Full narrative, root-cause derivations, and a before/after table are in the standalone `UPDATE_2026-08-01.md` in this same folder; this section summarizes the parts most relevant to the technical report's existing structure.

### 11.1. Compile-Time Fixes (New Sketch Location)
* **`HunStat2.ino`'s relative includes were stale.** The file moved from one level below `src/` to sitting directly beside it, but its `#include "../src/..."` lines still carried the old `../` prefix, producing `fatal error: ../src/setup/ad5941_setup.h: No such file or directory`. Fixed by dropping the `../` prefix on all 8 local includes — see `UPDATE_2026-08-01.md` §1.1.
* **`HunStat2.ino` and `AD5941_25.ino` cannot share a sketch folder.** They are two independent, complete firmware images with ~20 identically-named top-level functions, including `setup()`/`loop()` — not complementary files. `HunStat2.ino` was relocated to its own sibling sketch folder (`Software/update 2 (18 Juli 26)/HunStat2/`). See `UPDATE_2026-08-01.md` §1.2.

### 11.2. Dummy-Cell Verification Findings
A live run against a 3-branch dummy test cell (diode-nonlinear branch, plain-resistor branch, RC branch — see `UPDATE_2026-08-01.md` §2 for the full schematic breakdown) produced flat/noisy CA and SWV traces with no discernible decay or peak. Two explanations were identified, neither of which is a firmware defect requiring a fix:
* CA's flat trace is **expected** on the RC branch: its ≈330 µs time constant is roughly 150× faster than a typical 20 Hz CA sample interval, so no decay curve can be visible at that sample rate on that branch regardless of firmware correctness.
* The lack of any peak in SWV/CA may mean **the wrong branch is being sensed**: `ConfigDCMeasurement()` in `C_CA`/`C_SWV`/`C_DPV` all hardcode `SWN_SE0` (§3.2) with no electrode-channel-selection mechanism anywhere in the firmware. If `SE0` isn't wired to the one branch capable of producing a peak (the diode branch), a flat/noisy result is the *correct* outcome of measuring the wrong node — this remains an open item (recommendation 6 above), not something fixed this session.

### 11.3. Python UI Bugs Found and Fixed

#### 11.3.1. CV Stuck at Exactly 2 Data Points (Unit Mismatch)
Every CV run produced exactly 2 data points regardless of configured parameters. Root cause: `rampTest.cpp`'s step-count formula (`AppRAMPSeqInitGen()`, §5.2) is entirely millivolt-based (`DAC12BITVOLT_1LSB ≈ 0.537 mV`), but the Python UI's CV panel defaults to volt-scale values (`0.5`, `-0.22`, `0.002`, `0.8`) — unlike CA/SWV/DPV, whose fields are already millivolt-scale. Sending these unconverted gave the firmware a `0.72` "mV" swing instead of the intended `720 mV`, collapsing `StepNumber` to `⌊0.72 / 0.537 × 2⌋ = 2`. Confirmed present on the live code path (`communication.cpp`, not the dead `command_processing.cpp` copy — see §5.1's tokenizer note and architecture document §3.4). **Fixed**: the CV parameter builder now multiplies Start/Stop/Step/Scan by `1000.0` before transmission, and field labels now show `(V)`/`(V/s)` explicitly. Full derivation in `UPDATE_2026-08-01.md` §3.1.

#### 11.3.2. DPV Plot Showed a Spurious Diagonal Line
The DPV plot displayed a steep diagonal line unrelated to the actual sweep data. Root cause: the plotting function connects points in arrival order rather than sorted by x-value, and the UI never cleared previous-run data before starting a new run — so re-running DPV without manually clicking "Clear" first joined the previous run's last point to the new run's first point with a stray line. **Fixed**: a new `_clear_mode_data()` call now runs automatically before every method execution. Full derivation in `UPDATE_2026-08-01.md` §3.2.

### 11.4. Verification
`AD5941_25.ino` compiles and uploads successfully from the new folder location; `hunstat2_test_ui.py` passes `python -m py_compile` after both fixes. The corrected CV units have not yet been re-verified end-to-end against physical hardware — see `UPDATE_2026-08-01.md` §5.2 for that follow-up recommendation.

---

## 10. Update Log — Hardware Verification Session (2026-07-18)

This session started from a practical problem — the XIAO RP2040 board's COM port disappeared from Arduino IDE after flashing a modified sketch — and ended with a live CA measurement running successfully against a physical dummy test cell, streaming real data into the Tkinter test UI. Recovering that required working through the uncommitted firmware and UI changes already sitting in the working tree; this section records what those changes were and why, as the concrete "before vs. after" backing the corrections made throughout this report.

| File | Before | After | Why |
|---|---|---|---|
| `XIAOPort.cpp` | Hardware `SPI.begin()`/`SPI.transfer()`, assuming standard XIAO RP2040 SPI0 pin roles | Bit-banged `BitBangSPI` class driving `D8`/`D9`/`D10` directly | This board's MISO/MOSI wiring is reversed from the RP2040's fixed hardware SPI0 pin-mux (§4.1, §7.4) — the hardware peripheral physically cannot be used here. |
| `src/electrochemical_methods/c_ca.cpp`, `c_swv.cpp`, `c_dpv.cpp` | `ConfigDCMeasurement()` never wrote `HpLoopCfg.HsTiaCfg`; `MeasureCurrentRaw()` used a 100-tick timeout with no `SINC2RDY`→`AFEINTC_1` routing | All three now configure the full HSTIA block and route the ready interrupt before every measurement; timeout raised to 1000 ticks | Current readings were silently wrong/zero regardless of actual cell current (§7.5, §7.6; explained in depth in the electrochemical methods document §2.1). |
| `src/communication/communication.cpp` | No command set `DPV_Step_mV` | `case '!'` in `ProcessCommand1Float` maps `!<float>` → `DPV_Step_mV` | DPV step size was previously fixed at compile time (§7.7). |
| `AD5941_25.ino` (`Calibrate_HSDAC`) | Wrote `HfOSC32MHzMode = bTRUE` into the shared `clk_cfg` used later by EIS's per-step clock logic | Uses a local `local_clk_cfg`; only re-asserts `clk_cfg.HFOSCEn` on the shared struct | A calibration pass could leave a stale clock-speed flag for the next EIS sweep (§7.8). |
| `python_ui/hunstat2_test_ui.py` | DPV builder always ran entered values through `_coerce_to_volt()` | Skips the conversion when the start/end span exceeds `5.0` (heuristic: "this is already mV") | mV-range DPV sweeps (e.g. `0`–`1400`) were being mis-scaled into a sub-2V span (§7.9). |

**New findings surfaced while documenting the above** (not part of the original uncommitted diff, found by reading the live vs. dead code paths while writing this report):
* The firmware contains a complete second, unreachable implementation of the serial protocol and CA/SWV/DPV (`src/command_processing/` and the free-function half of `electrochemical_methods.cpp`) that still has the pre-fix bugs — see §5's directory note and the architecture document §3.4/§8.
* `Interface_SetLed()` ignores its `color` argument and always displays white, so the NeoPixel status-color table (architecture document §6) does not currently reflect on the physical board despite every call site passing the correct intended color.

**Verification**: `python_ui/test_hardware_ca_swv_dpv.py` (a new, deadline-bounded round-trip script added alongside these fixes) ran CA/SWV/DPV against COM8 end-to-end and confirmed all three return well-formed, non-empty data streams terminated by their `_END` markers. A live CA run was also driven through the Tkinter UI itself (source connected to `BOARD`, not `DUMMY`) and visually confirmed plotting real streamed current-vs-time data.
