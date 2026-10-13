#!/bin/bash
# FNFC-01.14 acceptance in 1-of-256 on the flashed firmware: `hf 15 reader -2` x3, `hf 15 reader --all -2` x3,
# `hf 15 dump -2` x NDUMP (compared with the dump). Emulation opened quietly (01.12 emu_open_quiet.py).
# usage: checks256.sh <outdir> <label>   (repo root, inside `newgrp uucp`)
OUT=$(readlink -f "$1"); L="$2"; T=$(dirname "$(readlink -f "$0")")
T13=$(readlink -f "$T/../../2026-10-11-fnfc013/tools"); T12=$(readlink -f "$T/../../2026-10-13-fnfc0112/tools")
PY=toolchain/current/bin/python3; PM3=${PM3:-~/proxmark3/pm3}; PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
DUMP="nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc"
timeout 40 $PY $T12/emu_open_quiet.py $T13 > "$OUT/${L}_emu_open.txt" 2>&1 < /dev/null; sleep 2
for i in 1 2 3; do
  timeout 60 $PM3 -p $PORT --incognito -f -c "data clear; hf 15 reader -2" > "$OUT/${L}_reader2_$i.log" 2>&1 < /dev/null
  echo "hf 15 reader -2 #$i: $(grep -acE 'E0 04 01 09 01 9A 83 AC' "$OUT/${L}_reader2_$i.log") UID lines"
done
for i in 1 2 3; do
  timeout 60 $PM3 -p $PORT --incognito -f -c "data clear; hf 15 reader --all -2" > "$OUT/${L}_reader_all2_$i.log" 2>&1 < /dev/null
  echo "hf 15 reader --all -2 #$i: $(grep -acE 'E0 04 01 09 01 9A 83 AC' "$OUT/${L}_reader_all2_$i.log") UID lines"
done
for i in $(seq 1 ${NDUMP:-3}); do
  timeout 300 $PM3 -p $PORT --incognito -f -c "data clear; hf 15 dump -2 --ns" > "$OUT/${L}_hf15_dump2_$i.log" 2>&1 < /dev/null
  python3 $T13/compare_dump.py "$DUMP" dump "$OUT/${L}_hf15_dump2_$i.log" | tee "$OUT/${L}_hf15_dump2_$i.compare.txt"
done
timeout 30 $PY $T13/emu_exit.py $T13 > /dev/null 2>&1 < /dev/null
