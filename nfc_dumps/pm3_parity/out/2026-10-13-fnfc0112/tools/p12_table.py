"""FNFC-01.12: match the probe's [P12] records to the `hf 15 dump` runs of a probe series.
Each dump starts with INVENTORY (cmd 01, after the coupling `hf 15 reader`), then GET_SYSTEM_INFO (2B), then
READBLOCK (20) 0..79. Prints per run: RxEnds seen, the records around every PM3 miss (from the run's trace), and
aggregate statistics of the probe fields over all READBLOCK records.
usage: p12_table.py <outdir> <label> <first run> <log file> [<first run> <log file> ...]"""
import os, re, statistics, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "dump_trace.py")).read().replace("\nmain()\n", "\n")
g = {"__file__": os.path.join(os.path.dirname(os.path.abspath(__file__)), "dump_trace.py")}
exec(src, g)

KV = re.compile(r"(\w+)=([-0-9A-Fa-f/B]+)")


def recs(path):
    out = []
    for line in open(path, errors="replace"):
        if "[P12]" in line:
            d = dict(KV.findall(line.split("[P12]", 1)[1]))
            d["line"] = line.split("[P12]", 1)[1].strip()
            out.append(d)
    return out


def main():
    out, label = sys.argv[1], sys.argv[2]
    pairs = list(zip(sys.argv[3::2], sys.argv[4::2]))
    allrb = []
    print("| run | trace: result / missed request | P12 RxEnds in run | P12 READBLOCK blocks seen | record(s) for the missed block / the next RxEnd |")
    print("|---|---|---|---|---|")
    for first, log in pairs:
        rs = recs(log)
        # split into runs at each INVENTORY; the first group is the coupling `hf 15 reader`
        groups, cur = [], None
        for r in rs:
            if r.get("cmd") == "01":
                cur = []; groups.append(cur)
            if cur is not None: cur.append(r)
        groups = groups[1:]
        for k, grp in enumerate(groups):
            run = int(first) + k
            base = f"{out}/{label}_dump_{run}"
            if not os.path.exists(base + ".trace"): continue
            q = g["analyse"](base + ".trace")
            bad = [x for x in q if not x["ok"]]
            rb = [r for r in grp if r.get("cmd") == "20"]
            allrb += rb
            blks = [int(r["blk"]) for r in rb]
            desc = "-"
            if bad:
                b = bad[0]["blk"]
                m = [r["line"] for r in rb if int(r["blk"]) == b]
                nxt = [r["line"] for r in grp if r.get("cmd") != "20" or int(r["blk"]) > b][:0]
                idx = [i for i, r in enumerate(grp) if r.get("cmd") == "20" and int(r["blk"]) == b - 1]
                after = grp[idx[0] + 1]["line"] if idx and idx[0] + 1 < len(grp) else "(no further RxEnd in this run)"
                desc = ("RECORD: " + " // ".join(m)) if m else f"NO RxEnd for blk {b}; next RxEnd: {after}"
            print(f"| {run} | {'complete' if not bad else 'miss blk %d (%s)' % (bad[0]['blk'], 'no tag frame' if not bad[0]['tag'] else 'bad tag frame')} | {len(grp)} | {min(blks) if blks else '-'}..{max(blks) if blks else '-'} ({len(blks)}) | {desc} |")
    if allrb:
        print()
        for f in ("eof2dma", "rearm", "txend2arm", "armed2sof", "fail2sof", "sofail", "pfail", "restarts", "maxrestart", "ovf", "maxb", "wake", "crc"):
            v = [int(r[f]) for r in allrb if f in r and re.fullmatch(r"-?\d+", r[f])]
            if v:
                print(f"{f:10s} n={len(v)} min={min(v)} median={statistics.median(v)} max={max(v)}  values>median: {sorted(set(x for x in v if x > statistics.median(v)))[:12]}")
        print("bad bytes:", sorted({r.get('bad') for r in allrb}))


main()
