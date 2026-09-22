import serial, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM8"
BAUD = 1_000_000

ser = serial.Serial(PORT, BAUD, timeout=0.3)
time.sleep(2.0)
ser.reset_input_buffer()

def send(cmd, wait=1.5):
    ser.write((cmd + "\n").encode())
    ser.flush()
    out, t0 = [], time.monotonic()
    while time.monotonic() - t0 < wait:
        ln = ser.readline()
        if not ln:
            continue
        out.append(ln.decode("utf-8", "replace").rstrip())
    return out

print("--- banner/idle ---")
for l in send("", 1.0): print(l)
print("--- '?' show parameters ---")
for l in send("?", 3.0): print(l)
ser.close()
