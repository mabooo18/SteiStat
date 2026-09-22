import statistics
from steistat import SteiStat

RF = {0:"OPEN",1:"SHORT",2:"20K",3:"100K",4:"200K",5:"400K",6:"600K",7:"1M"}
R_NOM = 10560.0
hs = SteiStat("COM8", boot_delay=4.0)
hs.send("@ 0"); hs.drain(0.3)
print(f"{'Rf':>6} {'SW13':>5} | {'I(-200mV)':>12} {'I(+200mV)':>12} {'delta':>12} "
      f"{'R_from_delta':>13}")
for rf in (1, 2, 3, 7):
    for sw13 in (0, 1):
        hs.send(f"k {(rf<<1)|sw13}"); hs.drain(0.25)
        res = {}
        for v in (-200, 200):
            pts = hs.ca(v, 0.4, 100.0, tia_rf=3)
            tail = [i for t, i in pts if t > 0.25]
            res[v] = statistics.mean(tail) if tail else float('nan')
        d = res[200] - res[-200]
        R = (0.4 / d) if d else float('nan')
        print(f"{RF[rf]:>6} {sw13:>5} | {res[-200]:+12.4e} {res[200]:+12.4e} {d:+12.4e} "
              f"{R:13.1f}")
hs.close()
print(f"\nexpected delta for 400 mV across {R_NOM} ohm: {0.4/R_NOM:.4e} A")
