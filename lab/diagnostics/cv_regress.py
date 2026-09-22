import numpy as np
from hunstat import HunStat
hs = HunStat("COM8", boot_delay=4.0)
hs.send("@ 0"); hs.drain(0.3)
hs.ser.reset_input_buffer()
hs.send("D -200,200,5,200,1"); hs.drain(0.4); hs.send("M", settle=0)
L = hs.collect(120, quiet_stop=4.0, min_lines=5)
hs.close()
print("RTIACAL:", [l for l in L if l.startswith("RTIACAL")])
pts=[(float(p[1]),float(p[2])) for p in (l.split(",") for l in L if l.startswith("CV,")) if len(p)>=3]
v=np.array([p[0] for p in pts]); i=np.array([p[1] for p in pts])
m,b=np.polyfit(v,i,1); r2=1-np.sum((i-(m*v+b))**2)/np.sum((i-i.mean())**2)
print(f"n={len(pts)}  R={1e3/m:.1f} ohm  err={100*(1e3/m-10560)/10560:+.2f}%  R2={r2:.7f}")
