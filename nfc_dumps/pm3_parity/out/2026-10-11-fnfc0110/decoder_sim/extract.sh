#!/bin/bash
# Extract the PM3 ISO15693 tag-answer decoder (reader mode) verbatim from the proxmark3 source.
# usage: extract.sh <proxmark3 dir> > pm3_decoder.inc
F="$1/armsrc/iso15693.c"
awk '/^#define NOISE_THRESHOLD/{p=1} p{print} /^static void DecodeTagReset/{exit}' "$F" | sed '$d'
