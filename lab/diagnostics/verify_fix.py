import numpy as np, time, serial
from hunstat import HunStat

hs = HunStat("COM8", boot_delay=4.0, verbose=False)
print("params rcal:", [l for l in hs.params() if "cal" in l.lower()])

hs.send("@ 0"); hs.drain(0.3)
raw = []
hs.ser.reset_input_buffer()
hs.send(f"D -200,200,5,200,1"); hs.drain(0.4)
hs.send("M", settle=0)
lines = hs.collect(120, quiet_stop=4.0, min_lines=5)
hs.close()

print("RTIACAL lines:", [l for l in lines if l.startswith("RTIACAL")])
pts = [(float(p[1]), float(p[2])) for p in
       (l.split(",") for l in lines if l.startswith("CV,")) if len(p) >= 3]
print(f"n = {len(pts)}")
v = np.array([p[0] for p in pts]); i = np.array([p[1] for p in pts])
m, b = np.polyfit(v, i, 1)
r2 = 1 - np.sum((i-(m*v+b))**2)/np.sum((i-i.mean())**2)
print(f"slope = {m:.6g} uA/mV   ->  implied R = {1e3/m:.1f} ohm")
print(f"R2 = {r2:.7f}   intercept = {b:+.5f} uA")
print(f"I range: {i.min():.3f} .. {i.max():.3f} uA")
