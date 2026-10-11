#!/bin/bash
# FNFC-01.10 PM3 series on the flashed firmware (repo root, inside `newgrp uucp`).
# Emulates card AC (RPC), optionally captures `log info` ([PROBE]/[TICKS] lines), checks coupling,
# then runs the requested reads; every read is a separate `hf 15 raw` with its own trace.
# usage: series.sh <outdir> <label>
# env:   DWT=1     also the 01.3 DWT set: rd1/rdm8/rdm32/rdm64/rdm80 x3 (names as dwt_run.sh, for pm3delay.py)
#        N32 N64 N80   repeats of the 32/64/80-block one-shot reads (default 5/0/5)
#        LOG=1     capture `log info` (probe builds)
OUT="$1"; L="$2"; T=$(dirname "$(readlink -f "$0")"); T13=${T13:-$(readlink -f "$T/../../2026-10-11-fnfc013/tools")}
PY=toolchain/current/bin/python3
PM3=${PM3:-~/proxmark3/pm3}; PORT=/dev/serial/by-id/usb-proxmark.org_proxmark3_iceman-if00
mkdir -p "$OUT"; OUT=$(readlink -f "$OUT")   # PM3 resolves relative trace paths against $HOME
[ -d "$T13/pb" ] || bash "$T13/make_pb.sh" || exit 1
timeout 40 $PY $T13/emu_open.py $T13 > "$OUT/${L}_emu_open.txt" 2>&1 < /dev/null
sleep 1
LP=
if [ "${LOG:-0}" = 1 ]; then
  STOP="$OUT/.${L}_logstop"; rm -f "$STOP"
  timeout 1300 $PY $T/logcap_until.py "$OUT/${L}_probe_log.txt" "$STOP" 1200 > /dev/null 2>&1 < /dev/null &
  LP=$!; sleep 3
fi
timeout 40 $PM3 -p $PORT --incognito -f -c "hf 15 reader" > "$OUT/${L}_coupling_reader.log" 2>&1 < /dev/null
if ! grep -aq "E0 04 01 09 01 9A 83 AC" "$OUT/${L}_coupling_reader.log"; then
  echo "COUPLING FAIL (see $OUT/${L}_coupling_reader.log)"; [ -n "$LP" ] && { touch "$STOP"; wait $LP; }; exit 3
fi
echo coupling OK
U=AC839A01090104E0
read1() {  # <basename> <hex request>
  rm -f "$1.trace"
  # `trace clear` only clears the client copy: `data clear` empties the device trace (BigBuf), otherwise
  # the device trace fills up after ~30 KB and later frames are silently not recorded
  timeout 60 $PM3 -p $PORT --incognito -f -c "data clear; trace clear; hf 15 raw -a -c -d $2; trace list -t 15; trace save -f $1" > "$1.log" 2>&1 < /dev/null
  echo "$(basename $1): $(grep -aoE '\([0-9]+\) [0-9A-F ]{0,17}|command failed|timeout|no response' "$1.log" | tail -1)"
  sleep 1
}
if [ "${DWT:-0}" = 1 ]; then
  for spec in rd1=2220${U}00 rdm8=2223${U}0007 rdm32=2223${U}001F rdm64=2223${U}003F rdm80=2223${U}004F; do
    for i in 1 2 3; do read1 "$OUT/${L}_${spec%%=*}_${i}" "${spec#*=}"; done
  done
fi
for i in $(seq 1 ${N32:-5}); do read1 "$OUT/${L}_s32_$i" 2223${U}001F; done
for i in $(seq 1 ${N64:-0}); do read1 "$OUT/${L}_s64_$i" 2223${U}003F; done
for i in $(seq 1 ${N80:-5}); do read1 "$OUT/${L}_s80_$i" 2223${U}004F; done
[ -n "$LP" ] && { touch "$STOP"; wait $LP; rm -f "$STOP"; }
timeout 30 $PY $T13/emu_exit.py $T13 > /dev/null 2>&1 < /dev/null
echo "series $L done"
