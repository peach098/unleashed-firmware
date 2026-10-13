#!/bin/bash
# FNFC-01.12: N x `hf 15 dump` on the running emulation, each with its own device trace (repo root, inside `newgrp uucp`).
# Each run: data clear; trace clear; hf 15 dump; trace list -t 15; trace save, then compare with the dump.
# usage: dumps.sh <outdir> <label> <N> [first index]
set -u
OUT=$(readlink -f "$1"); L="$2"; N="$3"; S=${4:-1}
T13=$(readlink -f nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools)
PM3=${PM3:-~/proxmark3/pm3}; PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
DUMP="nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc"
for i in $(seq $S $((S + N - 1))); do
  b="$OUT/${L}_dump_$i"; rm -f "$b.trace"
  timeout 180 $PM3 -p $PORT --incognito -f -c "data clear; trace clear; hf 15 dump; trace list -t 15; trace save -f $b" > "$b.log" 2>&1 < /dev/null
  echo "$L #$i: $(python3 $T13/compare_dump.py "$DUMP" dump "$b.log" | tee "$b.compare.txt")"
done
