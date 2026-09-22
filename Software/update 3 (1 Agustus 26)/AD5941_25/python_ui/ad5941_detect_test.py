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


KNOWN_ID_REGISTERS = {
    0x0400: "ADIID",
    0x0404: "CHIPID",
    0x0408: "CLKCON0",
    0x0410: "CLKEN1",
}

KNOWN_CHIP_IDS = {0x5500, 0x5501, 0x5502}
READBACK_RE = re.compile(r"I\s+0x([0-9A-Fa-f]+)=0x([0-9A-Fa-f]+)")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Test koneksi AD5941 lewat firmware HunStat2 dengan membaca register SPI.",
    )
    parser.add_argument("port", nargs="?", help="Port serial, misalnya COM8")
    parser.add_argument("--baud", type=int, default=1_000_000, help="Baud rate firmware (default: 1000000)")
    parser.add_argument(
        "--register",
        dest="registers",
        action="append",
        default=[],
        help="Register tambahan untuk dibaca, contoh: --register 0x420",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=0.35,
        help="Timeout baca serial per command dalam detik (default: 0.35)",
    )
    parser.add_argument(
        "--startup-delay",
        type=float,
        default=2.0,
        help="Jeda setelah membuka port agar firmware siap menerima command (default: 2.0 detik)",
    )
    return parser.parse_args()


def parse_register(value):
    text = value.strip().lower()
    return int(text, 16) if text.startswith("0x") else int(text)


def detect_default_port():
    if serial is None:
        return None, []

    ports = [port.device for port in serial.tools.list_ports.comports()]
    if not ports:
        return None, []
    if len(ports) == 1:
        return ports[0], ports
    return None, ports


def collect_lines(ser, window_seconds):
    deadline = time.monotonic() + max(window_seconds, 0.05)
    lines = []
    while time.monotonic() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            lines.append(line)
    return lines


def send_command(ser, command, window_seconds):
    ser.write((command.rstrip("\r\n") + "\n").encode("utf-8"))
    ser.flush()
    return collect_lines(ser, window_seconds)


def extract_register_value(lines, address):
    expected = f"0x{address:X}"
    for line in lines:
        match = READBACK_RE.search(line)
        if not match:
            continue
        if f"0x{int(match.group(1), 16):X}" != expected:
            continue
        return int(match.group(2), 16)
    return None


def main():
    if serial is None:
        print(f"pyserial belum tersedia: {SERIAL_IMPORT_ERROR}", file=sys.stderr)
        return 2

    args = parse_args()
    extra_registers = [parse_register(item) for item in args.registers]
    register_list = list(KNOWN_ID_REGISTERS)
    for address in extra_registers:
        if address not in register_list:
            register_list.append(address)

    selected_port = args.port
    available_ports = []
    if not selected_port:
        selected_port, available_ports = detect_default_port()
        if selected_port is None:
            if available_ports:
                print(
                    "Terdeteksi lebih dari satu port serial. Pilih port secara eksplisit, misalnya COM8.",
                    file=sys.stderr,
                )
                print(f"Port tersedia: {', '.join(available_ports)}", file=sys.stderr)
            else:
                print("Tidak ada port serial yang terdeteksi.", file=sys.stderr)
            print("Jalankan misalnya: python .\\ad5941_detect_test.py COM8", file=sys.stderr)
            return 2

    print("AD5941 detect test")
    print("Catatan: AD5941 memakai SPI, jadi tidak punya alamat device seperti I2C.")
    print("Yang dicek di sini adalah alamat register SPI, terutama ADIID dan CHIPID.")
    print(f"Port: {selected_port} | Baud: {args.baud}")
    if available_ports:
        print(f"Auto-detect port tersedia: {', '.join(available_ports)}")

    try:
        with serial.Serial(selected_port, args.baud, timeout=args.timeout) as ser:
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            if args.startup_delay > 0:
                print(f"\n[0] Tunggu firmware siap: {args.startup_delay:.1f} detik")
                time.sleep(args.startup_delay)
                boot_lines = collect_lines(ser, min(args.timeout, 0.25))
                for line in boot_lines:
                    print(f"  {line}")

            print("\n[0.5] Probe parser dengan command ?")
            probe_lines = send_command(ser, "?", max(args.timeout, 0.6))
            if probe_lines:
                for line in probe_lines[:12]:
                    print(f"  {line}")
                if len(probe_lines) > 12:
                    print(f"  ... {len(probe_lines) - 12} baris lain")
            else:
                print("  Tidak ada balasan untuk command ?")

            print("\n[1] Init ulang AD5941 dengan command Z")
            init_lines = send_command(ser, "Z", max(args.timeout, 0.4))
            for line in init_lines:
                print(f"  {line}")

            print("\n[2] Baca register penting")
            read_results = {}
            for address in register_list:
                lines = send_command(ser, f"I 0x{address:X}", args.timeout)
                value = extract_register_value(lines, address)
                label = KNOWN_ID_REGISTERS.get(address, "REG")
                if value is None:
                    print(f"  {label:<8} 0x{address:04X} -> tidak ada balasan yang cocok")
                    for line in lines:
                        print(f"    {line}")
                    continue

                read_results[address] = value
                print(f"  {label:<8} 0x{address:04X} -> 0x{value:04X}")

            print("\n[3] Ringkasan")
            chip_id = read_results.get(0x0404)
            adiid = read_results.get(0x0400)

            if chip_id in KNOWN_CHIP_IDS:
                print(f"  CHIPID valid: 0x{chip_id:04X}. AD5941/AD5940 terdeteksi.")
            elif chip_id is None:
                print("  CHIPID belum terbaca. Komunikasi SPI/firmware kemungkinan masih bermasalah.")
            else:
                print(f"  CHIPID terbaca tapi tidak dikenal: 0x{chip_id:04X}.")

            if adiid is not None:
                print(f"  ADIID terbaca: 0x{adiid:04X}")

            if not read_results:
                print("  Tidak ada register yang berhasil dibaca.")
                return 1

            if chip_id not in KNOWN_CHIP_IDS:
                return 1

            return 0
    except KeyboardInterrupt:
        print("\nDibatalkan pengguna.")
        return 130
    except Exception as exc:
        print(f"Gagal membuka atau membaca serial: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())