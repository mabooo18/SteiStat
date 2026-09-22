"""Host-side driver for the SteiStat / AD5941 potentiostat firmware.

Speaks the compact ASCII protocol implemented in
``AD5941_25/src/communication/communication.cpp``.  Every wait has an explicit
deadline so a misbehaving board can never hang an acquisition script.
"""

from __future__ import annotations

import math
import time
from dataclasses import dataclass, field

import serial

BAUD = 1_000_000


class SteiStat:
    def __init__(self, port="COM8", baud=BAUD, boot_delay=2.0, verbose=False):
        self.ser = serial.Serial(port, baud, timeout=0.2)
        self.verbose = verbose
        time.sleep(boot_delay)
        self.ser.reset_input_buffer()

    # ------------------------------------------------------------------ io --
    def send(self, cmd, settle=0.05):
        self.ser.write((cmd + "\n").encode())
        self.ser.flush()
        if settle:
            time.sleep(settle)

    def drain(self, window=0.3):
        """Read whatever is pending, stopping after `window` seconds of silence."""
        out, last = [], time.monotonic()
        while time.monotonic() - last < window:
            ln = self.ser.readline()
            if ln:
                s = ln.decode("utf-8", "replace").rstrip()
                if s:
                    out.append(s)
                    last = time.monotonic()
        return out

    def collect(self, timeout, quiet_stop=None, min_lines=0):
        """Collect lines until `timeout`, or until `quiet_stop` s of silence
        have passed after at least `min_lines` lines arrived."""
        out = []
        t0 = last = time.monotonic()
        while time.monotonic() - t0 < timeout:
            ln = self.ser.readline()
            if ln:
                s = ln.decode("utf-8", "replace").rstrip()
                if s:
                    out.append(s)
                    if self.verbose:
                        print("   <", s)
                    last = time.monotonic()
            elif quiet_stop and len(out) >= min_lines and (time.monotonic() - last) > quiet_stop:
                break
        return out

    def params(self):
        self.ser.reset_input_buffer()
        self.send("?")
        return self.collect(3.0, quiet_stop=0.5, min_lines=5)

    def close(self):
        self.ser.close()

    # ------------------------------------------------------------- methods --
    def ocp(self, npts=10):
        self.ser.reset_input_buffer()
        self.send(f"n {npts}")
        self.drain(0.3)
        self.send("T")
        lines = self.collect(6.0, quiet_stop=0.5, min_lines=1)
        for ln in lines:
            try:
                return float(ln.strip())
            except ValueError:
                continue
        return None

    def ca(self, voltage_mV, duration_s, sample_rate_Hz=100.0, tia_rf=3, pga_gain=1):
        """Chronoamperometry.  Returns list of (t_s, I_A)."""
        self.ser.reset_input_buffer()
        for c in (f"r {tia_rf}", f"g {pga_gain}", f"1 {voltage_mV}",
                  f"2 {duration_s}", f"3 {sample_rate_Hz}"):
            self.send(c)
        self.drain(0.3)
        self.send("A", settle=0)
        budget = duration_s * 1.8 + 12.0
        lines = self.collect(budget, quiet_stop=1.5, min_lines=3)
        return _parse_xy(lines, "CA")

    def swv(self, start_mV, end_mV, step_mV, amp_mV, freq_Hz, tia_rf=3, pga_gain=1):
        self.ser.reset_input_buffer()
        for c in (f"r {tia_rf}", f"g {pga_gain}", f"4 {start_mV}", f"5 {end_mV}",
                  f"6 {step_mV}", f"7 {amp_mV}", f"8 {freq_Hz}"):
            self.send(c)
        self.drain(0.3)
        self.send("W", settle=0)
        n = abs(end_mV - start_mV) / step_mV + 1
        budget = n * (2.0 / freq_Hz + 0.12) + 20.0
        lines = self.collect(budget, quiet_stop=2.0, min_lines=3)
        return _parse_xy(lines, "SWV")

    def dpv(self, start_mV, end_mV, step_mV, amp_mV, tia_rf=3, pga_gain=1):
        self.ser.reset_input_buffer()
        for c in (f"r {tia_rf}", f"g {pga_gain}", f"9 {start_mV}", f"0 {end_mV}",
                  f"! {step_mV}", f"# {amp_mV}"):
            self.send(c)
        self.drain(0.3)
        self.send("D", settle=0)
        n = abs(end_mV - start_mV) / step_mV + 1
        budget = n * 0.45 + 25.0
        lines = self.collect(budget, quiet_stop=2.5, min_lines=3)
        return _parse_xy(lines, "DPV")

    def cv(self, v_start_mV, v_stop_mV, estep_mV, scan_rate_mVs, cycles=2):
        """Cyclic voltammetry.  Returns list of (E_mV, I_arb)."""
        self.ser.reset_input_buffer()
        self.send(f"D {v_start_mV},{v_stop_mV},{estep_mV},{scan_rate_mVs},{cycles}")
        self.drain(0.4)
        self.send("M", settle=0)
        span = abs(v_stop_mV - v_start_mV)
        budget = cycles * 2 * span / scan_rate_mVs + 45.0
        lines = self.collect(budget, quiet_stop=4.0, min_lines=5)
        return _parse_xy(lines, "CV")

    def eis(self, f_lo, f_hi, n_freqs, rcal_ohm, amplitude_mV=None, tia_rf=None):
        """Dual-sweep (Z then RCAL) EIS.  Returns list of (f_Hz, Zre, Zim)."""
        self.ser.reset_input_buffer()
        cmds = [f"y {n_freqs}", f"c {rcal_ohm}", f"W {f_lo}", f"X {f_hi}", "@ 0"]
        if amplitude_mV is not None:
            cmds.append(f"Y {amplitude_mV}")
        if tia_rf is not None:
            cmds.append(f"r {tia_rf}")
        for c in cmds:
            self.send(c)
        self.drain(0.5)
        self.send("P", settle=0)

        # Sweep time is dominated by the low-frequency settling delays of both
        # sweeps (cell + RCAL), plus DFT integration.
        est = 2 * sum(max(1.0 / f, 0.02) + 0.6 for f in log_freqs(f_lo, f_hi, n_freqs))
        lines = self.collect(est + 180.0, quiet_stop=15.0, min_lines=n_freqs)

        pts = []
        for ln in lines:
            parts = [p for p in ln.replace(" ", "").split(",") if p]
            if len(parts) == 2:
                try:
                    pts.append((float(parts[0]), float(parts[1])))
                except ValueError:
                    pass
        freqs = log_freqs(f_lo, f_hi, n_freqs)
        return [(freqs[i], re, im) for i, (re, im) in enumerate(pts[:n_freqs])]


# --------------------------------------------------------------- helpers --
def log_freqs(f_lo, f_hi, n):
    """Reproduce the firmware's logarithmic frequency ladder.

    The firmware truncates both limits to integer millihertz before taking
    logs (FreqToLabVIEW), so mirror that here.
    """
    lo = int(round(f_lo * 1000.0)) / 1000.0
    hi = int(round(f_hi * 1000.0)) / 1000.0
    a, b = math.log10(lo), math.log10(hi)
    return [10.0 ** (a + i * (b - a) / (n - 1)) for i in range(n)]


def _parse_xy(lines, tag):
    out = []
    for ln in lines:
        if not ln.startswith(tag + ","):
            continue
        parts = [p for p in ln.split(",") if p.strip()]
        if len(parts) >= 3:
            try:
                out.append((float(parts[1]), float(parts[2])))
            except ValueError:
                pass
    return out


def save_csv(path, rows, header):
    with open(path, "w", newline="") as fh:
        fh.write(",".join(header) + "\n")
        for r in rows:
            fh.write(",".join(f"{v:.6g}" if isinstance(v, float) else str(v) for v in r) + "\n")
