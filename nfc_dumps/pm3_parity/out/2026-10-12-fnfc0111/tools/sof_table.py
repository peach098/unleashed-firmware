"""PM3 SOF delay per read length (FNFC-01.11): response after the LAST occurrence of the exact request,
length- and data-checked against the dump; any number of repeats.
usage: sof_table.py <dir> <dump.nfc> <label> [<label> ...]
       reads <label>_{rd1,rdm8,rdm32,rdm64,rdm80}_<i>.trace (series.sh DWT=1 NREP=n)
Prints one line per sample and a markdown table: n decoded / n, median, min, max (us), card 316 us.
SOF delay = (tag Start - (reader Start + dur*32)) / 13.56: the moment the PM3 detected the tag SOF
(quantum 64 fc = 4.72 us), measured exactly like the card reference (01.1)."""
import sys, glob, os, re, statistics as st
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))  # nfc_dumps/pm3_parity
from pm3_matrix import parse_trace
D, DUMP, LABELS = sys.argv[1], sys.argv[2], sys.argv[3:]
DATA = bytes.fromhex(re.search(r"^Data Content:(.*)$", open(DUMP).read(), re.M).group(1))
U = bytes.fromhex("AC839A01090104E0")
CMDS = [("rd1", 1, bytes([0x22, 0x20]) + U + bytes([0])), ("rdm8", 8, bytes([0x22, 0x23]) + U + bytes([0, 7])),
        ("rdm32", 32, bytes([0x22, 0x23]) + U + bytes([0, 0x1F])), ("rdm64", 64, bytes([0x22, 0x23]) + U + bytes([0, 0x3F])),
        ("rdm80", 80, bytes([0x22, 0x23]) + U + bytes([0, 0x4F]))]
summary = {}
for L in LABELS:
    for n, blk, c in CMDS:
        exp = 1 + 4 * blk + 2
        files = sorted(glob.glob(f"{D}/{L}_{n}_*.trace"), key=lambda p: int(re.search(r"_(\d+)\.trace$", p).group(1)))
        vals, tot = [], 0
        for p in files:
            tot += 1
            ev = parse_trace(p)
            idx = [k for k, e in enumerate(ev) if not e["resp"] and e["frame"][:len(c)] == c]
            if not idx: print(f"{L} {os.path.basename(p)}: no request in trace (stale?)"); continue
            k = idx[-1]; req = ev[k]
            resp = ev[k + 1] if k + 1 < len(ev) and ev[k + 1]["resp"] else None
            if resp is None: print(f"{L} {os.path.basename(p)}: silence"); continue
            f = resp["frame"]; want = bytes([0]) + DATA[:blk * 4]
            ok = len(f) == exp and resp["crc_ok"] and f[:-2] == want
            d = (resp["ts"] - (req["ts"] + req["dur"] * 32)) / 13.56
            print(f"{L} {os.path.basename(p)}: len={len(f)} crc={resp['crc_ok']} data={'EQUAL' if ok else 'DIFF'} sof_delay_us={d:.1f}")
            if ok: vals.append(d)
        summary.setdefault(L, []).append((blk, len(vals), tot, vals))
for L, rows in summary.items():
    print(f"\n### {L}\n\n| blocks | decoded | median us | min us | max us | median - 316 us |\n|---|---|---|---|---|---|")
    med1 = None
    for blk, ok, tot, v in rows:
        if not v: print(f"| {blk} | 0/{tot} | - | - | - | - |"); continue
        m = st.median(v); med1 = m if blk == 1 else med1
        print(f"| {blk} | {ok}/{tot} | {m:.1f} | {min(v):.1f} | {max(v):.1f} | {m - 316:+.1f} |")
    allv = [x for r in rows for x in r[3]]
    m80 = next((st.median(r[3]) for r in rows if r[0] == 80 and r[3]), None)
    if allv: print(f"\nall lengths: min {min(allv):.1f} us, max {max(allv):.1f} us" + (f"; 80/1 = {m80 / med1:.3f}" if med1 and m80 else ""))
