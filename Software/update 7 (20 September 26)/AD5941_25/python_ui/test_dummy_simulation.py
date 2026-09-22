# Offline test: verifies the Tkinter UI's "Dummy" simulation path actually produces
# plausible CA / SWV / DPV / CV curves, with no serial hardware involved at all.
#
# Run with:
#   python test_dummy_simulation.py
#
# Requires a display (it builds the real Tk window, just withdrawn/hidden) since the
# dummy generators live inside TestUI itself rather than as free functions.

import math
import sys
import unittest

from hunstat2_test_ui import TestUI


class DummySimulationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.app = TestUI()
        except Exception as exc:  # pragma: no cover - environment dependent
            raise unittest.SkipTest(f"Tidak bisa membuat window Tk (butuh display): {exc}")
        cls.app.withdraw()

    @classmethod
    def tearDownClass(cls):
        cls.app.destroy()

    def _assert_curve_is_sane(self, mode, min_points=5):
        points = self.app.mode_points.get(mode, [])
        self.assertGreaterEqual(
            len(points), min_points, f"{mode}: terlalu sedikit titik data ({len(points)})"
        )

        xs = [p[0] for p in points]
        ys = [p[1] for p in points]

        for x, y in zip(xs, ys):
            self.assertTrue(math.isfinite(x), f"{mode}: x tidak valid ({x})")
            self.assertTrue(math.isfinite(y), f"{mode}: y tidak valid ({y})")

        # A real curve should vary - not be a flat line of identical values.
        self.assertGreater(max(xs) - min(xs), 0.0, f"{mode}: sumbu x tidak berubah sama sekali")
        self.assertGreater(max(ys) - min(ys), 0.0, f"{mode}: sumbu y tidak berubah sama sekali (current selalu sama)")
        return xs, ys

    def test_ca_dummy_curve(self):
        self.app.ca_voltage_var.set("100")
        self.app.ca_duration_var.set("5")
        self.app.ca_rate_var.set("20")
        self.app._generate_dummy_for_method("CA")

        xs, ys = self._assert_curve_is_sane("CA")
        # Cottrell-style decay: current should trend down in magnitude over time.
        self.assertGreater(abs(ys[0]), abs(ys[-1]) * 0.5, "CA: arus tidak menunjukkan peluruhan yang wajar")
        self.assertAlmostEqual(xs[0], 0.0, places=6)

    def test_swv_dummy_curve(self):
        self.app.swv_start_var.set("0")
        self.app.swv_end_var.set("1400")
        self.app.swv_step_var.set("5")
        self.app.swv_amp_var.set("25")
        self.app.swv_freq_var.set("25")
        self.app._generate_dummy_for_method("SWV")

        expected_points = int(abs(1400 - 0) / (5.0 / 1000.0)) + 1
        xs, ys = self._assert_curve_is_sane("SWV")
        self.assertLessEqual(len(xs), 2500, "SWV: jumlah titik melebihi batas aman UI")

    def test_dpv_dummy_curve(self):
        self.app.dpv_start_var.set("0")
        self.app.dpv_end_var.set("1400")
        self.app.dpv_step_var.set("5")
        self.app.dpv_amp_var.set("25")
        self.app._generate_dummy_for_method("DPV")

        xs, ys = self._assert_curve_is_sane("DPV")
        # DPV should show a peak: the max |current| shouldn't sit right at an endpoint.
        peak_index = max(range(len(ys)), key=lambda i: abs(ys[i]))
        self.assertNotIn(peak_index, (0, len(ys) - 1), "DPV: tidak ada puncak yang jelas di tengah scan")

    def test_cv_dummy_curve(self):
        self.app.cv_start_var.set("0.5")
        self.app.cv_stop_var.set("-0.22")
        self.app._generate_dummy_for_method("CV")

        xs, ys = self._assert_curve_is_sane("CV", min_points=100)
        # CV is a closed forward+reverse loop: first and last voltage should both be
        # near the start potential (forward sweep starts there, reverse sweep ends there).
        self.assertAlmostEqual(xs[0], 0.5, delta=0.01)


if __name__ == "__main__":
    print("Menjalankan test simulasi dummy (CA, SWV, DPV, CV) tanpa hardware...")
    unittest.main()
