#!/bin/bash
# Length/offset sweep of ReadMultiple against the Flipper emulation (repo root, inside newgrp uucp).
# usage: sweep.sh <outdir> <label>
OUT=$(readlink -f "$1"); L="$2"; D=$(dirname "$(readlink -f "$0")")
T=nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools
PY=toolchain/current/bin/python3; PM3=~/proxmark3/pm3; PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
mkdir -p "$OUT"
timeout 40 $PY $T/emu_open.py $T > "$OUT/${L}_emu_open.txt" 2>&1; sleep 1
timeout 400 $PY $D/logcap_lvl.py "$OUT/${L}_debug_log.txt" 200 "log debug" >/dev/null 2>&1 & LP=$!
sleep 3
U=AC839A01090104E0
# first:count-1 (hex) : 32 blocks from offsets 0/32/48, then growing length from 0
for fc in 001F 201F 301F 0023 0027 002B 002F 0033 0037 003B 003F 004F; do
  b="$OUT/${L}_rdm_$fc"
  timeout 60 $PM3 -p $PORT --incognito -f -c "trace clear; hf 15 raw -a -c -d 2223${U}${fc}; trace list -t 15; trace save -f $b" > "$b.log" 2>&1
  echo "$fc: $(grep -aoE '\([0-9]+\) [0-9A-F ]{0,12}|command failed|timeout' "$b.log" | head -1)"
  sleep 1
done
b="$OUT/${L}_rdm_004F_w"
timeout 60 $PM3 -p $PORT --incognito -f -c "hf 15 raw -a -w -c -d 2223${U}004F" > "$b.log" 2>&1
echo "004F -w: $(grep -aoE '\([0-9]+\) [0-9A-F ]{0,12}|command failed|timeout' "$b.log" | head -1)"
kill $LP 2>/dev/null; wait $LP 2>/dev/null
grep -a 'SEQ\|PROBE' "$OUT/${L}_debug_log.txt" | sed 's/\x1b\[[0-9;]*m//g' | tail -40
timeout 30 $PY $T/emu_exit.py $T
