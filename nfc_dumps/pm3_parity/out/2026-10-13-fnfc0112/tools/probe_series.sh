#!/bin/bash
# FNFC-01.12: emulate card AC, capture `log info` ([P12] lines) while running N x `hf 15 dump` (dumps.sh).
# Repo root, inside `newgrp uucp`, probe firmware flashed. usage: probe_series.sh <outdir> <label> <N> [first]
set -u
OUT=$(readlink -f "$1"); L="$2"; N="$3"; S=${4:-1}; HERE=$(dirname "$(readlink -f "$0")")
T13=$(readlink -f nfc_dumps/pm3_parity/out/2026-10-11-fnfc013/tools); T11=$(readlink -f nfc_dumps/pm3_parity/out/2026-10-12-fnfc0111/tools)
PY=toolchain/current/bin/python3; PM3=${PM3:-~/proxmark3/pm3}; PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
timeout 40 $PY $T13/emu_open.py $T13 > "$OUT/${L}_emu_open.txt" 2>&1 < /dev/null; sleep 1
STOP="$OUT/.${L}_logstop"; rm -f "$STOP"
timeout 3700 $PY $T11/logcap_until.py "$OUT/${L}_probe_log_$S.txt" "$STOP" 3600 > /dev/null 2>&1 < /dev/null &
LP=$!; sleep 3
timeout 40 $PM3 -p $PORT --incognito -f -c "hf 15 reader" > "$OUT/${L}_coupling_reader.log" 2>&1 < /dev/null
grep -aq "E0 04 01 09 01 9A 83 AC" "$OUT/${L}_coupling_reader.log" && echo coupling OK || echo "COUPLING FAIL"
bash $HERE/dumps.sh "$OUT" "$L" "$N" "$S" < /dev/null
sleep 2; touch "$STOP"; wait $LP; rm -f "$STOP"
echo "[P12] lines: $(grep -ac '\[P12\]' "$OUT/${L}_probe_log_$S.txt")"
