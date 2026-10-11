#!/bin/bash
# FNFC-01.10 probe measurements (repo root, inside `newgrp uucp`).
# For each <label>=<commit>[:<probes>]: detached checkout, apply the probes, flash, check the device
# runs that commit, run series.sh, revert. Returns to the original branch at the end.
#   probes (comma separated, default "dwt,ticks"): dwt = 01.3 latency probe v2 (rebased),
#   ticks = tick_probe.patch, bias<ppm> = bias_probe.patch with ISO15693_SIGNAL_BIAS_PPM=<ppm>
# series.sh options are passed through the environment (DWT, N32, N64, N80); LOG=1 is set when
# the build has a probe that logs.
# usage: measure.sh <outdir> before=402c24f50 after=HEAD:dwt,ticks bias+100=HEAD:ticks,bias100
set -u
W=$(git rev-parse --show-toplevel); cd "$W"
SRC=$(dirname "$(readlink -f "$0")"); OUTDIR=$(readlink -f "$1"); shift
P13=$(readlink -f "$SRC/../../2026-10-11-fnfc013")
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "tree not clean, abort"; exit 1; }
TMP=$(mktemp -d)
[ -d "$P13/tools/pb" ] || bash "$P13/tools/make_pb.sh" || exit 1
cp -r "$SRC" "$TMP/tools"; cp -r "$P13/tools" "$TMP/tools13"; cp "$SRC/../tick_probe.patch" "$SRC/../bias_probe.patch" "$P13/latency_probe_v2_rebased.patch" "$TMP/"
mkdir -p "$OUTDIR"
BRANCH=$(git rev-parse --abbrev-ref HEAD)
FLIP=/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00
PY=toolchain/current/bin/python3
finish() { git checkout -q -- . 2>/dev/null; git checkout -q "$BRANCH"; }
trap finish EXIT
declare -A HASH
for spec in "$@"; do r=${spec#*=}; r=${r%%:*}; HASH[$r]=$(git rev-parse --verify "$r^{commit}") || exit 1; done
trap 'exit 130' INT TERM
for spec in "$@"; do
  L=${spec%%=*}; REF=${spec#*=}; PROBES=dwt,ticks
  case "$REF" in *:*) PROBES=${REF#*:}; REF=${REF%%:*};; esac
  REF=${HASH[$REF]}
  git checkout -q --detach "$REF" || exit 1
  XDEF=""; LOGP=0
  for p in ${PROBES//,/ }; do
    case $p in
      dwt) git apply "$TMP/latency_probe_v2_rebased.patch" || exit 1; XDEF="$XDEF --extra-define=NFC_LISTENER_TX_LATENCY_PROBE"; LOGP=1;;
      ticks) git apply "$TMP/tick_probe.patch" || exit 1; XDEF="$XDEF --extra-define=DIGITAL_SEQUENCE_TICK_PROBE"; LOGP=1;;
      bias*) git apply "$TMP/bias_probe.patch" || exit 1; XDEF="$XDEF --extra-define=ISO15693_SIGNAL_BIAS_PPM=${p#bias}";;
      none) ;;
      *) echo "unknown probe $p"; exit 1;;
    esac
  done
  { echo "== $L ($REF) probes: $PROBES defines:$XDEF"; git diff --stat; } | tee "$OUTDIR/${L}_build.txt"
  ./fbt DEBUG=0 $XDEF FLIP_PORT=$FLIP flash_usb_full > "$OUTDIR/${L}_flash.log" 2>&1 < /dev/null
  rc=$?; git checkout -q -- .
  [ $rc -ne 0 ] && { echo "flash failed ($L), see $OUTDIR/${L}_flash.log"; exit 2; }
  sleep 20; for t in $(seq 1 60); do [ -e "$FLIP" ] && break; sleep 5; done; sleep 10
  for t in 1 2 3 4 5 6; do
    timeout 30 $PY "$TMP/tools13/flcli.py" "info device" > "$OUTDIR/${L}_device_info.txt" 2>&1 < /dev/null
    grep -aq "firmware.commit.hash" "$OUTDIR/${L}_device_info.txt" && break; sleep 10
  done
  H=$(grep -a "firmware.commit.hash" "$OUTDIR/${L}_device_info.txt" | tr -d "\r" | awk '{print $3}')
  echo "device runs $H (dirty: $(grep -a 'commit.dirty' "$OUTDIR/${L}_device_info.txt" | tr -d '\r' | awk '{print $3}'))"
  case "$REF" in "${H:-none}"*) ;; *) echo "device commit $H != $REF, abort"; exit 4;; esac
  T13="$TMP/tools13" LOG=$LOGP bash "$TMP/tools/series.sh" "$OUTDIR" "$L" < /dev/null || { echo "series failed ($L)"; exit 3; }
done
