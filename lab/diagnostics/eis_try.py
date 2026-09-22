import cmath, time
from hunstat import HunStat, log_freqs

F_LO, F_HI, N, RCAL = 100.0, 100000.0, 15, 200.0
hs = HunStat("COM8", boot_delay=4.0)
for c in ("@ 1", "S 1", f"y {N}", f"c {RCAL}", f"W {F_LO}", f"X {F_HI}"):
    hs.send(c)
hs.drain(0.5); hs.ser.reset_input_buffer()
t0 = time.monotonic()
hs.send("P", settle=0)
lines = hs.collect(600, quiet_stop=45.0, min_lines=N)
hs.close()
print(f"elapsed {time.monotonic()-t0:.1f}s, {len(lines)} lines")
print("timeouts:", sum(1 for l in lines if "TIMEOUT" in l))
pts = []
for l in lines:
    p = [x for x in l.replace(" ", "").split(",") if x]
    if len(p) == 2:
        try: pts.append((float(p[0]), float(p[1])))
        except ValueError: pass
print("RAW LINES:");
for l in lines: print("  ", l)
print(f"nyquist points: {len(pts)}")
fr = log_freqs(F_LO, F_HI, N)
for k, (re, im) in enumerate(pts[:N]):
    z = complex(re, im)
    print(f"  {fr[k]:10.1f} Hz  Zre={re:11.1f}  Zim={im:11.1f}  |Z|={abs(z):10.1f}  "
          f"ph={cmath.phase(z)*180/cmath.pi:7.2f}")
