#!/bin/sh
set -e

echo "=== Building POSIX Log Aggregator & Producers ==="
make clean
make all

echo ""
echo "=== Executing Test Suite ==="
./bin/test_suite

echo ""
echo "=== SUCCESS: All POSIX System Tests Passed Cleanly ==="
