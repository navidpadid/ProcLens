# Testing Guide

## Unit Tests (No Kernel Required)

Run pure function tests without loading any kernel modules:

```bash
make unit
```

This builds and runs:
- `src/elf_det_tests.c` – verifies `compute_usage_permyriad()`, `compute_bss_range()`, `compute_heap_range()`, `is_address_in_range()`, `get_thread_state_char()`, `build_cpu_affinity_string()`, and memory-pressure helpers
- `src/proc_elf_ctrl_tests.c` – verifies `build_proc_path()`, PID argument bounds,
    process-info print path, cmdline formatting, and section filtering for memory/network/I/O live views

Artifacts are created under `build/`.

## QEMU Testing (Recommended)

For maximum safety, test the kernel module in an isolated QEMU virtual machine.

### Quick Start

```bash
# One-time setup
sudo ./e2e/qemu-setup.sh

# Start VM
sudo ./e2e/qemu-run.sh

# In another terminal, run automated tests
sudo ./e2e/qemu-test.sh
```

### What QEMU Testing Does

- Downloads Ubuntu 24.04 VM image
- Configures VM to install kernel and headers matching host `uname -r`
- Installs build prerequisites and repairs interrupted package state during setup
- Provides SSH access on port 2222
- Completely isolates module testing from your host
- Automated build, install, test, and uninstall cycle

### Manual Testing in QEMU

After starting the VM with `sudo ./e2e/qemu-run.sh`:

```bash
# SSH into the VM
ssh -p 2222 ubuntu@localhost
# Password: ubuntu

# Inside VM - build and test
cd ProcLens
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
sudo ./e2e/qemu-setup.sh
```

## Kernel Compatibility

Validated testing path in this repository:
- QEMU image: Ubuntu 24.04 LTS
- Guest kernel: installed to match host `uname -r` during `qemu-setup.sh`

**Requirements:**
- Kernel 5.6+ required (proc_ops API)
- Kernel 6.8+ recommended (VMA iterator API with maple tree)

For reproducible results, use the QEMU workflow (`qemu-setup.sh`, `qemu-run.sh`, `qemu-test.sh`).

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

If you are developing from WSL or macOS using Docker/dev containers, the container
kernel may not provide a usable `/lib/modules/$(uname -r)/build` tree. When you
see errors like `No such file or directory` for that path, run the QEMU e2e flow
instead of building modules directly in the container:

```bash
sudo ./e2e/qemu-setup.sh
sudo ./e2e/qemu-run.sh
sudo ./e2e/qemu-test.sh
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

### Testing Live Mode Controls

Verify no-argument live mode behavior:

```bash
./build/proc_elf_ctrl
```

Expected behavior:
- Program prints the startup logo once, then validates module/proc readiness
- If `elf_det` is not loaded, program exits immediately with status `-1`
- If required proc files are missing (`pid`, `det`, `threads`), program exits immediately with status `-1`
- Screen refreshes every 1 second
- Header shows controls (`1` memory, `2` network, `3` threads, `4` I/O, `0` switch PID)
- Each refresh shows `Snapshot start` and `Snapshot end` timestamps in `YY/MM/DD HH:MM:SS`
- `Up`/`k` moves to older snapshots, `Down`/`j` moves toward newer snapshots
- While browsing old snapshots, header indicates history-browsing mode
- Press `f` to return to live-follow mode
- Default section is memory (`1`)
- Press `4` to view only I/O statistics (`[io]` section)
- Pressing `0` prompts for a new PID and continues live refresh for that PID

Note: Live mode requires a real TTY for raw terminal input (`tcgetattr`/`tcsetattr`).
The single-keypress navigation cannot be exercised by unit tests; use manual testing
or the QEMU VM to verify it end-to-end.

Quick deterministic check with a fake proc directory:

```bash
mkdir -p /tmp/fake_proc
: > /tmp/fake_proc/pid
printf 'Memory Pressure Statistics:\n[network]\nsockets_total: 2\n' > /tmp/fake_proc/det
printf 'tid header\nthread line\n' > /tmp/fake_proc/threads
ELF_DET_PROC_DIR=/tmp/fake_proc ./build/proc_elf_ctrl
```
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
- `top_talkers:`

Note: RX/TX bytes and packets are aggregated from TCP sockets only. UNIX
sockets are counted but do not contribute to byte/packet totals.

Top talkers behavior:
- Up to three sockets are listed in descending `RX + TX` byte order.
- TCP entries use lifetime byte counters.
- UDP entries use queue-based byte snapshots.
- If no socket has byte activity, the section displays `none`.

In the open sockets section:
- TCP sockets include lifetime packet/byte counters.
- UDP sockets include queue-based packet/byte counters (current queued data).

### Testing I/O Stats

The process output should include an I/O section:

```bash
./build/proc_elf_ctrl $$
```

Look for:
- `[io]`
- `rchar:`, `wchar:`, `syscr:`, `syscw:`
- `read_bytes:`, `write_bytes:`, `cancelled_write_bytes:`
- `avg_read_bytes_per_syscall:`, `avg_write_bytes_per_syscall:`
- `io_intensity:`

If the running kernel was built without task I/O accounting (`CONFIG_TASK_XACCT`),
the section will show `status: unavailable` instead of per-field counters.

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