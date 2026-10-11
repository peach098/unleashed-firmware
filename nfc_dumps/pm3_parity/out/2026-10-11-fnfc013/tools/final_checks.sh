#!/bin/bash
# FNFC-01.3 acceptance on clean firmware (no probe). Run from the repo root inside `newgrp uucp`.
#  1. flash the current tree (default app set), emulate card AC
#  2. PM3: coupling, 3x one-shot 80-block read (hf 15 raw ...004F) -> (323) == 00 + dump, hf 15 dump == dump
#  3. (skipped with SKIP_UNIT_TESTS=1) flash the unit_tests app set, run `unit_tests test_nfc`, flash the default firmware back
# usage: final_checks.sh <outdir>
set -u
OUT="$1"; T=$(dirname "$(readlink -f "$0")"); mkdir -p "$OUT"; OUT=$(readlink -f "$OUT")   # PM3 resolves relative trace paths against $HOME
PY=toolchain/current/bin/python3; PM3=${PM3:-~/proxmark3/pm3}
PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
FLIP=/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00
DUMP="nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc"
[ -d "$T/pb" ] || bash "$T/make_pb.sh" || exit 1
wait_flipper() {   # flash_usb_full re-installs resources: USB is gone for ~3.5 min
  sleep 20; for t in $(seq 1 60); do [ -e "$FLIP" ] && break; sleep 5; done; sleep 10
  for t in 1 2 3 4 5 6; do
    timeout 30 $PY $T/flcli.py "info device" > "$1" 2>&1; grep -aq "firmware.commit.hash" "$1" && break; sleep 10
  done
  grep -aE "commit.hash|commit.dirty" "$1" | tr -d '\r'
}

./fbt DEBUG=0 FLIP_PORT=$FLIP flash_usb_full > "$OUT/final_flash_default.log" 2>&1 || { echo "flash failed"; exit 1; }
wait_flipper "$OUT/final_device_info.txt"
timeout 40 $PY $T/emu_open.py $T > "$OUT/final_emu_open.txt" 2>&1
sleep 2
timeout 40 $PM3 -p $PORT --incognito -f -c "hf 15 reader" > "$OUT/final_coupling_reader.log" 2>&1
grep -aq "E0 04 01 09 01 9A 83 AC" "$OUT/final_coupling_reader.log" || { echo "COUPLING FAIL"; exit 3; }
for i in 1 2 3; do
  b="$OUT/final_raw_004F_$i"
  timeout 60 $PM3 -p $PORT --incognito -f -c "trace clear; hf 15 raw -a -c -d 2223AC839A01090104E0004F; trace list -t 15; trace save -f $b" > "$b.log" 2>&1
  python3 $T/compare_dump.py "$DUMP" raw "$b.log" | tee "$b.compare.txt"
done
timeout 120 $PM3 -p $PORT --incognito -f -c "hf 15 dump" > "$OUT/final_hf15_dump.log" 2>&1
python3 $T/compare_dump.py "$DUMP" dump "$OUT/final_hf15_dump.log" | tee "$OUT/final_hf15_dump.compare.txt"
timeout 30 $PY $T/emu_exit.py $T
[ "${SKIP_UNIT_TESTS:-0}" = 1 ] && { echo "unit_tests skipped (SKIP_UNIT_TESTS=1)"; exit 0; }

./fbt FIRMWARE_APP_SET=unit_tests DEBUG=0 FLIP_PORT=$FLIP flash_usb_full > "$OUT/final_flash_unit_tests.log" 2>&1 || { echo "unit_tests flash failed"; exit 1; }
wait_flipper "$OUT/final_unit_tests_device_info.txt"
timeout 400 $PY $T/unit_tests_run.py "unit_tests test_nfc" "$OUT/final_unit_tests_nfc.log"
tail -6 "$OUT/final_unit_tests_nfc.log"
grep -a "NfcIso15693SignalTest" "$OUT/final_unit_tests_nfc.log"
./fbt DEBUG=0 FLIP_PORT=$FLIP flash_usb_full > "$OUT/final_flash_default_restore.log" 2>&1 && echo "default firmware restored"
