"""PM3 SOF delay per sample: response after the LAST occurrence of the exact request, length-checked.
With a dump (.nfc) as 3rd argument the response payload is also compared byte by byte:
response = 00 + dump blocks[first..first+n-1] + CRC (first mismatch offset is reported).
usage: pm3delay.py <dir> <label> [<dump.nfc>]"""
import sys, glob, os, re
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))  # nfc_dumps/pm3_parity
from pm3_matrix import parse_trace
D, L = sys.argv[1], sys.argv[2]
DATA = None
if len(sys.argv) > 3:
    DATA = bytes.fromhex(re.search(r"^Data Content:(.*)$", open(sys.argv[3]).read(), re.M).group(1))
NBLK = {"rd1": 1, "rdm8": 8, "rdm32": 32, "rdm64": 64, "rdm80": 80}
U = bytes.fromhex("AC839A01090104E0")
cmds = {"rd1": (bytes([0x22,0x20])+U+bytes([0]), 7), "rdm8": (bytes([0x22,0x23])+U+bytes([0,7]),35),
        "rdm32": (bytes([0x22,0x23])+U+bytes([0,0x1F]),131), "rdm64": (bytes([0x22,0x23])+U+bytes([0,0x3F]),259),
        "rdm80": (bytes([0x22,0x23])+U+bytes([0,0x4F]),323)}
for n,(c,exp) in cmds.items():
    for i in (1,2,3):
        p = f"{D}/{L}_{n}_{i}.trace"
        ev = parse_trace(p)
        idx = [k for k,e in enumerate(ev) if not e["resp"] and e["frame"][:len(c)]==c]
        if not idx: print(n,i,"frames",len(ev),"no req"); continue
        k = idx[-1]; req = ev[k]
        resp = ev[k+1] if k+1 < len(ev) and ev[k+1]["resp"] else None
        if resp is None: print(f"{n} #{i}: frames={len(ev)} silence"); continue
        d = (resp["ts"] - (req["ts"] + req["dur"]*32))/13.56
        ok = "OK" if len(resp["frame"])==exp else f"LEN MISMATCH exp {exp}"
        cmp = ""
        if DATA is not None:
            f = resp["frame"]; want = bytes([0]) + DATA[:NBLK[n] * 4]
            bad = next((j for j in range(len(want)) if j >= len(f) - 2 or f[j] != want[j]), None)
            cmp = " data=" + ("EQUAL" if bad is None else f"DIFF@{bad}")
        print(f"{n} #{i}: frames={len(ev)} resp_len={len(resp['frame'])} {ok} crc={resp['crc_ok']}{cmp} sof_delay_us={d:.1f}")
