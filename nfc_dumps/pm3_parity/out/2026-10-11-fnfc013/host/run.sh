#!/bin/bash
# Host-side checks of the FNFC-01.3 code (run from the repo root, needs gcc).
# The firmware sources are compiled unchanged against stub headers in stubs/.
set -e
H=$(dirname "$(readlink -f "$0")"); B=$(mktemp -d); L=$(pwd)/lib
gcc -O2 -w -I "$H/stubs" -I "$L" "$H/encode_equiv.c" -o "$B/encode_equiv"
gcc -O2 -w -I "$H/stubs" -I "$L" "$H/crc_equiv.c" -o "$B/crc_equiv"
gcc -O2 -w -I "$H/stubs" -I "$L" "$H/rdm80_frame.c" -o "$B/rdm80_frame"
"$B/encode_equiv" > "$H/encode_equiv.txt"; tail -1 "$H/encode_equiv.txt"
"$B/crc_equiv" > "$H/crc_equiv.txt"; cat "$H/crc_equiv.txt"
"$B/rdm80_frame" "$(grep '^Data Content:' 'nfc_dumps/SLIX2_2026-02-27_16,08,34.nfc' | cut -d: -f2)" \
    "$(grep -a -o '(323) [0-9A-F ]*' nfc_dumps/pm3_parity/out/2026-10-11/card_AC_rdm_00_4F_1.log | head -1 | cut -c7-)" \
    | tee "$H/rdm80_frame.txt"
