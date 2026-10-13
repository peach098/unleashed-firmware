#!/bin/bash
# FNFC-01.14: flash the current tree (release, DEBUG=0) and wait for the Flipper USB to come back.
# usage: flash.sh <outdir> <label> [extra fbt args, e.g. --extra-define=NFC_1OF256_PROBE]   (repo root, inside `newgrp uucp`)
OUT=$(readlink -f "$1"); L="$2"; shift 2
T13=$(readlink -f nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools); PY=toolchain/current/bin/python3
FLIP=/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00
timeout 30 $PY $T13/emu_exit.py $T13 > /dev/null 2>&1 < /dev/null
{ git log --oneline -1; git status --short --untracked-files=no; echo "fbt args: $*"; } > "$OUT/${L}_build.txt"
rm -f build/usbinstall.flag
./fbt DEBUG=0 FLIP_PORT=$FLIP "$@" flash_usb_full > "$OUT/${L}_flash.log" 2>&1 < /dev/null || { echo "flash failed"; tail -20 "$OUT/${L}_flash.log"; exit 1; }
sleep 20; for t in $(seq 1 60); do [ -e "$FLIP" ] && break; sleep 5; done; sleep 10
for t in 1 2 3 4 5 6; do
  timeout 30 $PY $T13/flcli.py "info device" > "$OUT/${L}_device_info.txt" 2>&1 < /dev/null; grep -aq "firmware.commit.hash" "$OUT/${L}_device_info.txt" && break; sleep 10
done
grep -aE "commit.hash|commit.dirty" "$OUT/${L}_device_info.txt" | tr -d '\r'
