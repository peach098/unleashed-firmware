#!/bin/bash
# Compile the Flipper RPC protobufs for rpc.py into tools/pb (run from the repo root).
set -e
T=$(dirname "$(readlink -f "$0")")
mkdir -p "$T/pb"
toolchain/current/bin/protoc -I assets/protobuf --python_out="$T/pb" assets/protobuf/*.proto
