"""Compare PM3 output against the .nfc dump.
usage: compare_dump.py <dump.nfc> raw <pm3 log of hf 15 raw ...004F>   -> 80-block response == 00 + dump data (+ CRC ok)
       compare_dump.py <dump.nfc> dump <pm3 log of hf 15 dump>         -> every block == dump"""
import re, sys
nfc, mode, log = sys.argv[1], sys.argv[2], open(sys.argv[3], errors="replace").read()
data = bytes.fromhex(re.search(r"^Data Content:(.*)$", open(nfc).read(), re.M).group(1))
if mode == "raw":
    m = re.findall(r"\((\d+)\) ([0-9A-F ]+)", log)
    if not m: print("FAIL: no response (silence)"); sys.exit(1)
    n, hexs = m[-1]; resp = bytes.fromhex(hexs)
    ok = int(n) == 323 and len(resp) >= 321 and resp[0] == 0 and resp[1:321] == data
    print(f"{'OK' if ok else 'FAIL'}: ({n}) flags={resp[0]:02X} data_equal={resp[1:321] == data} crc={resp[321:323].hex(' ').upper()}")
else:
    blocks = {int(b): bytes.fromhex(h) for b, h in re.findall(r"^\[=\]\s+(\d+) \| ([0-9A-F]{2}(?: [0-9A-F]{2}){3}) \|", log, re.M)}
    bad = [b for b in range(80) if blocks.get(b) != data[b * 4:b * 4 + 4]]
    # an aborted dump still prints all 80 rows (unread ones as 00), which match the zero blocks of the dump
    prog = [int(x) for x in re.findall(r"blk +(\d+)", log)]
    aborted = "command failed" in log or (prog and max(prog) < 80)
    ok = not bad and len(blocks) == 80 and not aborted
    print(f"{'OK' if ok else 'FAIL'}: {len(blocks)} blocks read, {len(blocks) - len([b for b in bad if b in blocks])}/80 equal, differing/missing: {bad}"
          + (f", ABORTED after blk {max(prog) if prog else '?'} (rows past it are not tag data)" if aborted else ""))
    sys.exit(0 if ok else 1)
