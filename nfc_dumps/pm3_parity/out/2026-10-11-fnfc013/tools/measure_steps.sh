#!/bin/bash
# FNFC-01.3 per-step DWT + PM3 measurement (run from the repo root, inside `newgrp uucp`).
# For each <label>=<commit>[:<patch>]: check out the commit (detached), apply a DWT probe, flash it,
# check that the device runs that commit, run dwt_run.sh, revert the probe.
# Patch: v2 rebased (01.3 commits), else original v2 (baseline), else v2 rebased with -C2
# (steps 1/2: only the context of the digital_sequence.c hunk differs). ":v3" selects latency_probe_v3.patch.
# Returns to the original branch at the end (also on error / Ctrl-C).
# usage: measure_steps.sh baseline=e0ff6c791 step1=2cc4be797 step2=24a966882 step3=effe31b99 [step4=814b9ee38:v3]
set -u
W=$(git rev-parse --show-toplevel); cd "$W"
SRC=$(dirname "$(readlink -f "$0")")
OUT=$(dirname "$SRC")
[ -d "$SRC/pb" ] || bash "$SRC/make_pb.sh" || exit 1
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "tree not clean, abort"; exit 1; }
TMP=$(mktemp -d)                     # older commits do not contain this directory
cp -r "$SRC" "$TMP/tools"; cp "$OUT/latency_probe_v2_rebased.patch" "$OUT/latency_probe_v3.patch" "$TMP/"
cp nfc_dumps/pm3_parity/out/2026-10-11-fnfc012/dwt/latency_probe_v2.patch "$TMP/"
cp "nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc" "$TMP/dump.nfc"
cp nfc_dumps/pm3_parity/pm3_matrix.py "$TMP/"; sed -i 's#"..", "..", ".."#".."#' "$TMP/tools/pm3delay.py"
RES="$TMP/results"; mkdir -p "$RES"
BRANCH=$(git rev-parse --abbrev-ref HEAD)
FLIP=/dev/serial/by-id/usb-Flipper_Devices_Inc._Flipper_E17ana_flip_E17ana-if00
PY=toolchain/current/bin/python3
finish() {
  git checkout -q -- . 2>/dev/null
  git checkout -q "$BRANCH"
  cp -r "$RES"/. "$OUT/"
  echo "results copied to $OUT (temp: $TMP)"
}
trap finish EXIT
trap 'exit 130' INT TERM
for spec in "$@"; do
  L=${spec%%=*}; REF=${spec#*=}; KIND=v2; XDEF=""
  case "$REF" in *:v3) REF=${REF%:v3}; KIND=v3; XDEF="--extra-define=NFC_LISTENER_TX_LATENCY_PROBE_V3";; esac
  git checkout -q --detach "$REF" || exit 1
  if [ $KIND = v3 ]; then
    git apply "$TMP/latency_probe_v3.patch" || exit 1; P=latency_probe_v3.patch
  elif git apply --check "$TMP/latency_probe_v2_rebased.patch" 2>/dev/null; then
    git apply "$TMP/latency_probe_v2_rebased.patch"; P=latency_probe_v2_rebased.patch
  elif git apply --check "$TMP/latency_probe_v2.patch" 2>/dev/null; then
    git apply "$TMP/latency_probe_v2.patch"; P=latency_probe_v2.patch
  else
    git apply -C2 "$TMP/latency_probe_v2_rebased.patch" || exit 1; P="latency_probe_v2_rebased.patch -C2"
  fi
  echo "== $L ($REF) probe $P" | tee "$RES/${L}_build.txt"
  git diff --stat >> "$RES/${L}_build.txt"
  ./fbt DEBUG=0 --extra-define=NFC_LISTENER_TX_LATENCY_PROBE $XDEF FLIP_PORT=$FLIP flash_usb_full > "$RES/${L}_flash.log" 2>&1
  rc=$?; git checkout -q -- .
  [ $rc -ne 0 ] && { echo "flash failed ($L), see $RES/${L}_flash.log"; exit 2; }
  # the updater re-installs resources: USB is gone for ~3.5 min (a fixed 45 s sleep was too short)
  sleep 20
  for t in $(seq 1 60); do [ -e "$FLIP" ] && break; sleep 5; done
  sleep 10
  for t in 1 2 3 4 5 6; do
    timeout 30 $PY "$TMP/tools/flcli.py" "info device" > "$RES/${L}_device_info.txt" 2>&1
    grep -aq "firmware.commit.hash" "$RES/${L}_device_info.txt" && break; sleep 10
  done
  H=$(grep -a "firmware.commit.hash" "$RES/${L}_device_info.txt" | tr -d "\r" | awk '{print $3}')  # CLI lines end in \r
  echo "device runs $H (dirty: $(grep -a 'commit.dirty' "$RES/${L}_device_info.txt" | tr -d '\r' | awk '{print $3}'))"
  case "$(git rev-parse "$REF")" in "${H:-none}"*) ;; *) echo "device commit $H != $REF, abort"; exit 4;; esac
  bash "$TMP/tools/dwt_run.sh" "$RES" "$L" || { echo "dwt_run failed ($L)"; exit 3; }
done
