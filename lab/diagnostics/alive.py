import time, serial
ser = serial.Serial("COM8", 1_000_000, timeout=0.3)
time.sleep(1.5); ser.reset_input_buffer()
ser.write(b"?\n"); ser.flush()
t0=time.monotonic(); got=[]
while time.monotonic()-t0 < 4:
    ln=ser.readline()
    if ln: got.append(ln.decode('utf-8','replace').rstrip())
print("RESPONSIVE" if got else "HUNG (no reply to '?')")
for g in got[:6]: print("  ", g)
ser.close()
