#!/bin/bash
# FNFC-01.14: N x read single block 0 and N x read multiple 8 blocks with the reader in "1 out of 256"
# coding (`hf 15 raw -2`), one trace each, on the flashed firmware. Like the 01.11 rd256.sh, but the
# emulation is opened with the 01.12 emu_open_quiet.py (no RPC/log session left attached: an attached
# session shifts SOF and hides misses). Traces <label>_{rd1,rdm8}_<i>.trace -> 01.11 sof_table.py.
# usage: rd256.sh <outdir> <label> [N=5] [CMDS="rd1 rdm8"]   (repo root, inside `newgrp uucp`)
#        CODING=4 rd256.sh ...   same with the default 1-of-4 coding (no -2), for comparison
OUT=$(readlink -f "$1"); L="$2"; N=${3:-5}; CMDS=${4:-"rd1 rdm8"}
T=$(dirname "$(readlink -f "$0")"); T13=$(readlink -f "$T/../../2026-10-11-fnfc013/tools"); T12=$(readlink -f "$T/../../2026-10-13-fnfc0112/tools")
PY=toolchain/current/bin/python3; PM3=${PM3:-~/proxmark3/pm3}; PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
U=AC839A01090104E0; C2="-2"; [ "${CODING:-256}" = 4 ] && C2=""
[ "${NOOPEN:-0}" = 1 ] || { timeout 40 $PY $T12/emu_open_quiet.py $T13 > "$OUT/${L}_emu_open.txt" 2>&1 < /dev/null; sleep 2; }
for c in $CMDS; do
  case $c in rd1) D=2220${U}00;; rdm8) D=2223${U}0007;; *) echo "bad cmd $c"; exit 1;; esac
  for i in $(seq 1 $N); do
    b="$OUT/${L}_${c}_$i"; rm -f "$b.trace"
    timeout 60 $PM3 -p $PORT --incognito -f -c "data clear; trace clear; hf 15 raw -a -c $C2 -d $D; trace list -t 15; trace save -f $b" > "$b.log" 2>&1 < /dev/null
    echo "$(basename $b): $(grep -aoE '\([0-9]+\) [0-9A-F ]{0,17}|command failed|timeout|no response' "$b.log" | tail -1)"; sleep 1
  done
done
[ "${NOEXIT:-0}" = 1 ] || timeout 30 $PY $T13/emu_exit.py $T13 > /dev/null 2>&1 < /dev/null
