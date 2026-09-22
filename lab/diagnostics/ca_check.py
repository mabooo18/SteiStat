import statistics
from steistat import SteiStat

R_NOM = 10560.0
hs = SteiStat("COM8", boot_delay=4.0)
hs.send("@ 0"); hs.drain(0.3)
print(f"{'step mV':>8} {'I_ss (A)':>13} {'expected':>13} {'err':>9} {'R_implied':>11}")
for v in (-200, -100, -50, 50, 100, 200):
    pts = hs.ca(v, 0.6, 100.0, tia_rf=3)
    if not pts: print(f"{v:8.0f}   no data"); continue
    tail = [i for t, i in pts if t > 0.3]
    I = statistics.mean(tail); exp = v/1000.0/R_NOM
    R = (v/1000.0/I) if I else float('nan')
    print(f"{v:8.0f} {I:+13.4e} {exp:+13.4e} {100*(I-exp)/abs(exp):+8.2f}% {R:11.1f}")
hs.close()
