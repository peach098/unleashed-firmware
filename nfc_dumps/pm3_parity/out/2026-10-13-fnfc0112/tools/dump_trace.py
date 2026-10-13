"""FNFC-01.12: per-run analysis of `hf 15 dump` traces.
For every READBLOCK request (cmd 0x20) in <base>.trace: block number, whether a tag frame follows before the next
reader frame (length / CRC), PM3 pause = request start - end of the previous tag frame (fc and us), reply SOF delay.
usage: dump_trace.py <outdir> <label> [runs...]   -> table on stdout (one row per run) + details for failed requests
"""
import glob, os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../.."))
from pm3_matrix import parse_trace  # noqa: E402

FC_US = 1 / 13.56


def analyse(trace):
    """timestamps in fc (13.56 MHz); dur in units of 32 fc (as sof_table.py)"""
    fr = parse_trace(trace)
    reqs = []
    last_tag_end = None
    for k, f in enumerate(fr):
        if f["resp"]:
            last_tag_end = f["ts"] + f["dur"] * 32  # dur is in 32 fc units
            continue
        b = f["frame"]
        if len(b) >= 3 and b[1] == 0x20:
            blk = b[10] if b[0] & 0x20 else b[2]
            nxt = fr[k + 1] if k + 1 < len(fr) else None
            tag = nxt if nxt is not None and nxt["resp"] else None
            reqs.append({
                "blk": blk, "ts": f["ts"], "end": f["ts"] + f["dur"] * 32, "crc_req": f["crc_ok"],
                "pause": (f["ts"] - last_tag_end) if last_tag_end is not None and f["ts"] > last_tag_end else None,
                "tag": tag,
                "sof": (tag["ts"] - (f["ts"] + f["dur"] * 32)) if tag else None,
                "ok": bool(tag and tag["crc_ok"] and len(tag["frame"]) == 8),
            })
    return reqs


def main():
    out, label = sys.argv[1], sys.argv[2]
    runs = sys.argv[3:] or sorted({int(m) for m in (re.findall(rf"{label}_dump_(\d+)\.trace$", p) and
                                                     re.findall(rf"{label}_dump_(\d+)\.trace$", p) or [] for p in glob.glob(f"{out}/{label}_dump_*.trace")) for m in m}, key=int)
    print("| run | result | abort after blk | READBLOCK reqs | failed reqs (blk: request / reply / PM3 pause before it) | pause min..max us | SOF min..max us |")
    print("|---|---|---|---|---|---|---|")
    allp, alls = [], []
    for r in runs:
        base = f"{out}/{label}_dump_{r}"
        cmp_ = open(base + ".compare.txt").read().strip() if os.path.exists(base + ".compare.txt") else "?"
        res = "complete" if cmp_.startswith("OK") else "ABORTED"
        m = re.search(r"ABORTED after blk (\d+)", cmp_)
        reqs = analyse(base + ".trace")
        bad = [q for q in reqs if not q["ok"]]
        ps = [q["pause"] for q in reqs if q["pause"] is not None]
        ss = [q["sof"] for q in reqs if q["sof"] is not None]
        allp += ps; alls += ss
        desc = "; ".join(
            f"{q['blk']}: req {'crc ok' if q['crc_req'] else 'CRC BAD'} / "
            + (f"tag ({len(q['tag']['frame'])}) {q['tag']['frame'].hex(' ').upper()} crc {'ok' if q['tag']['crc_ok'] else 'BAD'} sof {q['sof'] * FC_US:.1f}us" if q["tag"] else "NO TAG FRAME")
            + (f" / pause {q['pause']} fc = {q['pause'] * FC_US:.0f} us" if q['pause'] else " / pause n/a") for q in bad) or "-"
        rng = lambda v: f"{min(v) * FC_US:.0f}..{max(v) * FC_US:.0f}" if v else "-"
        print(f"| {r} | {res} | {m.group(1) if m else '-'} | {len(reqs)} | {desc} | {rng(ps)} | {rng(ss)} |")
    if allp:
        allp.sort(); alls.sort()
        print(f"\nall runs: {len(allp)} pauses, median {allp[len(allp) // 2] * FC_US:.0f} us, min {allp[0] * FC_US:.0f} us; "
              f"SOF median {alls[len(alls) // 2] * FC_US:.1f} us, min {alls[0] * FC_US:.1f}, max {alls[-1] * FC_US:.1f}")


main()
