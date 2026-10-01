#!/usr/bin/env bash
# Host-side (PC) tests of the Chronos detection logic and state machine.
# Compiles the real firmware file against tiny Arduino/RTClib stubs and drives it with a scripted
# fake GPS + RTC. Needs only g++ (C++17). It does NOT test wiring, timing on the ESP32, or real GPS.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
CXX="${CXX:-g++}"
FLAGS="-std=c++17 -Wall -Wextra -Wno-unused-function -I$HERE/stubs -x c++"

echo "=== Variant 1: HAS_RELAY = false (default) ==="
$CXX $FLAGS "$HERE/harness.cpp" -o "$OUT/h1"
"$OUT/h1" | grep -E "CHECK|----|>>>|checks" 

echo
echo "=== Variant 2: HAS_RELAY = true ==="
sed 's/HAS_RELAY         = false/HAS_RELAY         = true/' "$HERE/../../firmware/chronos/chronos.ino" > "$OUT/chronos_relay.ino"
$CXX $FLAGS -DRELAY_VARIANT -DFIRMWARE_PATH="\"$OUT/chronos_relay.ino\"" "$HERE/harness.cpp" -o "$OUT/h2"
"$OUT/h2" | grep -E "CHECK|----|>>>|checks"
echo
echo "ALL HOST TESTS PASSED"
