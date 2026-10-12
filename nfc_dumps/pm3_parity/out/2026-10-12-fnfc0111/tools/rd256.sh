#!/bin/bash
# FNFC-01.11: N x read single block 0 with the reader in "1 out of 256" coding (`hf 15 raw -2`), one trace each,
# on the flashed firmware (emulation opened here). Traces <label>_rd1_<i>.trace -> sof_table.py <label>.
# usage: rd256.sh <outdir> <label> [N=5]     (repo root, inside `newgrp uucp`)
OUT=$(readlink -f "$1"); L="$2"; N=${3:-5}; T13=$(readlink -f "$(dirname "$(readlink -f "$0")")/../../2026-10-11-fnfc013/tools")
PY=toolchain/current/bin/python3; PM3=${PM3:-~/proxmark3/pm3}; PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
timeout 40 $PY $T13/emu_open.py $T13 > "$OUT/${L}_emu_open.txt" 2>&1 < /dev/null; sleep 2
for i in $(seq 1 $N); do
  b="$OUT/${L}_rd1_$i"; rm -f "$b.trace"
  timeout 60 $PM3 -p $PORT --incognito -f -c "data clear; trace clear; hf 15 raw -a -c -2 -d 2220AC839A01090104E000; trace list -t 15; trace save -f $b" > "$b.log" 2>&1 < /dev/null
  echo "$(basename $b): $(grep -aoE '\([0-9]+\) [0-9A-F ]{0,17}|command failed|timeout' "$b.log" | tail -1)"; sleep 1
done
timeout 30 $PY $T13/emu_exit.py $T13 > /dev/null 2>&1 < /dev/null
