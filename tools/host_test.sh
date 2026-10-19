#!/bin/sh
# Builds + runs the portable logic tests on a normal PC (no Quest SDK needed). Needs g++ and python3.
set -e
cd "$(dirname "$0")/.."
FLAGS="-std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined"
g++ $FLAGS tests/test_shared.cpp shared/sfq_shared.cpp -o /tmp/sfq_test
/tmp/sfq_test

# PC .saber reader: build a synthetic Windows-style bundle, then parse/convert it.
python3 tools/make_test_bundle.py /tmp/sfq_pc_test
g++ $FLAGS tests/test_pc.cpp shared/unity_bundle.cpp shared/pc_saber.cpp -o /tmp/sfq_test_pc
/tmp/sfq_test_pc /tmp/sfq_pc_test
