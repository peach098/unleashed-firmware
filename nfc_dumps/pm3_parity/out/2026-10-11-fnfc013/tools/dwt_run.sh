#!/bin/bash
# One measurement run on the flashed firmware (run from the repo root, inside `newgrp uucp`):
# emulate card AC via RPC, capture [PROBE] lines with `log info`, coupling check,
# then PM3 read single (1 block) and read multiple 8/32/64/80 blocks, 3 repeats each.
# usage: dwt_run.sh <outdir> <label>
OUT="$1"; L="$2"; T=$(dirname "$(readlink -f "$0")")
PY=toolchain/current/bin/python3
PM3=${PM3:-~/proxmark3/pm3}; PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
mkdir -p "$OUT"; OUT=$(readlink -f "$OUT")   # PM3 resolves relative trace paths against $HOME
timeout 40 $PY $T/emu_open.py $T > "$OUT/${L}_emu_open.txt" 2>&1
cp $T/emu_s1.png "$OUT/${L}_emu_screen.png" 2>/dev/null
sleep 1
timeout 400 $PY $T/logcap.py "$OUT/${L}_probe_log.txt" 150 > /dev/null 2>&1 &
LP=$!
sleep 3
timeout 40 $PM3 -p $PORT --incognito -f -c "hf 15 reader" > "$OUT/${L}_coupling_reader.log" 2>&1
if ! grep -aq "E0 04 01 09 01 9A 83 AC" "$OUT/${L}_coupling_reader.log"; then echo "COUPLING FAIL"; kill $LP; exit 3; fi
echo coupling OK
U=AC839A01090104E0
for spec in rd1=2220${U}00 rdm8=2223${U}0007 rdm32=2223${U}001F rdm64=2223${U}003F rdm80=2223${U}004F; do
  n=${spec%%=*}; p=${spec#*=}
  for i in 1 2 3; do
    b="$OUT/${L}_${n}_${i}"
    rm -f "$b.trace"
    timeout 60 $PM3 -p $PORT --incognito -f -c "trace clear; hf 15 raw -a -c -d $p; trace list -t 15; trace save -f $b" > "$b.log" 2>&1
    echo "$n #$i: $(grep -aoE '\([0-9]+\) [0-9A-F ]{0,30}|command failed|timeout' "$b.log" | head -1)"
    sleep 1
  done
done
wait $LP
DUMP=$T/../dump.nfc; [ -f "$DUMP" ] || DUMP="nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc"
$PY $T/pm3delay.py "$OUT" "$L" "$DUMP" > "$OUT/${L}_pm3_sof_delay.txt"
cat "$OUT/${L}_pm3_sof_delay.txt"
timeout 30 $PY $T/emu_exit.py $T
