"""Static sanity checks on the manuscript sources (no LaTeX toolchain needed)."""

import collections
import glob
import os
import re

PAPER = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     "..", "journal", "paper"))
os.chdir(PAPER)

ok = True
files = sorted(glob.glob("sections/*.tex")) + ["main.tex"]

for f in files:
    s = open(f, encoding="utf-8").read()

    begins = re.findall(r"\\begin\{([A-Za-z]+\*?)\}", s)
    ends = re.findall(r"\\end\{([A-Za-z]+\*?)\}", s)
    cb, ce = collections.Counter(begins), collections.Counter(ends)
    for env in set(cb) | set(ce):
        if cb[env] != ce[env]:
            print(f"  MISMATCH {f}: {env} begin={cb[env]} end={ce[env]}")
            ok = False

    depth = 0
    for i, ch in enumerate(s):
        esc = i > 0 and s[i - 1] == "\\"
        if ch == "{" and not esc:
            depth += 1
        elif ch == "}" and not esc:
            depth -= 1
        if depth < 0:
            print(f"  NEGATIVE BRACE DEPTH {f} at offset {i}")
            ok = False
            break
    if depth != 0:
        print(f"  UNBALANCED BRACES {f}: net {depth}")
        ok = False

    # math-mode $ parity (ignore \$)
    dollars = len(re.findall(r"(?<!\\)\$", s))
    if dollars % 2:
        print(f"  ODD NUMBER OF $ in {f}: {dollars}")
        ok = False

    # tabular row width vs column spec
    for m in re.finditer(r"\\begin\{tabular\}\{([^\n]*?)\}\s*\n(.*?)\\end\{tabular\}",
                         s, re.S):
        spec, body = m.group(1), m.group(2)
        clean = re.sub(r">\{[^}]*\}", "", spec)
        clean = re.sub(r"@\{[^}]*\}", "", clean)
        ncol = len(re.findall(r"[lcr]|p\{[^}]*\}", clean))
        for raw in body.split("\\\\"):
            ln = raw.strip()
            if not ln or "&" not in ln:
                continue
            if "multicolumn" in ln or ln.startswith("%"):
                continue
            n = ln.count("&") + 1
            if n != ncol:
                print(f"  {f}: row has {n} cells, spec implies {ncol}")
                print(f"      {ln[:80]}")
                ok = False

print("STRUCTURE OK" if ok else "--- issues above ---")
