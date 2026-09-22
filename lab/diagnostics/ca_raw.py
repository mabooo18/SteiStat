import time, serial
ser = serial.Serial("COM8", 1_000_000, timeout=0.3); time.sleep(4); ser.reset_input_buffer()
def s(c, w=0.3):
    ser.write((c+"\n").encode()); ser.flush(); time.sleep(w)
    out=[]
    while True:
        ln=ser.readline()
        if not ln: break
        out.append(ln.decode('utf-8','replace').rstrip())
    return out
s("@ 0"); s("r 3"); s("2 0.3"); s("3 50")
for v in (-200, 0, 200):
    s(f"1 {v}")
    ser.reset_input_buffer()
    ser.write(b"A\n"); ser.flush()
    t0=time.monotonic(); raws=[]
    while time.monotonic()-t0 < 20:
        ln=ser.readline()
        if not ln:
            if raws: break
            continue
        t=ln.decode('utf-8','replace').strip()
        if t.startswith("CA,"):
            raws.append(t.split(",")[-1])
        if t == "CA_END": break
    uniq = sorted(set(raws))
    print(f"V={v:+5d} mV  n={len(raws):3d}  first={raws[0] if raws else '-'}  "
          f"distinct={len(uniq)}  {uniq[:4]}")
ser.close()
