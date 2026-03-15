#!/bin/bash
# QEMU VM Runner Script
# Launches the QEMU VM for kernel module testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QEMU_DIR="$SCRIPT_DIR/qemu-env"
UBUNTU_VERSION="24.04"

# Resolve QEMU binary (works across apt and snap installs)
QEMU_BIN="qemu-system-x86_64"
if ! command -v "$QEMU_BIN" >/dev/null 2>&1; then
  for candidate in \
    /usr/bin/qemu-system-x86_64 \
    /usr/local/bin/qemu-system-x86_64 \
    /snap/bin/qemu-system-x86_64; do
    if [ -x "$candidate" ]; then
      QEMU_BIN="$candidate"
      break
    fi
  done
fi
if ! command -v "$QEMU_BIN" >/dev/null 2>&1 && [ ! -x "$QEMU_BIN" ]; then
  echo "ERROR: qemu-system-x86_64 not found in PATH."
  echo "Run sudo ./e2e/qemu-setup.sh to provision host dependencies first."
  echo "If QEMU is installed via snap, ensure /snap/bin is in PATH."
  exit 1
fi

# Check if setup was run
if [ ! -f "$QEMU_DIR/ubuntu-${UBUNTU_VERSION}.img" ] || [ ! -f "$QEMU_DIR/seed.img" ]; then
  echo "ERROR: QEMU environment is incomplete or not set up."
  echo "Run: sudo ./e2e/qemu-setup.sh first"
  exit 1
fi

cd "$QEMU_DIR"

echo "==================================================="
echo "Starting QEMU VM for Kernel Module Testing"
echo "==================================================="
echo ""
echo "VM will start with:"
echo "  - 2GB RAM"
echo "  - 2 CPU cores"
echo "  - SSH accessible at localhost:2222"
echo ""
echo "Login credentials:"
echo "  - Username: ubuntu"
echo "  - Password: ubuntu"
echo ""
echo "To copy files to VM:"
echo "  scp -P 2222 file ubuntu@localhost:~/"
echo ""
echo "To SSH into VM:"
echo "  ssh -p 2222 ubuntu@localhost"
echo "  (Uses SSH keys if configured, otherwise password: ubuntu)"
echo ""
echo "Press Ctrl+A then X to exit QEMU"
echo "==================================================="
echo ""

# Wait a moment for user to read
sleep 2

# Determine acceleration options and CPU model
ACCEL_ARGS=( -accel tcg )
CPU_MODEL="qemu64"
if [ -e /dev/kvm ] && [ -w /dev/kvm ]; then
  ACCEL_ARGS=( -accel kvm -accel tcg )
  CPU_MODEL="host"
fi

# Use virtio disk for the Ubuntu image; cloud image is qcow2
DISK_IMG="ubuntu-${UBUNTU_VERSION}.img"
SEED_IMG="seed.img"

# Run QEMU with appropriate settings
"$QEMU_BIN" \
  -name "kernel-module-test" \
  -m 2048 \
  -smp 2 \
  -cpu "$CPU_MODEL" \
  "${ACCEL_ARGS[@]}" \
  -drive file="${DISK_IMG}",if=virtio,format=qcow2 \
  -cdrom "${SEED_IMG}" \
  -device e1000,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -serial mon:stdio \
  -display none \
  "$@"
