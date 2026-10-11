"""Breakdown of the fixed handler window (RxEnd -> nfc_listener_tx) from [PROBE3] lines of latency_probe_v3.
usage: python3 probe3_table.py <dir> <label>   (reads <label>_probe_log.txt)
Stamps are DWT cycles @64 MHz relative to RxEnd; each column is the delta from the previous stamp, median of samples."""
import re, sys, statistics as st
D, L = sys.argv[1], sys.argv[2]
RE = re.compile(r"\[PROBE3\] (\d+)B rx=(\d+) copy=(\d+) run=(\d+) crc=(\d+) handler=(\d+) send=(\d+) crc_app=(\d+) tx_enter=(\d+)")
NAMES = {7: 1, 35: 8, 131: 32, 259: 64, 323: 80}
STAGES = [("RxEnd->rx (nfc_listener_rx, parser copy)", 0, None),
          ("rx->copy (bit_buffer_copy_bits)", 1, 0),
          ("copy->run (dispatch to iso15693_3_listener_run)", 2, 1),
          ("run->crc (request CRC check+trim)", 3, 2),
          ("crc->handler (flags/addr/UID checks)", 4, 3),
          ("handler->send (cmd handler + SLIX ext)", 5, 4),
          ("send->crc_app (tx copy + reply CRC)", 6, 5),
          ("crc_app->tx_enter (nfc_listener_tx call)", 7, 6),
          ("total RxEnd->tx_enter", 7, None)]
rows = {}
for m in RE.finditer(open(f"{D}/{L}_probe_log.txt", errors="replace").read()):
    v = list(map(int, m.groups()))
    if v[0] in NAMES: rows.setdefault(v[0], []).append(v[1:])
sizes = sorted(rows)
print("| stage | " + " | ".join(f"{NAMES[b]} blk ({b} B, n={len(rows[b])})" for b in sizes) + " |")
print("|---|" + "---|" * len(sizes))
for name, a, b in STAGES:
    cells = []
    for s in sizes:
        v = st.median([(x[a] - (x[b] if b is not None else 0)) & 0xFFFFFFFF for x in rows[s]])
        cells.append(f"{int(v)} ({v / 64:.1f})")
    print(f"| {name} | " + " | ".join(cells) + " |")
