# Testing Guide

## Unit Tests (No Kernel Required)

Run pure function tests without loading any kernel modules:

```bash
make unit
```

This builds and runs:
- `src/elf_det_tests.c` – verifies `compute_usage_permyriad()`, `compute_bss_range()`, `compute_heap_range()`, `is_address_in_range()`, `get_thread_state_char()`, `build_cpu_affinity_string()`, and memory-pressure helpers
- `src/proc_elf_ctrl_tests.c` – verifies `build_proc_path()` with and without `ELF_DET_PROC_DIR`

Artifacts are created under `build/`.

## QEMU Testing (Recommended)

For maximum safety, test the kernel module in an isolated QEMU virtual machine.

### Quick Start

```bash
# One-time setup
./e2e/qemu-setup.sh

# Start VM
./e2e/qemu-run.sh

# In another terminal, run automated tests
./e2e/qemu-test.sh
```

### What QEMU Testing Does

- Downloads Ubuntu 24.04 VM image
- Configures VM with kernel headers
- Provides SSH access on port 2222
- Completely isolates module testing from your host
- Automated build, install, test, and uninstall cycle

### Manual Testing in QEMU

After starting the VM with `./e2e/qemu-run.sh`:

```bash
# SSH into the VM
ssh -p 2222 ubuntu@localhost
# Password: ubuntu

# Inside VM - build and test
cd kernel_module
make clean && make all
sudo make install
./build/proc_elf_ctrl

# Multi-threaded module test
make run-multithread

# Check kernel logs
sudo dmesg | tail -20

# Unload module
sudo make uninstall
```

### VM Access

- **SSH**: `ssh -p 2222 ubuntu@localhost`
- **Password**: `ubuntu` (change after first login)
- **Exit QEMU console**: Ctrl+A then X
- **Copy files TO VM**: `scp -P 2222 file.txt ubuntu@localhost:~/`
- **Copy files FROM VM**: `scp -P 2222 ubuntu@localhost:~/file.txt ./`

### Cleanup

```bash
# Remove QEMU environment and start fresh
rm -rf e2e/qemu-env/
./e2e/qemu-setup.sh
```

## Kernel Compatibility

The module has been tested on:
- Ubuntu 20.04 LTS (Kernel 5.15+)
- Ubuntu 22.04 LTS (Kernel 5.19+)
- Ubuntu 24.04 LTS (Kernel 6.8+)

**Requirements:**
- Kernel 5.6+ required (proc_ops API)
- Kernel 6.8+ recommended (VMA iterator API with maple tree)

## Troubleshooting

### Module won't load

```bash
# Check kernel logs
dmesg | tail -n 20

# Verify kernel headers are installed
ls /lib/modules/$(uname -r)/build
```

### Build errors

```bash
# Install missing dependencies
sudo apt-get install -y build-essential linux-headers-$(uname -r)

# Clean and rebuild
make clean
make all
```

### Permission denied when running user program

```bash
# Ensure module is loaded
lsmod | grep elf_det

# Check proc entries exist (should show det, pid, threads)
ls -la /proc/elf_det/
```

### Testing Thread Information

```bash
# Test with single-threaded process
./build/proc_elf_ctrl $$

# Test with multi-threaded process
# Start the bundled multi-threaded test program
make build-multithread
./build/test_multithread &
./build/proc_elf_ctrl $!

# Manually check thread info
echo "<PID>" | sudo tee /proc/elf_det/pid
sudo cat /proc/elf_det/threads
```

### Verify Thread Features

The thread output should include:
- TID (Thread ID)
- Thread name
- Per-thread CPU usage
- Thread state (R/S/D/T/t/Z/X)
- Priority and nice values
- CPU affinity mask
- Total thread count

Multi-threaded processes (browsers, IDEs, servers) will show multiple threads.
### Testing Socket Information

To verify socket functionality, test with processes that have open sockets:

```bash
# Test with systemd (PID 1) - usually has several sockets
./build/proc_elf_ctrl 1

# Test with SSH daemon
pgrep sshd | head -1 | xargs ./build/proc_elf_ctrl

# Test with current shell (will show SSH connection socket if remote)
./build/proc_elf_ctrl $$
```

The socket output should include:
- File descriptor numbers
- Socket family (AF_INET, AF_INET6, AF_UNIX, AF_NETLINK)
- Socket type (STREAM, DGRAM, RAW)
- Connection state (ESTABLISHED, LISTEN, CLOSE_WAIT, etc.)
- Protocol label (TCP, UDP, OTHER)
- Per-socket traffic line for TCP/UDP sockets (`Traffic: RX ... TX ...`)
- Local and remote addresses/ports for inet sockets
- IPv6 addresses in full format

Processes with no open sockets will display "No open sockets".

### Testing Network Stats (Brief)

The process output should include a short network section when sockets exist:

```bash
./build/proc_elf_ctrl $$
```

Look for:
- `[network]`
- `sockets_total:`
- `net_devices:`

Note: RX/TX bytes and packets are aggregated from TCP sockets only. UNIX
sockets are counted but do not contribute to byte/packet totals.

In the open sockets section:
- TCP sockets include lifetime packet/byte counters.
- UDP sockets include queue-based packet/byte counters (current queued data).

## Test Coverage

### Helper Function Tests (`elf_det_tests.c`)

Complete unit test coverage for all pure helper functions:

#### CPU and Memory Helpers
- `compute_usage_permyriad()` - CPU usage calculation
- `compute_bss_range()` - BSS boundary validation
- `compute_heap_range()` - Heap boundary validation
- `is_address_in_range()` - Address containment check

#### Thread Helpers
- `get_thread_state_char()` - Thread state conversion
- `build_cpu_affinity_string()` - CPU affinity formatting

#### Memory Pressure Helpers
- `calculate_rss_pages()` - RSS calculation
- `pages_to_kb()` - Page to KB conversion
- `calculate_total_faults()` - Total page faults
- `is_valid_oom_score_adj()` - OOM score validation
- `calculate_memory_usage_percent()` - Memory percentage
- `format_page_fault_stats()` - Page fault formatting
- `is_high_memory_pressure()` - Memory pressure detection

#### Socket Helpers
- `socket_family_to_string()` - Socket family conversion (AF_INET, AF_INET6, AF_UNIX, AF_NETLINK)
- `socket_type_to_string()` - Socket type conversion (STREAM, DGRAM, RAW)
- `socket_state_to_string()` - TCP state conversion (ESTABLISHED, LISTEN, etc.)
- `add_netdev_count()` - Net device usage aggregation helper

All helpers are tested with:
- Normal cases
- Edge cases (zero, maximum values, boundaries)
- Error cases (invalid input, NULL pointers)
- All valid enum/state values