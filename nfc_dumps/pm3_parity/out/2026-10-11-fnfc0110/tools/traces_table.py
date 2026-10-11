"""FNFC-01.10: PM3 `hf 15 raw` traces -> per-trace result and per-series summary.
For each trace the response after the LAST occurrence of the ReadSingle/ReadMultiple request is taken,
length-checked and compared byte by byte with 00 + dump blocks + CRC. Also printed:
  end-start  = End - Start of the tag frame in the trace (fc);
  nominal    = the value armsrc/iso15693.c derives Start from (32*16*4 + len*8*8*16*4 + 32*16*4 fc)
               -> end-start == nominal for every decoded frame: the trace carries no frame-length info;
  sof_delay  = Start(tag) - End(request), us (as tools/pm3delay.py).
usage: traces_table.py <dump.nfc> <trace> [<trace> ...]"""
import collections, os, re, sys
H = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(H, "..", "..", ".."))  # nfc_dumps/pm3_parity
sys.path.insert(0, H)
from pm3_matrix import parse_trace
from frames import dump_data, read_reply, pm3_nominal_fc

data = dump_data(sys.argv[1])
U = bytes.fromhex("AC839A01090104E0")
series = collections.OrderedDict()
print("| trace | request | resp | ok | end-start fc | nominal fc | diff | sof_delay us |")
print("|---|---|---|---|---|---|---|---|")
for p in sys.argv[2:]:
    ev = parse_trace(p)
    idx = [k for k, e in enumerate(ev) if not e["resp"] and len(e["frame"]) >= 11 and e["frame"][1] in (0x20, 0x23) and e["frame"][2:10] == U]
    name = os.path.basename(p)[:-6]
    key = re.sub(r"_\d+$", "", name)
    if not idx:
        print(f"| {name} | no request | | | | | | |"); continue
    k = idx[-1]; req = ev[k]; f = req["frame"]
    first = f[10]; count = 1 if f[1] == 0x20 else f[11] + 1
    want = read_reply(data, first, count)
    resp = ev[k + 1] if k + 1 < len(ev) and ev[k + 1]["resp"] else None
    s = series.setdefault(key, {"n": 0, "ok": 0, "len": len(want), "sof": [], "fail": collections.Counter()})
    s["n"] += 1
    reqhex = f"{f[1]:02X} {first:02X}" + (f" {f[11]:02X}" if f[1] == 0x23 else "")
    if resp is None:
        s["fail"]["silence"] += 1
        print(f"| {name} | {reqhex} | silence | FAIL | | | | |"); continue
    r = resp["frame"]
    ok = r == want and resp["crc_ok"]
    es = resp["dur"] * 32
    nom = pm3_nominal_fc(len(r))
    sof = (resp["ts"] - (req["ts"] + req["dur"] * 32)) / 13.56
    if ok:
        s["ok"] += 1; s["sof"].append(sof)
    else:
        s["fail"][f"({len(r)}) {'crc ok' if resp['crc_ok'] else 'bad crc'}"] += 1
    print(f"| {name} | {reqhex} | ({len(r)}) | {'OK' if ok else 'FAIL'} | {es} | {nom} | {es - nom} | {sof:.1f} |")
print()
print("| series | expected | decoded = dump | failures | sof_delay us (ok) |")
print("|---|---|---|---|---|")
for key, s in series.items():
    sof = s["sof"]
    rng = f"{min(sof):.1f}..{max(sof):.1f}" if sof else "-"
    print(f"| {key} | ({s['len']}) | {s['ok']}/{s['n']} | {', '.join(f'{v}x {k}' for k, v in s['fail'].items()) or '-'} | {rng} |")
