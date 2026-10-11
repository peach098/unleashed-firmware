#!/bin/bash
# FNFC-01.10 host tick-arithmetic test (run from the repo root, needs gcc). Compiles the tree's
# lib/digital_signal sources unchanged against the stubs in stubs/ and writes tick_model.txt.
# usage: run.sh [output-file]   (default: tick_model.txt next to this script)
#        run.sh --frame "<hex bytes>"   ticks of one ISO15693 high-rate frame
#        LIB=<dir> run.sh ...           use another copy of lib/ (e.g. the code before the fix)
H=$(dirname "$(readlink -f "$0")"); B=$(mktemp -d); L=${LIB:-$(pwd)/lib}
OUTF=${1:-$H/tick_model.txt}
gcc -O2 -w -I "$H/stubs" -I "$H" -I "$L" "$H/tick_model.c" -lm -o "$B/tick_model" || exit 2
[ "${1:-}" = --frame ] && { "$B/tick_model" --frame "$2"; exit; }
"$B/tick_model" > "$OUTF"; rc=$?
grep -E "^(merge|floor|iso15693:|iso14443a:|random:|FAIL|PASSED|FAILED)" "$OUTF"
exit $rc
