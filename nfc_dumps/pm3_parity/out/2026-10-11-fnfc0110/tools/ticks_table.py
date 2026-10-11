"""FNFC-01.10: on-device frame length from the tick probe ([TICKS] lines of tick_probe.patch).
For every logged listener frame of a known reply length (7/35/131/259/323 B: block 0 / blocks 0..n-1)
the device's sum of (ARR + 1) is compared with
  host  = the host harness prediction for the same bytes and the same lib/ (exact model check),
  ideal = the durations iso15693_signal.c requests (10 ps truncated physical constants),
  phys  = exact multiples of 1/fc (13.56 MHz).
usage: ticks_table.py <dump.nfc> <lib dir for the host model> <probe_log> [<label>]"""
import collections, os, re, subprocess, sys, tempfile
H = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, H)
from frames import dump_data, read_reply, ideal_ticks
HOST = os.path.join(H, "..", "host")
data = dump_data(sys.argv[1]); lib = os.path.abspath(sys.argv[2]); log = open(sys.argv[3], errors="replace").read()
label = sys.argv[4] if len(sys.argv) > 4 else os.path.basename(sys.argv[3])
exe = os.path.join(tempfile.mkdtemp(), "tick_model")
subprocess.run(["gcc", "-O2", "-w", "-I", f"{HOST}/stubs", "-I", HOST, "-I", lib, f"{HOST}/tick_model.c", "-lm", "-o", exe], check=True)
BLOCKS = {7: 1, 35: 8, 131: 32, 259: 64, 323: 80}
seen = collections.OrderedDict()
for n, periods, ticks in re.findall(r"\[TICKS\] (\d+)B periods=(\d+) ticks=(\d+)", log):
    seen.setdefault(int(n), []).append((int(periods), int(ticks)))
print(f"### {label}")
print("| reply B | frames logged | device periods | device ticks | host model ticks | device - host | ideal ticks | device - ideal | ppm vs ideal | ppm vs 1/fc |")
print("|---|---|---|---|---|---|---|---|---|---|")
for n, vals in sorted(seen.items()):
    pv = sorted(set(v[0] for v in vals)); tv = sorted(set(v[1] for v in vals))
    if n in BLOCKS:
        frame = read_reply(data, 0, BLOCKS[n])
        out = subprocess.run([exe, "--frame", frame.hex(" ")], capture_output=True, text=True).stdout
        m = re.search(r"periods=(\d+) ticks=(\d+)", out); hp, ht = int(m.group(1)), int(m.group(2))
        dev = tv[0] if len(tv) == 1 else None
        ide = ideal_ticks(n); phy = ideal_ticks(n, True)
        t = tv[0]
        print(f"| {n} | {len(vals)} | {','.join(map(str, pv))} | {','.join(map(str, tv))} | {ht} (periods {hp}) | {t - ht:+d} | {ide:.2f} | {t - ide:+.2f} | {(t - ide) / ide * 1e6:+.1f} | {(t - phy) / phy * 1e6:+.1f} |")
    else:
        print(f"| {n} | {len(vals)} | {','.join(map(str, pv))} | {','.join(map(str, tv))} | (other frame, not modelled) | | {ideal_ticks(n):.2f} | {tv[0] - ideal_ticks(n):+.2f} | {(tv[0] - ideal_ticks(n)) / ideal_ticks(n) * 1e6:+.1f} | |")
