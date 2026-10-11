#!/bin/bash
# FNFC-01.10 acceptance on clean firmware (no probe). Repo root, inside `newgrp uucp`.
#  1. (unless SKIP_FLASH=1) flash the current tree, default app set
#  2. SLIX2 card AC: coupling, N80 x 004F, N64 x 003F, N32 x 001F one-shot reads, NDUMP x hf 15 dump,
#     each compared with the dump
#  3. ISO14443-A regression: emulate a MIFARE Classic 1K file from the Flipper SD, PM3 `hf 14a info` x3,
#     `hf mf rdbl --blk 0` and `hf mf rdsc -s 0` with key A FFFFFFFFFFFF
# usage: final.sh <outdir> <label>
set -u
OUT="$1"; L="$2"; T=$(dirname "$(readlink -f "$0")"); T13=${T13:-$(readlink -f "$T/../../2026-10-11-fnfc013/tools")}
mkdir -p "$OUT"; OUT=$(readlink -f "$OUT")
PY=toolchain/current/bin/python3; PM3=${PM3:-~/proxmark3/pm3}
PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
FLIP=/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00
DUMP="nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc"
MFC=${MFC:-/ext/nfc/MFC_1K_2026-03-23_21,06,03.nfc}
[ -d "$T13/pb" ] || bash "$T13/make_pb.sh" || exit 1
if [ "${SKIP_FLASH:-0}" != 1 ]; then
  git log --oneline -1 > "$OUT/${L}_build.txt"; git status --short --untracked-files=no >> "$OUT/${L}_build.txt"
  ./fbt DEBUG=0 FLIP_PORT=$FLIP flash_usb_full > "$OUT/${L}_flash.log" 2>&1 < /dev/null || { echo "flash failed"; exit 1; }
  sleep 20; for t in $(seq 1 60); do [ -e "$FLIP" ] && break; sleep 5; done; sleep 10
fi
for t in 1 2 3 4 5 6; do
  timeout 30 $PY $T13/flcli.py "info device" > "$OUT/${L}_device_info.txt" 2>&1 < /dev/null; grep -aq "firmware.commit.hash" "$OUT/${L}_device_info.txt" && break; sleep 10
done
grep -aE "commit.hash|commit.dirty" "$OUT/${L}_device_info.txt" | tr -d '\r'
N32=${N32:-5} N64=${N64:-10} N80=${N80:-10} LOG=0 T13=$T13 bash $T/series.sh "$OUT" "$L" < /dev/null || exit 3
for i in $(seq 1 ${N80:-10}); do python3 $T13/compare_dump.py "$DUMP" raw "$OUT/${L}_s80_$i.log" > "$OUT/${L}_s80_$i.compare.txt"; done
grep -h . "$OUT/${L}"_s80_*.compare.txt | sort | uniq -c
# hf 15 dump (emulation restarted)
timeout 40 $PY $T13/emu_open.py $T13 > "$OUT/${L}_dump_emu_open.txt" 2>&1 < /dev/null; sleep 2
for i in $(seq 1 ${NDUMP:-3}); do
  timeout 180 $PM3 -p $PORT --incognito -f -c "hf 15 dump" > "$OUT/${L}_hf15_dump_$i.log" 2>&1 < /dev/null
  python3 $T13/compare_dump.py "$DUMP" dump "$OUT/${L}_hf15_dump_$i.log" | tee "$OUT/${L}_hf15_dump_$i.compare.txt"
done
timeout 30 $PY $T13/emu_exit.py $T13 > /dev/null 2>&1 < /dev/null
T13=$T13 MFC=$MFC bash $T/iso14a.sh "$OUT" "$L" < /dev/null
echo "final $L done"
