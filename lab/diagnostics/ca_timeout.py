import time, serial
ser = serial.Serial("COM8", 1_000_000, timeout=0.3); time.sleep(4); ser.reset_input_buffer()
def s(c, w=0.3):
    ser.write((c+"\n").encode()); ser.flush(); time.sleep(w); ser.reset_input_buffer()
s("@ 0"); s("r 3"); s("2 0.4"); s("3 50")
for v in (-200, 200):
    s(f"1 {v}")
    ser.reset_input_buffer(); ser.write(b"A\n"); ser.flush()
    t0=time.monotonic(); rows=[]
    while time.monotonic()-t0 < 25:
        ln=ser.readline()
        if not ln:
            if rows: break
            continue
        t=ln.decode('utf-8','replace').strip()
        if t.startswith("CA,"): rows.append(t.split(","))
        if t=="CA_END": break
    to = sum(1 for r in rows if len(r)>4 and r[4]=="1")
    print(f"V={v:+5d}  n={len(rows)}  timeouts={to}/{len(rows)}")
    for r in rows[:5]: print("   ", ",".join(r))
ser.close()
