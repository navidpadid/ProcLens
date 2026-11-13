#!/bin/bash
# E2E tests via Docker — no kernel module needed
set -e
cd "$(dirname "$0")"

echo "Building Docker image..."
docker build -f Dockerfile.e2e -t proclens-e2e .

echo ""
echo "=== Test 1: One-shot with PID 1 ==="
docker run --rm proclens-e2e ./build/proclens 1 2>&1 | head -80

echo ""
echo "=== Test 2: One-shot with self ==="
docker run --rm proclens-e2e bash -c './build/proclens $$ 2>&1 | head -80'

echo ""
echo "=== Test 3: Multi-threaded ==="
docker run --rm proclens-e2e bash -c '
  ./build/test_multithread &
  TPID=$!
  sleep 1
  echo "Testing PID $TPID"
  ./build/proclens $TPID 2>&1 | head -100
  wait $TPID
' 2>&1 | head -120

echo ""
echo "=== Test 4: Error handling ==="
docker run --rm proclens-e2e ./build/proclens 99999 2>&1

echo ""
echo "=== Test 5: Help/Version ==="
docker run --rm proclens-e2e ./build/proclens --help 2>&1 | head -10
docker run --rm proclens-e2e ./build/proclens --version 2>&1

echo ""
echo "All E2E tests done"
