#!/bin/bash
# FNFC-01.10 ISO14443-A regression on the flashed firmware (repo root, inside `newgrp uucp`): emulate a
# MIFARE Classic 1K file from the Flipper SD, PM3 `hf 14a info` x3, `hf mf rdbl --blk 0` and
# `hf mf rdsc -s 0` with key A FFFFFFFFFFFF.
# usage: iso14a.sh <outdir> <label>     env: MFC=</ext/nfc/...nfc>
OUT=$(readlink -f "$1"); L="$2"; T=$(dirname "$(readlink -f "$0")"); T13=${T13:-$(readlink -f "$T/../../2026-10-11-fnfc013/tools")}
PY=toolchain/current/bin/python3; PM3=${PM3:-~/proxmark3/pm3}
PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
MFC=${MFC:-/ext/nfc/MFC_1K_2026-03-23_21,06,03.nfc}
timeout 40 $PY $T/emu_open_file.py $T13 "$MFC" > "$OUT/${L}_mfc_emu_open.txt" 2>&1 < /dev/null; sleep 2
for i in 1 2 3; do
  timeout 60 $PM3 -p $PORT --incognito -f -c "hf 14a info" > "$OUT/${L}_14a_info_$i.log" 2>&1 < /dev/null
  echo "hf 14a info #$i: $(grep -aE 'UID:' "$OUT/${L}_14a_info_$i.log" | head -1 | tr -s ' ')"
done
timeout 60 $PM3 -p $PORT --incognito -f -c "hf mf rdbl --blk 0 -k FFFFFFFFFFFF" > "$OUT/${L}_mf_rdbl0.log" 2>&1 < /dev/null
timeout 60 $PM3 -p $PORT --incognito -f -c "hf mf rdsc -s 0 -k FFFFFFFFFFFF" > "$OUT/${L}_mf_rdsc0.log" 2>&1 < /dev/null
grep -aE '^\[=\] +[0-9]+ \|' "$OUT/${L}_mf_rdbl0.log" "$OUT/${L}_mf_rdsc0.log" | head -8
timeout 30 $PY $T13/emu_exit.py $T13 > /dev/null 2>&1 < /dev/null
