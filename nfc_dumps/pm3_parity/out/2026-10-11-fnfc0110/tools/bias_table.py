"""FNFC-01.10 decode-window sweep: PM3 success of the 80-block reply vs a known frame-length bias.
For each <label>:<bias ppm>, counts `(323)` = dump in <label>_s80_*.log (and <label>_rdm80_*.log), reads
the programmed frame length from [TICKS] 323B in <label>_probe_log.txt (if any), and compares with the
idealised decoder model (decoder_sim/sim_window.txt, 80-block section). The model curve is then shifted
by s (maximum likelihood, binomial) to fit all points: s ~ how much longer (+) or shorter (-) the
Flipper's frame is in the PM3's time base than the tick count says, i.e. the two crystals' offset
plus any decoder alignment the model does not capture.
usage: bias_table.py <dir> <label>:<bias> [...]"""
import glob, math, os, re, sys
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H)
from frames import ideal_ticks
D = sys.argv[1]
dump = re.search(r"^Data Content:(.*)$", open("nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc").read(), re.M).group(1)
data = bytes.fromhex(dump)
sim = []
sec = open(os.path.join(D, "decoder_sim", "sim_window.txt")).read().split("## ")[1]
for p, k, n in re.findall(r"([-+0-9.]+) ppm .*?decoded +(\d+)/(\d+)", sec):
    sim.append((float(p), int(k) / int(n)))
def model(ppm):
    if ppm <= sim[0][0]: return sim[0][1]
    for (p0, f0), (p1, f1) in zip(sim, sim[1:]):
        if p0 <= ppm <= p1: return f0 + (f1 - f0) * (ppm - p0) / (p1 - p0)
    return sim[-1][1]
rows = []
for spec in sys.argv[2:]:
    label, bias = spec.rsplit(":", 1); bias = float(bias)
    ok = n = 0
    for f in sorted(glob.glob(f"{D}/{label}_s80_*.log") + glob.glob(f"{D}/{label}_rdm80_*.log")):
        log = open(f, errors="replace").read(); n += 1
        m = re.findall(r"\((\d+)\) ([0-9A-F ]+)", log)
        if m and m[-1][0] == "323":
            r = bytes.fromhex(m[-1][1])
            ok += r[0] == 0 and r[1:321] == data and r[321:323] == bytes([0x24, 0x9E])
    ticks = None
    pl = f"{D}/{label}_probe_log.txt"
    if os.path.exists(pl):
        t = sorted(set(int(x) for x in re.findall(r"\[TICKS\] 323B periods=\d+ ticks=(\d+)", open(pl, errors="replace").read())))
        ticks = t[0] if len(t) == 1 else (t or None)
    prog = (ticks - ideal_ticks(323, True)) / ideal_ticks(323, True) * 1e6 if isinstance(ticks, int) else bias - 0.6
    rows.append((label, bias, ticks, prog, ok, n))
best = None
for s10 in range(-800, 801):
    s = s10 / 10; ll = 0
    for _, _, _, prog, ok, n in rows:
        p = min(max(model(prog + s), 0.002), 0.998)
        ll += ok * math.log(p) + (n - ok) * math.log(1 - p)
    if best is None or ll > best[1]: best = (s, ll)
s = best[0]
inside = [x / 10 for x in range(-800, 801) if sum((ok * math.log(min(max(model(prog + x / 10), .002), .998)) + (n - ok) * math.log(1 - min(max(model(prog + x / 10), .002), .998))) for _, _, _, prog, ok, n in rows) > best[1] - 2]
print("| label | bias ppm | device ticks (323 B) | programmed vs 1/fc ppm | decoded = dump | model at programmed | model at programmed + s |")
print("|---|---|---|---|---|---|---|")
for label, bias, ticks, prog, ok, n in sorted(rows, key=lambda r: r[1]):
    print(f"| {label} | {bias:+.0f} | {ticks if ticks is not None else '(no probe)'} | {prog:+.1f} | {ok}/{n} | {model(prog):.2f} | {model(prog + s):.2f} |")
print(f"\nbest shift s = {s:+.1f} ppm (log-likelihood {best[1]:.2f}); approx. 95 % interval {min(inside):+.1f} .. {max(inside):+.1f} ppm")
