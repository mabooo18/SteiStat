# Offline unit test script to verify parser compatibility between firmware outputs and Python UI.

import unittest
import re

class SerialDataParser:
    """
    Extracted parsing logic from hunstat2_test_ui.py to verify compatibility
    with new refactored firmware output streams.
    """
    def parse_measurement(self, line):
        upper = line.upper()
        # 1. Matches generic tagged formats: MODE,x,y
        if upper.startswith(("OCP,", "EIS,", "CV,", "CA,", "SWV,", "DPV,")):
            parts = [part.strip() for part in line.split(",")]
            if len(parts) < 3:
                return None
            mode = parts[0].upper()
            try:
                x_val = float(parts[1])
                y_val = float(parts[2])
            except ValueError:
                return None
            return mode, x_val, y_val

        # 2. Matches legacy CV formats: CV:x,y or CV,x,y
        if upper.startswith("CV:"):
            parsed_cv = self.parse_cv_pair(line)
            if parsed_cv is not None:
                x_val, y_val = parsed_cv
                return "CV", x_val, y_val

        # 3. Matches SeeedStat EIS raw format: index=real,imag,
        match_raw = re.match(r"^\s*\d+\s*=\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*,\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*,?\s*$", line)
        if match_raw:
            real = float(match_raw.group(1))
            imag = float(match_raw.group(2))
            return "EIS_RAW", real, imag

        # 4. Matches SeeedStat Nyquist output format: x,y,
        match_nyq = re.match(r"^\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*,\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*,?\s*$", line)
        if match_nyq:
            x_val = float(match_nyq.group(1))
            y_val = float(match_nyq.group(2))
            return "NYQUIST", x_val, y_val

        return None

    def parse_cv_pair(self, line):
        cleaned = line.strip()
        cleaned_upper = cleaned.upper()
        if cleaned_upper.startswith("CV,"):
            cleaned = cleaned[3:]
        elif cleaned_upper.startswith("CV:"):
            cleaned = cleaned[3:]

        parts = re.split(r"[,;\t ]+", cleaned)
        parts = [part for part in parts if part]
        if len(parts) < 2:
            return None

        try:
            x_val = float(parts[0])
            y_val = float(parts[1])
        except ValueError:
            return None

        return x_val, y_val


class TestFirmwareOutputParser(unittest.TestCase):
    def setUp(self):
        self.parser = SerialDataParser()

    def test_chronoamperometry_output(self):
        # Format produced by c_ca.cpp: "CA,%.4f,%.4e"
        line = "CA,0.0100,1.2345e-05"
        result = self.parser.parse_measurement(line)
        self.assertIsNotNone(result)
        mode, x, y = result
        self.assertEqual(mode, "CA")
        self.assertAlmostEqual(x, 0.01)
        self.assertAlmostEqual(y, 1.2345e-05)

    def test_square_wave_voltammetry_output(self):
        # Format produced by c_swv.cpp: "SWV,%.2f,%.4e"
        line = "SWV,-50.00,-4.5670e-06"
        result = self.parser.parse_measurement(line)
        self.assertIsNotNone(result)
        mode, x, y = result
        self.assertEqual(mode, "SWV")
        self.assertAlmostEqual(x, -50.0)
        self.assertAlmostEqual(y, -4.567e-06)

    def test_differential_pulse_voltammetry_output(self):
        # Format produced by c_dpv.cpp: "DPV,%.2f,%.4e"
        line = "DPV,250.00,8.9100e-07"
        result = self.parser.parse_measurement(line)
        self.assertIsNotNone(result)
        mode, x, y = result
        self.assertEqual(mode, "DPV")
        self.assertAlmostEqual(x, 250.0)
        self.assertAlmostEqual(y, 8.91e-07)

    def test_eis_raw_output(self):
        # Format produced by c_eis.cpp in SeeedStat mode: "i=real,imag,"
        line = "1=1200.5,340.2,"
        result = self.parser.parse_measurement(line)
        self.assertIsNotNone(result)
        mode, x, y = result
        self.assertEqual(mode, "EIS_RAW")
        self.assertAlmostEqual(x, 1200.5)
        self.assertAlmostEqual(y, 340.2)

    def test_nyquist_output(self):
        # Format produced by c_eis.cpp: "%.3f,%.3f,"
        line = "15420.300,-5230.150,"
        result = self.parser.parse_measurement(line)
        self.assertIsNotNone(result)
        mode, x, y = result
        self.assertEqual(mode, "NYQUIST")
        self.assertAlmostEqual(x, 15420.3)
        self.assertAlmostEqual(y, -5230.15)

    def test_cv_step_output(self):
        # Format produced by cv.cpp (legacy parser fallback): "%.4f,%.4f,"
        line = "250.0000,-0.0045,"
        # This matches the raw double-coordinate float matcher
        result = self.parser.parse_measurement(line)
        self.assertIsNotNone(result)
        mode, x, y = result
        self.assertEqual(mode, "NYQUIST") # Matches general x,y regex since no tag is present
        self.assertAlmostEqual(x, 250.0)
        self.assertAlmostEqual(y, -0.0045)


if __name__ == "__main__":
    print("Menjalankan unit test untuk parser output serial...")
    unittest.main()
