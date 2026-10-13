#!/bin/bash
# FNFC-01.14 host test of the ISO15693 request parser (run from the repo root, needs gcc).
# Compiles the tree's lib/signal_reader/parsers/iso15693/iso15693_parser.c and lib/toolbox/bit_buffer.c
# unchanged against the stubs in stubs/ (signal_reader is stubbed in parser_test.c) and runs the test.
# usage: run.sh [output-file]   (default: parser_test.txt next to this script)
#        LIB=<dir> run.sh ...   use another copy of lib/ (e.g. the code before the fix)
H=$(dirname "$(readlink -f "$0")"); B=$(mktemp -d); L=${LIB:-$(pwd)/lib}
OUTF=${1:-$H/parser_test.txt}
gcc -O1 -g -Wall -Wextra -Wno-unused-parameter -fsanitize=address,undefined -I "$H/stubs" -I "$L" \
  "$H/parser_test.c" "$L/signal_reader/parsers/iso15693/iso15693_parser.c" "$L/toolbox/bit_buffer.c" \
  -o "$B/parser_test" || exit 2
"$B/parser_test" > "$OUTF" 2>&1; rc=$?
cat "$OUTF" | tail -40
rm -rf "$B"
exit $rc
