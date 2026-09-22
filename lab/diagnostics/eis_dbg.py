import time, serial
ser = serial.Serial("COM8", 1_000_000, timeout=0.2)
time.sleep(2); ser.reset_input_buffer()

def s(c, w=0.25):
    ser.write((c+"\n").encode()); ser.flush(); time.sleep(w)
    out=[]
    while True:
        ln=ser.readline()
        if not ln: break
        out.append(ln.decode('utf-8','replace').rstrip())
    return out

for c in ["@ 1", "S 1", "y 5", "c 10000", "W 100", "X 100000"]:
    print(c, "->", s(c))
print("params:", s("?", 1.0))
print("\n--- running P ---")
ser.write(b"P\n"); ser.flush()
t0=time.monotonic(); last=t0
while time.monotonic()-t0 < 420:
    ln = ser.readline()
    if ln:
        print(f"[{time.monotonic()-t0:6.2f}]", ln.decode('utf-8','replace').rstrip())
        last=time.monotonic()
    elif time.monotonic()-last > 300:
        print("(300s silence, stopping)"); break
ser.close()
