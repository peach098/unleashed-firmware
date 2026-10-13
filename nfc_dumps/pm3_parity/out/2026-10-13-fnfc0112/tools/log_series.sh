#!/bin/bash
# FNFC-01.12: N x hf 15 dump on the running emulation while a CLI `log info` session is attached (no probe).
# usage: log_series.sh <outdir> <label> <N> [first]
set -u
OUT=$(readlink -f "$1"); L="$2"; N="$3"; S=${4:-1}; HERE=$(dirname "$(readlink -f "$0")")
T11=$(readlink -f nfc_dumps/pm3_parity/out/2026-10-12-fnfc0111/tools); PY=toolchain/current/bin/python3
STOP="$OUT/.${L}_logstop"; rm -f "$STOP"
timeout 3700 $PY $T11/logcap_until.py "$OUT/${L}_log_$S.txt" "$STOP" 3600 > /dev/null 2>&1 < /dev/null &
LP=$!; sleep 3
bash $HERE/dumps.sh "$OUT" "$L" "$N" "$S" < /dev/null
sleep 1; touch "$STOP"; wait $LP; rm -f "$STOP"
