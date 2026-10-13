"""FNFC-01.13: `hf 15 wrbl [-o]` logs (`trace list -t 15` text): reply after the LAST WRITE BLOCK request and its timing.
PM3 does not log the single EOF (LogTrace drops zero-length frames), so for -o the only measurable gap is
end of the write request -> start of the tag frame (`trace list` Start/End are in fc). PM3 sends the EOF after its
write timeout (4700 DMA samples * 64 fc = 300800 fc, counted from the first DMA sample after the request) and the
EOF transmission takes 1536 fc (12 slots of 128 fc incl. the leading idle byte), so
EOF->SOF <= gap - 300800 - 1536 fc; the PM3 code time between the request end, the timeout and the EOF is unknown.
usage: wrbl_trace.py <log> [<log> ...]"""
import os, re, sys
FC_US = 1 / 13.56
ROW = re.compile(r"^\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(Rdr|Tag)\s*\|([0-9A-F ]+)\|")
for p in sys.argv[1:]:
    rows = []
    for line in open(p, errors="replace"):
        m = ROW.match(line)
        if m:
            rows.append((int(m[1]), int(m[2]), m[3], bytes.fromhex(m[4])))
    idx = [k for k, r in enumerate(rows) if r[2] == "Rdr" and len(r[3]) >= 2 and r[3][1] == 0x21]
    name = os.path.basename(p)
    if not idx:
        print(f"{name}: no WRITE BLOCK"); continue
    k = idx[-1]; req = rows[k]
    tag = rows[k + 1] if k + 1 < len(rows) and rows[k + 1][2] == "Tag" else None
    opt = bool(req[3][0] & 0x40)
    if tag is None:
        print(f"{name}: opt={opt} no reply"); continue
    gap = tag[0] - req[1]
    s = f"{name}: opt={opt} reply={tag[3].hex(' ')} len={len(tag[3])} gap={gap} fc = {gap * FC_US:.1f} us"
    if opt:
        ub = gap - 300800 - 1536
        s += f"; EOF->SOF <= {ub} fc = {ub * FC_US:.1f} us"
    print(s)
