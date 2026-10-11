#!/usr/bin/env python3
"""FNFC-01.10: attribute the frame-length error to the three arithmetic defects (host model).
Builds tick_model.c against 8 variants of lib/digital_signal: {old, exact} tick unit x {old, fixed}
merge carry x {old one-sided, two-sided} remainder carry, plus the FNFC-01.3 exp_ttim pre-scaling
experiment, and prints the ISO15693 frame error of the fixed-size frames.
The last two columns use the real 80-block reply of card AC (00 + dump + CRC 24 9E, mostly zero bytes,
hence few equal-level joints): error against the requested durations and against exact multiples of 1/fc.
usage (repo root): attribution.py <commit with the old code> [<dump.nfc>] > attribution.txt"""
import itertools, os, re, subprocess, sys, tempfile
old_rev = sys.argv[1]
H = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(H, "..", "tools"))
from frames import dump_data, read_reply, ideal_ticks
REAL = read_reply(dump_data(sys.argv[2] if len(sys.argv) > 2 else "nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc"), 0, 80)
FILES = ["digital_signal_i.h", "digital_signal.c", "digital_sequence.c", "digital_sequence.h", "digital_signal.h",
         "presets/nfc/iso15693_signal.c", "presets/nfc/iso15693_signal.h",
         "presets/nfc/iso14443_3a_signal.c", "presets/nfc/iso14443_3a_signal.h"]
def git_show(rev, f):
    return subprocess.run(["git", "show", f"{rev}:lib/digital_signal/{f}"], capture_output=True, text=True, check=True).stdout
new = {f: open(f"lib/digital_signal/{f}").read() for f in FILES}
old = {f: git_show(old_rev, f) for f in FILES}
MERGE_NEW = "reload_value_carry = reload_value + 1;"
MERGE_OLD = "reload_value_carry = reload_value;"
REM_NEW = re.search(r"        if\(remainder_ticks > DIGITAL_SIGNAL_T_TIM_DIV2\) \{.*?reload_value_carry -= 1;\n        \}", new["digital_sequence.c"], re.S).group(0)
REM_OLD = """        if(remainder_ticks >= DIGITAL_SIGNAL_T_TIM_DIV2) {
            remainder_ticks -= DIGITAL_SIGNAL_T_TIM;
            reload_value_carry += 1;
        }"""
assert MERGE_NEW in new["digital_sequence.c"] and REM_NEW
def variant(unit, merge, rem, exp=False):
    files = dict(new)
    if unit == "old":
        files["digital_signal_i.h"] = old["digital_signal_i.h"]; files["digital_signal.c"] = old["digital_signal.c"]
    seq = files["digital_sequence.c"]
    if merge == "old": seq = seq.replace(MERGE_NEW, MERGE_OLD)
    if rem == "old": seq = seq.replace(REM_NEW, REM_OLD)
    files["digital_sequence.c"] = seq
    if exp:  # FNFC-01.3 experiment: pre-scale the ISO15693 durations by 1562/1562.5
        files["presets/nfc/iso15693_signal.c"] = files["presets/nfc/iso15693_signal.c"].replace(
            "(16.0e11 / ISO15693_SIGNAL_FC)", "(16.0e11 / ISO15693_SIGNAL_FC * (1562.0 / 1562.5))").replace(
            "(256.0e11 / ISO15693_SIGNAL_FC)", "(256.0e11 / ISO15693_SIGNAL_FC * (1562.0 / 1562.5))").replace(
            "(768.0e11 / ISO15693_SIGNAL_FC)", "(768.0e11 / ISO15693_SIGNAL_FC * (1562.0 / 1562.5))")
    d = tempfile.mkdtemp()
    for f, s in files.items():
        p = os.path.join(d, "digital_signal", f); os.makedirs(os.path.dirname(p), exist_ok=True); open(p, "w").write(s)
    exe = os.path.join(d, "tick_model")
    subprocess.run(["gcc", "-O2", "-w", "-I", f"{H}/stubs", "-I", H, "-I", d, f"{H}/tick_model.c", "-lm", "-o", exe], check=True)
    p = subprocess.run([exe], capture_output=True, text=True); out = p.stdout + p.stderr
    real = subprocess.run([exe, "--frame", REAL.hex(" ")], capture_output=True, text=True).stdout
    return out, int(re.search(r"ticks=(\d+)", real).group(1))
def real_vs(t, exp, phys=False):
    ideal = ideal_ticks(len(REAL), phys)
    if exp and not phys:  # requested durations were pre-scaled by 1562/1562.5
        ideal *= 1562.0 / 1562.5
    return f"{(t - ideal) / ideal * 1e6:+.1f} ppm ({t - ideal:+.1f} t, {(t - ideal) / 64:+.2f} us)"
rows = []
combos = [(u, m, r, False) for u, m, r in itertools.product(["old", "exact"], ["old", "fixed"], ["old", "two-sided"])]
combos.insert(1, ("old", "old", "old", True))
print("ISO15693 frame error (sum of ARR+1 vs requested durations), ppm of the frame; host model")
print("| tick unit | merge carry | remainder carry | 1 blk hi (7 B) | 32 blk hi (131 B) | 64 blk hi (259 B) | 80 blk hi (323 B) | 80 blk lo (323 B) | ISO14443-A worst (ticks) | checks failed | real 80-blk reply vs request | real 80-blk reply vs 1/fc |")
print("|---|---|---|---|---|---|---|---|---|---|---|---|")
for u, m, r, e in combos:
    out, real_ticks = variant(u, m, r, e)
    if os.environ.get("DEBUG"): print(out[-800:], file=sys.stderr)
    def ppm(rate, n):
        g = re.search(rf"iso15693 rate={rate} bytes={n} .*?err=([-+0-9.]+) ticks \(([-+0-9.]+) ppm\)", out)
        return f"{g.group(2)} ({g.group(1)} t)" if g else "?"
    a = re.search(r"iso14443a: .*worst frame error ([-+0-9.]+)", out)
    f = re.search(r"(\d+) failed check", out)
    name = ("old (1562) + exp_ttim pre-scale" if e else u)
    print(f"| {name} | {m} | {r} | {ppm('hi',7)} | {ppm('hi',131)} | {ppm('hi',259)} | {ppm('hi',323)} | {ppm('lo',323)} | {a.group(1) if a else '?'} | {f.group(1) if f else '?'} | {real_vs(real_ticks, e)} | {real_vs(real_ticks, e, True)} |")
