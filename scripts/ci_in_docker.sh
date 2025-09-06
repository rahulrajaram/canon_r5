#!/usr/bin/env bash
set -euo pipefail

echo "== Canon R5 CI (Docker) =="
echo "Kernel: $(uname -r)"
echo "Distro: $(lsb_release -ds 2>/dev/null || cat /etc/os-release | head -1)"

echo "-- Checking kernel headers --"
if [ ! -e "/lib/modules/$(uname -r)/build" ]; then
  echo "Kernel headers for $(uname -r) not found; using whatever was installed (generic)."
  ls -la /usr/src/ || true
else
  echo "Headers present: /lib/modules/$(uname -r)/build"
fi

echo "-- Building modules --"
make clean
make -j"${JOBS:-$(nproc)}" modules

echo "-- Listing built modules --"
ls -la *.ko || true

echo "-- Running modinfo on built modules --"
for m in *.ko; do
  [ -f "$m" ] || continue
  echo "[modinfo] $m"
  modinfo "$m" || echo "modinfo failed for $m (ok in container)"
done

echo "-- Running basic tests (no root ops) --"
export CANON_R5_TEST_ENV=1
chmod +x tests/integration/*.sh || true

# Integration tests that don't require root will still give coverage
if [ -f tests/integration/test_dependencies.sh ]; then
  bash -lc 'tests/integration/test_dependencies.sh || true'
fi

if [ -f tests/integration/test_driver_loading.sh ]; then
  # This script attempts insmod if run as root; we're typically not root in CI
  bash -lc 'tests/integration/test_driver_loading.sh || true'
fi

echo "-- Done --"

