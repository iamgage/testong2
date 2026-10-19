#!/bin/sh
# Builds + runs the portable logic tests on a normal PC (no Quest SDK needed).
set -e
cd "$(dirname "$0")/.."
g++ -std=c++17 -Wall -Wextra -O1 -fsanitize=address,undefined tests/test_shared.cpp shared/sfq_shared.cpp -o /tmp/sfq_test
/tmp/sfq_test
g++ -std=c++17 -Wall -Wextra -O1 tests/test_bundle.cpp shared/saber_bundle.cpp -o /tmp/sfq_bundle_test
/tmp/sfq_bundle_test
