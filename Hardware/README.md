This folder contains the full KiCad hardware design source for the Steistat
potentiostat carrier board (`STEI_Bstat_v1.2`), consolidated from a previous
separate export folder.

- `STEI_Bstat_v1.2.kicad_sch` / `.kicad_pcb` / `.kicad_pro` / `.kicad_prl` /
  `.kicad_dru` / `.net` — the KiCad project itself (schematic, PCB layout,
  project settings, design rules, netlist). Open `STEI_Bstat_v1.2.kicad_pro`
  in KiCad to edit.
- `Production/bom-1.csv` and `Production/positions.csv` — the current,
  machine-generated Bill of Materials and pick-and-place file, exported
  directly from the PCB above. This is the BOM used in the project's
  Elsevier/HardwareX manuscript (`journal/paper4/`).
- `Production/STEI_Bstat_v1.2.zip` — a fabrication-ready export bundle.
- `Gerber_PCB.zip` — Gerber/drill files for ordering the bare PCB from any
  manufacturer.
- `Schematic_AD5941 corr. circ. diag._2025-12-14.pdf` — a rendered PDF of
  the schematic, for reference without opening KiCad.
- `DFM analysis report_JLCDFM_STEI_Bstat_v1.2.pdf` — a design-for-manufacture
  check report.
- `Connector_PinHeader_2.54mm_NoGND.pretty/`, `fp-lib-table`,
  `fabrication-toolkit-options.json` — supporting KiCad footprint library
  and fabrication-toolkit plugin files the project depends on.

Using the Gerbers together with the BOM and pick-and-place files, the board
can be ordered as a fully assembled and pre-soldered unit from most PCB
manufacturing services (see `journal/paper4/sections/6-build-instructions.tex`
for the full build procedure).

## `legacy/`

Older exported files kept for reference, superseded by the files above:

- `HunSta2_BOM_corr_OUTDATED.xlsx` — an earlier Bill of Materials export
  whose component values **do not match** the current schematic/PCB (e.g.
  it predates the `RCAL1`/`RCAL2`/`RCAL3` calibration-resistor bank and the
  Randles-branch values used throughout the paper). Use `Production/bom-1.csv`
  instead.
- `SteiStat_CPL.xlsx` — an earlier pick-and-place export in spreadsheet
  form; superseded by `Production/positions.csv`.
