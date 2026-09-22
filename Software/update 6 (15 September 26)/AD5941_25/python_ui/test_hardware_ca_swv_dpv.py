# Hardware round-trip diagnostic for CA / SWV / DPV.
#
# Unlike the Tkinter UI, this script NEVER blocks indefinitely: every wait has an
# explicit deadline, so it always finishes and prints a clear diagnosis of exactly
# where things break down (port won't open, board never boots, board boots but
# ignores commands, board replies but data is malformed, etc).
#
# Run with:
#   python test_hardware_ca_swv_dpv.py COM8
#   python test_hardware_ca_swv_dpv.py            (auto-detects if exactly one port exists)
#
# Uses short/fast parameters (short CA duration, few SWV/DPV steps) so a full run
# takes a few seconds, not minutes.

import argparse
import re
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except Exception as exc:  # pragma: no cover - runtime dependency
    serial = None
    SERIAL_IMPORT_ERROR = exc
else:
    SERIAL_IMPORT_ERROR = None


CA_LINE_RE = re.compile(r"^CA,([-+]?\d*\.?\d+),([-+]?\d*\.?\d+[eE][-+]?\d+)$")
SWV_LINE_RE = re.compile(r"^SWV,([-+]?\d*\.?\d+),([-+]?\d*\.?\d+[eE][-+]?\d+)$")
DPV_LINE_RE = re.compile(r"^DPV,([-+]?\d*\.?\d+),([-+]?\d*\.?\d+[eE][-+]?\d+)$")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Test round-trip CA/SWV/DPV lewat firmware HunStat2 tanpa pernah hang.",
    )
    parser.add_argument("port", nargs="?", help="Port serial, misalnya COM8")
    parser.add_argument("--baud", type=int, default=1_000_000, help="Baud rate firmware (default: 1000000)")
    parser.add_argument("--startup-delay", type=float, default=2.0, help="Jeda setelah membuka port (detik)")
    parser.add_argument("--probe-timeout", type=float, default=3.0, help="Timeout tunggu balasan command singkat (detik)")
    parser.add_argument("--run-timeout", type=float, default=15.0, help="Timeout maksimum tunggu satu metode selesai (detik)")
    return parser.parse_args()


def detect_default_port():
    if serial is None:
        return None, []
    ports = [port.device for port in serial.tools.list_ports.comports()]
    if not ports:
        return None, []
    if len(ports) == 1:
        return ports[0], ports
    return None, ports


def collect_lines_until(ser, deadline, stop_markers=()):
    """Read lines until a stop marker is seen or the wall-clock deadline passes.
    Always returns - never blocks past `deadline`, regardless of what the board does."""
    lines = []
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if not line:
            continue
        lines.append(line)
        if any(line == marker for marker in stop_markers):
            break
    return lines


def send(ser, command):
    ser.write((command.rstrip("\r\n") + "\n").encode("utf-8"))
    ser.flush()


def run_method(ser, label, setup_cmd, run_cmd, end_marker, line_re, run_timeout):
    print(f"\n[{label}] Mengirim parameter: {setup_cmd}")
    send(ser, setup_cmd)
    # Drain any parameter echo quickly; do not require it (verbose may be off).
    collect_lines_until(ser, time.monotonic() + 0.3)

    print(f"[{label}] Menjalankan: {run_cmd}")
    start = time.monotonic()
    send(ser, run_cmd)
    lines = collect_lines_until(ser, start + run_timeout, stop_markers=(end_marker,))
    elapsed = time.monotonic() - start

    if not lines:
        print(f"[{label}] GAGAL: tidak ada balasan sama sekali dalam {run_timeout:.1f}s.")
        print(f"           -> Board mungkin tidak menerima command '{run_cmd}', atau macet di rutin {label}.")
        return False

    data_lines = [ln for ln in lines if line_re.match(ln)]
    saw_start = any(ln.endswith("_START") for ln in lines)
    saw_end = lines[-1] == end_marker

    print(f"[{label}] Diterima {len(lines)} baris ({len(data_lines)} baris data valid) dalam {elapsed:.2f}s")
    if not saw_start:
        print(f"[{label}] PERINGATAN: tidak melihat penanda '_START' - cek apakah firmware yang berjalan cocok dengan source ini.")
    if not saw_end:
        print(f"[{label}] PERINGATAN: tidak melihat penanda akhir '{end_marker}' sebelum timeout - run mungkin terpotong.")
    if not data_lines:
        print(f"[{label}] GAGAL: tidak ada baris data yang cocok format '{label},<x>,<y>'.")
        print(f"           Contoh baris mentah yang diterima: {lines[:5]}")
        return False

    print(f"[{label}] OK: contoh data -> {data_lines[0]} ... {data_lines[-1]}")
    return True


def main():
    if serial is None:
        print(f"pyserial belum tersedia: {SERIAL_IMPORT_ERROR}", file=sys.stderr)
        return 2

    args = parse_args()

    selected_port = args.port
    available_ports = []
    if not selected_port:
        selected_port, available_ports = detect_default_port()
        if selected_port is None:
            if available_ports:
                print("Terdeteksi lebih dari satu port. Pilih eksplisit, misalnya COM8.", file=sys.stderr)
                print(f"Port tersedia: {', '.join(available_ports)}", file=sys.stderr)
            else:
                print("Tidak ada port serial yang terdeteksi.", file=sys.stderr)
            return 2

    print("HunStat2 CA/SWV/DPV round-trip test")
    print(f"Port: {selected_port} | Baud: {args.baud}")
    print("Catatan: script ini punya batas waktu di setiap langkah, jadi TIDAK AKAN pernah nge-hang.")

    try:
        with serial.Serial(selected_port, args.baud, timeout=0.2) as ser:
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            print(f"\n[0] Menunggu boot firmware ({args.startup_delay:.1f}s)...")
            boot_lines = collect_lines_until(ser, time.monotonic() + args.startup_delay)
            if boot_lines:
                print(f"    Board mengirim {len(boot_lines)} baris saat boot:")
                for line in boot_lines[:10]:
                    print(f"      {line}")
            else:
                print("    (tidak ada output saat boot - ini normal jika firmware diam sampai ada command)")

            print("\n[1] Probe dasar: kirim '?' dan tunggu balasan...")
            send(ser, "?")
            probe_lines = collect_lines_until(ser, time.monotonic() + args.probe_timeout)
            if not probe_lines:
                print(f"    GAGAL: board tidak membalas '?' sama sekali dalam {args.probe_timeout:.1f}s.")
                print("    Kemungkinan penyebab, cek satu per satu:")
                print("      1. Port/baud salah (pastikan sama dengan Serial.begin() di firmware, 1000000).")
                print("      2. Firmware yang ter-flash bukan versi source ini (misal masih HunStat2.ino.ino lama).")
                print("      3. Board sedang macet di inisialisasi AD5941 (cek LED status di board).")
                print("      4. Kabel USB cuma charging-only (tidak ada jalur data).")
                return 1
            print(f"    OK: dapat {len(probe_lines)} baris balasan, contoh: {probe_lines[0]}")

            print("\n[2] Re-init board dengan 'Z' sebelum test...")
            send(ser, "Z")
            collect_lines_until(ser, time.monotonic() + 1.0)

            # Short/fast test parameters. Built the same way the real UI builds them
            # (";"-joined "<command-char><value>" tokens) rather than hand-typed
            # strings, so there's no risk of ambiguous digit concatenation.
            ca_params = ";".join([f"1{100}", f"2{2}", f"3{40}"])          # 100mV, 2s @ 40Hz
            swv_params = ";".join([f"4{0}", f"5{200}", f"6{20}", f"7{25}", f"8{25}"])   # 0->200mV step 20mV
            dpv_params = ";".join([f"9{0}", f"0{200}", f"!{20}", f"#{25}"])             # 0->200mV step 20mV

            results = {}
            results["CA"] = run_method(
                ser, "CA",
                setup_cmd=ca_params,
                run_cmd="A",
                end_marker="CA_END",
                line_re=CA_LINE_RE,
                run_timeout=args.run_timeout,
            )
            results["SWV"] = run_method(
                ser, "SWV",
                setup_cmd=swv_params,
                run_cmd="W",
                end_marker="SWV_END",
                line_re=SWV_LINE_RE,
                run_timeout=args.run_timeout,
            )
            results["DPV"] = run_method(
                ser, "DPV",
                setup_cmd=dpv_params,
                run_cmd="D",
                end_marker="DPV_END",
                line_re=DPV_LINE_RE,
                run_timeout=args.run_timeout,
            )

            print("\n[3] Ringkasan")
            for method, ok in results.items():
                print(f"    {method}: {'OK' if ok else 'GAGAL'}")

            return 0 if all(results.values()) else 1

    except serial.SerialException as exc:
        print(f"Gagal membuka port serial: {exc}", file=sys.stderr)
        print("Cek: port benar, tidak sedang dipakai program lain (mis. Arduino Serial Monitor / UI Tkinter).", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nDibatalkan pengguna.")
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
