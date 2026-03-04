#!/bin/bash
# Script to copy and test kernel module in QEMU VM
# Run this on your HOST machine, not inside the VM

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SSH_PORT=2222
SSH_USER=ubuntu
SSH_HOST=localhost
SSH_OPTS="-p $SSH_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
SCP_OPTS="-P $SSH_PORT -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"

echo "==================================================="
echo "Testing Kernel Module in QEMU VM"
echo "==================================================="

# Check if VM is running
if ! ssh $SSH_OPTS -o ConnectTimeout=5 -o BatchMode=yes ${SSH_USER}@${SSH_HOST} exit 2>/dev/null; then
    echo "ERROR: QEMU VM is not running or SSH is not accessible"
    echo "Start the VM first with: ./e2e/qemu-run.sh"
    exit 1
fi

echo "1. Building kernel module locally..."
cd "$PROJECT_ROOT"
make clean
make all
make build-multithread

echo ""
echo "2. Copying files to QEMU VM..."
scp $SCP_OPTS -r build src/Kbuild Makefile ${SSH_USER}@${SSH_HOST}:~/kernel_module/

echo ""
echo "3. Installing and testing module in VM..."
ssh $SSH_OPTS ${SSH_USER}@${SSH_HOST} << 'ENDSSH'
set -e
cd ~/kernel_module

echo "Installing kernel module..."
if lsmod | grep -q '^elf_det'; then
    echo "Module already loaded; unloading first..."
    sudo rmmod elf_det || true
fi
sudo insmod build/elf_det.ko

echo "Checking if module is loaded..."
lsmod | grep elf_det

echo "Checking /proc entries..."
ls -la /proc/elf_det/
echo "Expected files: det, pid, threads"

echo ""
echo "=== Testing Process Information (PID: $$) ==="
echo "$$" | sudo tee /proc/elf_det/pid > /dev/null
PROC_OUT=$(sudo cat /proc/elf_det/det)
echo "$PROC_OUT"
if ! echo "$PROC_OUT" | grep -q "Memory Pressure Statistics"; then
    echo "[FAIL] Memory pressure stats missing for PID $$"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "\[network\]"; then
    echo "[FAIL] Network stats section missing for PID $$"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "sockets_total:"; then
    echo "[FAIL] Network sockets_total missing for PID $$"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "net_devices:"; then
    echo "[FAIL] Network net_devices missing for PID $$"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "Open Sockets"; then
    echo "[FAIL] Open sockets section missing for PID $$"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "Proto:"; then
    echo "[FAIL] Open sockets protocol field missing for PID $$"
    exit 1
fi
if echo "$PROC_OUT" | grep -Eq "Proto: +TCP|Proto: +UDP" && \
   ! echo "$PROC_OUT" | grep -q "Traffic: RX"; then
    echo "[FAIL] Per-socket traffic stats missing for TCP/UDP sockets (PID $$)"
    exit 1
fi

echo ""
echo "=== Testing Thread Information (PID: $$) ==="
sudo cat /proc/elf_det/threads

echo ""
echo "=== Testing with PID 1 (init/systemd) ==="
echo "1" | sudo tee /proc/elf_det/pid > /dev/null
echo "Process info:"
PROC_OUT=$(sudo cat /proc/elf_det/det)
echo "$PROC_OUT"
if ! echo "$PROC_OUT" | grep -q "Memory Pressure Statistics"; then
    echo "[FAIL] Memory pressure stats missing for PID 1"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "\[network\]"; then
    echo "[FAIL] Network stats section missing for PID 1"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "sockets_total:"; then
    echo "[FAIL] Network sockets_total missing for PID 1"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "net_devices:"; then
    echo "[FAIL] Network net_devices missing for PID 1"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "Open Sockets"; then
    echo "[FAIL] Open sockets section missing for PID 1"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "Proto:"; then
    echo "[FAIL] Open sockets protocol field missing for PID 1"
    exit 1
fi
if echo "$PROC_OUT" | grep -Eq "Proto: +TCP|Proto: +UDP" && \
   ! echo "$PROC_OUT" | grep -q "Traffic: RX"; then
    echo "[FAIL] Per-socket traffic stats missing for TCP/UDP sockets (PID 1)"
    exit 1
fi
echo ""
echo "Thread info:"
sudo cat /proc/elf_det/threads

echo ""
echo "=== Testing with user program (proc_elf_ctrl, PID=1) ==="
sudo ./build/proc_elf_ctrl 1 || true

echo ""
echo "=== Testing with multi-threaded application ==="
# Run the multi-threaded program in background
./build/test_multithread &
MULTITHREAD_PID=$!
echo "Started multi-threaded application (PID: $MULTITHREAD_PID)"

# Give threads time to start
sleep 1

# Test with the multi-threaded process
echo ""
echo "Testing thread detection with multi-threaded process:"
echo "$MULTITHREAD_PID" | sudo tee /proc/elf_det/pid > /dev/null
echo ""
echo "Process info:"
PROC_OUT=$(sudo cat /proc/elf_det/det)
echo "$PROC_OUT"
if ! echo "$PROC_OUT" | grep -q "Memory Pressure Statistics"; then
    echo "[FAIL] Memory pressure stats missing for PID $MULTITHREAD_PID"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "\[network\]"; then
    echo "[FAIL] Network stats section missing for PID $MULTITHREAD_PID"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "sockets_total:"; then
    echo "[FAIL] Network sockets_total missing for PID $MULTITHREAD_PID"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "net_devices:"; then
    echo "[FAIL] Network net_devices missing for PID $MULTITHREAD_PID"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "Open Sockets"; then
    echo "[FAIL] Open sockets section missing for PID $MULTITHREAD_PID"
    exit 1
fi
if ! echo "$PROC_OUT" | grep -q "Proto:"; then
    echo "[FAIL] Open sockets protocol field missing for PID $MULTITHREAD_PID"
    exit 1
fi
if echo "$PROC_OUT" | grep -Eq "Proto: +TCP|Proto: +UDP" && \
   ! echo "$PROC_OUT" | grep -q "Traffic: RX"; then
    echo "[FAIL] Per-socket traffic stats missing for TCP/UDP sockets (PID $MULTITHREAD_PID)"
    exit 1
fi
echo ""
echo "Thread info (should show 5 threads: 1 main + 4 workers):"
sudo cat /proc/elf_det/threads

sleep 1

# Use the user program to display formatted output
echo ""
echo "Using proc_elf_ctrl with multi-threaded process:"
sudo ./build/proc_elf_ctrl $MULTITHREAD_PID || true

# Wait for multi-threaded program to finish
wait $MULTITHREAD_PID
echo ""
echo "[PASS] Multi-threaded application test completed"

echo ""
echo "=== Verifying all proc files are accessible ==="
if [ -r /proc/elf_det/det ] && [ -r /proc/elf_det/pid ] && [ -r /proc/elf_det/threads ]; then
    echo "[PASS] All proc files exist and are readable"
else
    echo "[FAIL] Some proc files are missing or not readable"
    exit 1
fi

echo ""
echo "Checking kernel logs..."
sudo dmesg | tail -20

echo ""
echo "Uninstalling module..."
sudo rmmod elf_det

echo ""
echo "Verifying module unloaded..."
lsmod | grep elf_det || echo "Module successfully unloaded"

echo ""
echo "==================================================="
echo "Test completed successfully!"
echo "==================================================="
ENDSSH

echo ""
echo "==================================================="
echo "All tests passed in QEMU VM!"
echo "==================================================="
echo ""
echo "Your host machine kernel was never touched."
echo "The module was safely tested in isolation."
