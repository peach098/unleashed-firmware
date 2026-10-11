"""Per-step DWT ([PROBE] lines of latency_probe_v2) + PM3 SOF table, median of the samples.
usage: python3 dwt_table.py <dir> <label> [<label> ...]   (reads <label>_probe_log.txt, <label>_pm3_sof_delay.txt)
All builds have tx_prepare, so encode = prepared - tx_enter. 1 us = 64 cycles."""
import re, sys, os, statistics as st
D, LABELS = sys.argv[1], sys.argv[2:]
RE = re.compile(r"\[PROBE\] (\d+)B eof2rx=(\d+) tx_enter=(\d+) prepared=(\d+) fdt_end=(\d+) seq_tx=(\d+) dma=(\d+) \(fdt_after_prep=(\d)\)")
NAMES = {7: ("rd1", 1), 35: ("rdm8", 8), 131: ("rdm32", 32), 259: ("rdm64", 64), 323: ("rdm80", 80)}
us = lambda c: c / 64.0
summary = {}
for L in LABELS:
    rows = {}
    for m in RE.finditer(open(f"{D}/{L}_probe_log.txt", errors="replace").read()):
        v = list(map(int, m.groups()))
        if v[0] in NAMES: rows.setdefault(v[0], []).append(v)
    pm3 = {}
    p = f"{D}/{L}_pm3_sof_delay.txt"
    for ln in (open(p) if os.path.exists(p) else []):
        m = re.search(r"sof_delay_us=([\d.]+)", ln)
        pm3.setdefault(ln.split()[0], []).append(m.group(1) if m else "silence")
    print(f"\n### {L}\n")
    print("| blocks | resp B | n | EOF->RxEnd | handler | encode | FDT wait | tx call | prefill | EOF->DMA | PM3 SOF delay us |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for b in sorted(rows):
        r = rows[b]; med = lambda f: st.median([f(x) for x in r])
        cols = [med(lambda x: x[1]), med(lambda x: x[2]), med(lambda x: x[3] - x[2]), med(lambda x: x[4] - x[3]),
                med(lambda x: x[5] - x[4]), med(lambda x: x[6] - x[5]), med(lambda x: x[6] + x[1])]
        summary.setdefault(L, {})[b] = us(cols[-1])
        c = lambda v: f"{int(v)} ({us(v):.0f})"
        print(f"| {NAMES[b][1]} | {b} | {len(r)} | " + " | ".join(c(v) for v in cols) + f" | {' / '.join(pm3.get(NAMES[b][0], []))} |")
print("\n### EOF->DMA (us) per step, 80/1 block ratio\n")
print("| step | " + " | ".join(f"{NAMES[b][1]} blk" for b in sorted(NAMES)) + " | 80/1 |")
print("|---|" + "---|" * (len(NAMES) + 1))
for L, d in summary.items():
    ratio = f"{d[323] / d[7]:.2f}" if 7 in d and 323 in d else "-"
    print(f"| {L} | " + " | ".join(f"{d[b]:.0f}" if b in d else "-" for b in sorted(NAMES)) + f" | {ratio} |")
