# Linux Process Information Kernel Module

![Project Banner](docs/banner.png)

[![CI](https://img.shields.io/github/actions/workflow/status/navidpadid/process-info-kernel-module/ci.yml?branch=main&style=for-the-badge&logo=github&logoColor=white&label=Build)](https://github.com/navidpadid/process-info-kernel-module/actions/workflows/ci.yml)
[![Last Commit](https://img.shields.io/github/last-commit/navidpadid/process-info-kernel-module?style=for-the-badge&logo=git&logoColor=white)](https://github.com/navidpadid/process-info-kernel-module/commits/main)
[![License](https://img.shields.io/badge/License-MIT-blue?style=for-the-badge)](LICENSE)

> A Linux kernel module that extracts detailed process and it's thread information including memory layout, CPU usage, ELF sections, Networking sockets and statistics via `/proc` filesystem.

## Features

- **Process Memory Layout**: Code, Data, BSS, Heap, and Stack addresses
- **Memory Pressure Monitoring**: RSS, VSZ, swap usage, page faults (major/minor), and OOM score adjustment
- **Visual Memory Map**: Proportional bar chart visualization of memory regions
- **Open Sockets**: List all open sockets with family (IPv4/IPv6/Unix), type, state, and addresses
- **Network Stats (Brief)**: Per-process TCP counters, socket counts (TCP/UDP/UNIX), drops, and net devices
- **Thread Information**: List all threads with TID, state, CPU usage, priority, and CPU affinity
- **CPU Usage Tracking**: Real-time CPU percentage calculation per process and thread
- **ELF Section Analysis**: Binary base address and section boundaries
- **Proc Interface**: Easy access through `/proc/elf_det/`
- **Comprehensive Testing**: Unit tests and QEMU-based E2E testing
- **Code Quality**: Pre-configured static analysis (sparse, cppcheck, checkpatch)

### Example Output

```
>> Enter process ID (or Ctrl+C to exit): 7645

===============================================================
PROCESS INFORMATION
===============================================================
Command line:  /vscode/vscode-server/bin/linux-x64/b6a47e94e326b5c209d118cf0f994d6065585705/node --dns-result-order=ipv4first /vscode/
vscode-server/bin/linux-x64/b6a47e94e326b5c209d118cf0f994d6065585705/out/bootstrap-fork --type=extensionHost --transformURIs --useHostP
roxy=true  
Process ID:     7645
Name:            node
CPU Usage:       2.28%

Memory Pressure Statistics:
--------------------------------------------------------------------------------
 RSS (Resident):  821420 KB
   - Anonymous:   753576 KB
   - File-backed: 67844 KB
   - Shared Mem:  0 KB
 VSZ (Virtual):   66781376 KB
 Swap Usage:      0 KB
 Page Faults:
   - Major:       70
   - Minor:       2235493
   - Total:       2235563
 OOM Score Adj:   0
--------------------------------------------------------------------------------

Memory Layout:
--------------------------------------------------------------------------------
 Code Section:    0x0000000000e30000 - 0x0000000003400451
 Data Section:    0x00000000069c68e8 - 0x00000000069f2c30
 BSS Section:     0x00000000069f2c30 - 0x000000002d34a000
 Heap:            0x000000002d34a000 - 0x0000000035bf3000
 Stack:           0x00007fff6729f880 - 0x00007fff671a8000
 ELF Base:        0x0000000000400000

Memory Layout Visualization:
--------------------------------------------------------------------------------
Low:  0x0000000000e30000

CODE  (37 MB)
     [==                                                 ]

DATA  (176 KB)
     [=                                                  ]

BSS   (617 MB)
     [======================================           ]

HEAP  (136 MB)
     [========                                         ]

STACK (990 KB)
     [=                                                  ]

High: 0x00007fff6729f880
--------------------------------------------------------------------------------

[network]
sockets_total: 19 (tcp: 2, udp: 0, unix: 17)
rx_packets: 34284
tx_packets: 33993
rx_bytes: 68980951
tx_bytes: 37736732
tcp_retransmits: 0
drops: 0
net_devices: lo=1 eth0=1

Open Sockets:
--------------------------------------------------------------------------------
 [FD 0] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 1] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 2] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 3] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 23] Family: AF_INET     Type: STREAM    State: ESTABLISHED  
         Local:  127.0.0.1:[REDACTED]  Remote: 127.0.0.1:[REDACTED]
 [FD 26] Family: AF_UNIX     Type: STREAM    State: LISTEN       
 [FD 36] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 37] Family: AF_UNIX     Type: STREAM    State: LISTEN       
 [FD 39] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 41] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 43] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 45] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 49] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 50] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 51] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 53] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 57] Family: AF_UNIX     Type: STREAM    State: ESTABLISHED  
 [FD 59] Family: AF_INET     Type: STREAM    State: ESTABLISHED  
         Local:  [INTERNAL_IP]:[PORT]  Remote: [REDACTED_PUBLIC_IP]:443
 [FD 62] Family: AF_UNIX     Type: STREAM    State: LISTEN       
--------------------------------------------------------------------------------

===============================================================
THREAD INFORMATION
===============================================================
TID    NAME            CPU(%)  STATE  PRIORITY  NICE  CPU_AFFINITY
-----  ---------------  -------  -----  --------  ----  ----------------
7645   node               2.28   S        0        0  0,1,2,3,4,5,6,7
7650   DelayedTaskSche    0.00   S        0        0  0,1,2,3,4,5,6,7
7651   node               0.29   S        0        0  0,1,2,3,4,5,6,7
7652   node               0.29   S        0        0  0,1,2,3,4,5,6,7
7653   node               0.29   S        0        0  0,1,2,3,4,5,6,7
7654   node               0.29   S        0        0  0,1,2,3,4,5,6,7
7663   node               0.00   S        0        0  0,1,2,3,4,5,6,7
7675   libuv-worker       0.04   S        0        0  0,1,2,3,4,5,6,7
7676   libuv-worker       0.05   S        0        0  0,1,2,3,4,5,6,7
7677   libuv-worker       0.04   S        0        0  0,1,2,3,4,5,6,7
7678   libuv-worker       0.05   S        0        0  0,1,2,3,4,5,6,7
7728   node               0.00   S        0        0  0,1,2,3,4,5,6,7
7732   node               0.00   S        0        0  0,1,2,3,4,5,6,7
13594  node               0.05   S        0        0  0,1,2,3,4,5,6,7
18189  node               0.01   S        0        0  0,1,2,3,4,5,6,7
--------------------------------------------------------------------------------
Total threads: 15
===============================================================
```

## Quick Start

### Prerequisites

- **Docker** + **VS Code** with Remote - Containers extension
- Dev container includes everything: Ubuntu 24.04, kernel 6.8+ headers, build tools, static analysis

### Build and Run

1. Open project in VS Code → "Reopen in Container"
2. Build:
   ```bash
   make all
   ```
3. Install module:
   ```bash
   sudo make install
   ```
4. Run user program:
   ```bash
   ./build/proc_elf_ctrl
   ```

### Uninstall the Module

```bash
sudo make uninstall
```

## Project Structure

```
kernel_module/
├── .devcontainer/      # Dev container config (Docker + VS Code setup)
├── .github/            # CI/CD workflows (GitHub Actions)
├── docs/               # Detailed documentation
├── e2e/                # End-to-end testing scripts (QEMU setup, automation)
├── src/                # Source code (kernel module, user program, tests, helpers)
├── build/              # Build artifacts (generated by make)
└── Makefile            # Build system with quality checks
```

**Notes**: 
- **Memory Visualization**: Each region's bar length is proportional to its actual size
- Sizes are automatically displayed in appropriate units (B, KB, or MB)
- Low/High addresses show the memory address range of the process
- BSS_START and BSS_END may be equal (zero-length BSS) in modern ELF binaries. This is normal.
- **Open Sockets**: Shows file descriptor, socket family (AF_INET, AF_INET6, AF_UNIX), type (STREAM/DGRAM), state, and addresses
- Socket families: AF_INET (IPv4), AF_INET6 (IPv6), AF_UNIX (Unix domain), AF_NETLINK (Netlink)
- Thread STATE: R=Running, S=Sleeping, D=Uninterruptible, T=Stopped, t=Traced, Z=Zombie, X=Dead
- PRIORITY: Shown as nice value (-20 to 19, where lower is higher priority)
- CPU_AFFINITY: Shows which CPUs the thread can run on



## Makefile Targets

```bash
Build Targets:
  make all               - Build both kernel module and user program (default)
  make module            - Build kernel module only
  make user              - Build user program only
  make build-multithread - Build multi-threaded test program

Run Targets:
  make install           - Install kernel module (requires root)
  make uninstall         - Remove kernel module (requires root)
  make test              - Install module and run user program

Test Targets:
  make unit              - Build and run function-level unit tests
  make run-multithread   - Install module and test multi-thread program

Code Quality Targets:
  make check             - Run all static analysis checks
  make checkpatch        - Check kernel coding style with checkpatch.pl
  make sparse            - Run sparse static analyzer
  make cppcheck          - Run cppcheck static analyzer
  make format            - Format code with clang-format
  make format-check      - Check if code is properly formatted (CI)

Cleanup Targets:
  make clean             - Remove all build artifacts
```

## Testing

### Unit Tests (Recommended First)
```bash
make unit
```
Runs pure function tests without kernel dependencies.

### Multi-threaded Module Test
```bash
make run-multithread
```
Builds the multi-threaded test program, installs the module, and validates output via the user program.

### QEMU Testing (Safe Kernel Testing)
```bash
./e2e/qemu-setup.sh    # One-time setup
./e2e/qemu-run.sh      # Start VM
./e2e/qemu-test.sh     # Run automated tests
```

## Documentation

- [TESTING.md](docs/TESTING.md) - Unit tests, QEMU testing, troubleshooting
- [TECHNICAL.md](docs/TECHNICAL.md) - Kernel module details, memory layout, limitations
- [CODE_QUALITY.md](docs/CODE_QUALITY.md) - Static analysis, code formatting, best practices
- [SCRIPTS.md](docs/SCRIPTS.md) - Detailed script documentation
- [RELEASE.md](docs/RELEASE.md) - Version release process and guidelines

## License

MIT License. See [LICENSE](LICENSE) for full terms.

## Contributing

Contributions welcome! The project includes:
- Pre-configured dev container
- Automated testing (unit tests + QEMU E2E)
- Static analysis and formatting tools
- GitHub Actions CI/CD

---


### Static Analysis Tools

#### checkpatch.pl
Official Linux kernel coding style checker. Enforces kernel coding standards including:
- Indentation and spacing rules
- Line length limits
- Function declaration style
- Comment formatting
- Macro usage patterns

#### sparse
Semantic parser specifically designed for kernel code. Detects:
- Type confusion errors
- Endianness issues
- Lock context imbalances
- Address space mismatches
- Null pointer dereferences

#### cppcheck
General-purpose C/C++ static analyzer. Finds:
- Memory leaks
- Buffer overflows
- Uninitialized variables
- Dead code
- Logic errors

#### clang-format
Code formatter that ensures consistent style:
- 8-space tabs (kernel standard)
- 80-column limit
- Linux brace style
- Proper spacing and alignment