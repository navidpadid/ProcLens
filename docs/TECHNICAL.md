# Technical Details

## Kernel Module (`elf_det.c`)

The kernel module creates entries in `/proc/elf_det/`:
- `/proc/elf_det/pid` - Write-only file to specify target PID
- `/proc/elf_det/det` - Read-only file to retrieve process information
- `/proc/elf_det/threads` - Read-only file to retrieve thread information

### Key Functions

- `elfdet_show()` - Main function to gather and format process information
- `elfdet_threads_show()` - Gathers thread information for all threads in a process
- `find_stack_vma_end()` - Finds stack VMA lower boundary by iterating VMAs
- `procfile_write()` - Handles PID input from user space
- `procfile_read()` - Returns formatted process data

### Memory Information Extracted

| Field | Description | Source |
|-------|-------------|--------|
| **Code Section** | `start_code` to `end_code` | Executable code region (includes rodata) |
| **Data Section** | `start_data` to `end_data` | Initialized data region |
| **BSS Section** | `end_data` to `start_brk` | Uninitialized data region |
| **Heap Section** | `start_brk` to `brk` | brk-based dynamic memory allocation |
| **Stack** | `start_stack` to `stack_end` | Stack region (grows downward) |
| **ELF Base** | First VMA start | Base address of ELF binary (for PIE) |

### Memory Pressure Statistics

The process information output includes a memory pressure section with:

| Field | Description | Source |
|-------|-------------|--------|
| **RSS (Resident)** | Total physical memory used | Sum of anonymous, file-backed, and shared pages |
| **Anonymous** | Private memory pages | `MM_ANONPAGES` |
| **File-backed** | Mapped file pages | `MM_FILEPAGES` |
| **Shared Mem** | Shared memory pages | `MM_SHMEMPAGES` |
| **VSZ (Virtual)** | Total virtual memory | `mm->total_vm` |
| **Swap Usage** | Pages swapped out | `MM_SWAPENTS` |
| **Page Faults** | Major and minor faults | `task->maj_flt`, `task->min_flt` |
| **OOM Score Adj** | OOM killer adjustment | `task->signal->oom_score_adj` |

### Open Sockets

The process information output includes an open sockets section that lists all open socket file descriptors:

| Field | Description | Details |
|-------|-------------|---------|
| **FD** | File descriptor number | File descriptor index in process fd table |
| **Family** | Socket address family | AF_INET, AF_INET6, AF_UNIX, AF_NETLINK, etc. |
| **Type** | Socket type | STREAM (TCP), DGRAM (UDP), RAW |
| **State** | Connection state (TCP) | ESTABLISHED, LISTEN, CLOSE_WAIT, etc. |
| **Proto** | Socket protocol | TCP, UDP, or OTHER |
| **Traffic** | Per-socket RX/TX packet and byte counters | TCP: lifetime counters; UDP: current queue counters |
| **Local** | Local address and port | IPv4 (e.g., 127.0.0.1:8080) or IPv6 format |
| **Remote** | Remote address and port | Destination address for connected sockets |

The socket listing provides visibility into network connections and IPC sockets in use by the process. For processes with no open sockets, displays "No open sockets".

Notes:
- TCP traffic values come from `struct tcp_sock` counters (`segs_in`, `segs_out`, `bytes_received`, `bytes_sent`).
- UDP traffic values are queue-based snapshots (`sk_receive_queue`, `sk_write_queue`, `sk_rmem_alloc`, `sk_wmem_queued`).
- Protocols other than TCP/UDP are labeled `OTHER` and do not include a traffic line.

### Network Stats (Brief)

The process information output includes a brief network stats section, aggregated across the process sockets:

| Field | Description | Source |
|-------|-------------|--------|
| **sockets_total** | Total sockets and TCP/UDP/UNIX counts | File descriptor scan + `sk_protocol`/`sk_family` |
| **rx_packets** | Total TCP segments received | `struct tcp_sock::segs_in` |
| **tx_packets** | Total TCP segments sent | `struct tcp_sock::segs_out` |
| **rx_bytes** | Total TCP bytes received | `struct tcp_sock::bytes_received` |
| **tx_bytes** | Total TCP bytes sent | `struct tcp_sock::bytes_sent` |
| **tcp_retransmits** | TCP retransmitted segments | `struct tcp_sock::retrans_out` |
| **drops** | Raw/UDP drops | `struct sock::sk_drops` |
| **net_devices** | Device names with socket counts | `sk_bound_dev_if` or `sk_rx_dst_ifindex` |

Notes:
- Packet and byte counters are best-effort and only reflect TCP sockets. UDP and UNIX sockets are counted but do not contribute to byte/packet totals.
- Device mapping uses the socket bound interface or RX route ifindex; if neither is set, the socket is not attributed to a device.

### Important Notes and Limitations

#### 1. BSS May Be Zero-Length
Modern ELF binaries often have `end_data == start_brk`, resulting in zero-length BSS. This is **normal**, not an error.

#### 2. Read-Only Data (rodata)
The read-only data segment is typically merged with the code section (`start_code` to `end_code`) in modern binaries. It's not shown separately.

#### 3. Heap Limitation
The heap shown is **brk-based only** (traditional heap managed by brk/sbrk syscalls). Modern allocators like glibc's malloc also use:
- **mmap-based allocations** for large requests (>128KB typically)
- **Arena heaps** (multiple heap regions)
- These are NOT included in the brk-based heap range shown
- To see full heap usage, parse `/proc/pid/maps` for anonymous `[heap]` entries and unnamed mmap regions

#### 4. Stack
Shows both `start_stack` (top/base) and `stack_end` (current lower boundary). The stack grows downward from start_stack. The actual current stack pointer (in CPU registers) may be anywhere between these bounds.

### Kernel APIs Used

- `proc_fs.h` - Proc filesystem operations
- `seq_file.h` - Sequential file interface
- `sched.h` - Task/process structures
- `mm_types.h` - Memory management structures
- Maple tree API - Modern VMA iteration (kernel 6.8+)

## User Program (`proc_elf_ctrl.c`)

Simple C program that supports two modes:

### Interactive Mode
```bash
./build/proc_elf_ctrl
```
Prompts for a PID, writes to `/proc/elf_det/pid`, then reads `/proc/elf_det/det` and `/proc/elf_det/threads` and prints output.

### Argument Mode
```bash
./build/proc_elf_ctrl <PID>
```
Non-interactive mode - write PID and print both process and thread information.

### Environment Override

You can override the proc directory for testing:

```bash
ELF_DET_PROC_DIR=/tmp/fakeproc ./build/proc_elf_ctrl 12345
```

Internally, path construction is handled via `build_proc_path()`.

## Helper Libraries

### `src/elf_det.h`
Pure functions for CPU usage, BSS range, heap range, thread state, and address range checking:
- `compute_usage_permyriad()` - CPU usage calculation
- `compute_bss_range()` - BSS boundary validation
- `compute_heap_range()` - Heap boundary validation
- `is_address_in_range()` - Address containment check

Works in both kernel and user space contexts.

### `src/proc_elf_ctrl.h`
Path building with environment override:
- `build_proc_path()` - Constructs `/proc/elf_det/` paths with `ELF_DET_PROC_DIR` support

## Output Format

### Process Information (`/proc/elf_det/det`)

The output is human-readable and grouped into sections:

- Basic process info (PID, name, CPU usage)
- Memory pressure statistics (RSS, VSZ, swap, faults, OOM adjustment)
- Memory layout (code/data/BSS/heap/stack/ELF base)
- Memory layout visualization
- Network stats (brief)
- Open sockets (file descriptors, address families, connection states)

### Thread Information (`/proc/elf_det/threads`)
```
TID     NAME    CPU(%)  STATE   PRIORITY        NICE    CPU_AFF
```

Example:
```
01234   bash    0.50    S       0       0       0,1,2,3
01235   worker  0.01    R       0       0       0,1

Total threads: 2
```

#### Thread State Codes
- **R** - Running or runnable (on run queue)
- **S** - Interruptible sleep (waiting for an event)
- **D** - Uninterruptible sleep (usually I/O)
- **T** - Stopped (by job control signal)
- **t** - Tracing stop (by debugger)
- **Z** - Zombie (terminated but not reaped)
- **X** - Dead (should never be seen)

#### Thread Fields
- **TID** - Thread ID (same as PID for main thread)
- **NAME** - Thread name (typically same as process name)
- **CPU(%)** - Per-thread CPU usage since thread start
- **STATE** - Current thread state (see codes above)
- **PRIORITY** - Shown as nice value (-20 to 19, lower = higher priority)
- **NICE** - Nice value for the thread
- **CPU_AFF** - CPU affinity mask (which CPUs thread can run on)

**Note**: BSS_START and BSS_END may be equal (zero-length BSS) in modern ELF binaries. This is normal.
