# Technical Details

## Architecture

ProcLens is a pure userspace Rust application. It reads directly from standard Linux interfaces:

- `/proc/<pid>/status` — RSS, VSZ, swap, OOM score
- `/proc/<pid>/stat` — CPU times, faults, thread count
- `/proc/<pid>/maps` — memory layout (code, data, heap, stack)
- `/proc/<pid>/io` — I/O accounting (rchar, wchar, syscr, syscw, read/write bytes)
- `/proc/<pid>/cmdline` — command line
- `/proc/<pid>/task/` — thread enumeration
- `/proc/<pid>/fd/` — open file descriptors / sockets
- `NETLINK_SOCK_DIAG` — detailed socket info (future)

No kernel module is required. No `insmod`/`rmmod`. No kernel headers at build time.

## Key Modules

| Module | Purpose |
|--------|---------|
| `main.rs` | CLI entry, arg parsing via `clap` |
| `collector.rs` | Snapshot assembly from all sources |
| `procfs.rs` | `/proc` filesystem readers |
| `render.rs` | Output formatting and section filtering |
| `live.rs` | Live dashboard with raw terminal mode |
| `config.rs` | Proc root and color configuration |
| `util.rs` | Pure helpers (formatting, calculations) |

## Memory Information

| Field | Description | Source |
|-------|-------------|--------|
| **Code Section** | Executable region | `maps` r-xp with file |
| **Data Section** | Writable file-backed | `maps` rw-p with file |
| **BSS Section** | Gap between data and heap | Inferred from maps |
| **Heap Section** | Dynamic allocation | `maps` [heap] |
| **Stack** | Stack region | `maps` [stack] |
| **ELF Base** | First executable mapping | Lowest r-xp address |

## Memory Pressure Statistics

| Field | Description | Source |
|-------|-------------|--------|
| **RSS (Resident)** | Physical memory used | `status` VmRSS |
| **Anonymous** | Private pages | `status` RssAnon |
| **File-backed** | Mapped file pages | `status` RssFile |
| **Shared Mem** | Shared pages | `status` RssShmem |
| **VSZ (Virtual)** | Virtual memory | `status` VmSize |
| **Swap Usage** | Swapped pages | `status` VmSwap |
| **Page Faults** | Major/minor faults | `stat` minflt/majflt |
| **OOM Score Adj** | OOM killer adjustment | `oom_score_adj` |

## I/O Stats

| Field | Description | Source |
|-------|-------------|--------|
| **rchar/wchar** | Syscall bytes | `io` rchar/wchar |
| **syscr/syscw** | Syscall counts | `io` syscr/syscw |
| **read_bytes/write_bytes** | Storage bytes | `io` read_bytes/write_bytes |
| **cancelled_write_bytes** | Truncated bytes | `io` cancelled_write_bytes |
| **avg per syscall** | Average payload | rchar/syscr, wchar/syscw |
| **io_intensity** | Total storage traffic | read_bytes + write_bytes |

If `CONFIG_TASK_XACCT` is disabled, shows `status: unavailable`.

## Network Stats

| Field | Description | Source |
|-------|-------------|--------|
| **sockets_total** | Total socket FDs | `fd/` scan |
| **top_talkers** | Top sockets by traffic | FD analysis |
| **net_devices** | Device usage | Socket ifindex |

## Thread Information

Enumerated via `/proc/<pid>/task/`. Each thread shows TID, name, CPU%, state, priority, nice, and CPU affinity.

## Limitations

- BSS is inferred, not exact (gap between data and heap in maps)
- Heap only shows `[heap]` region, not mmap-based allocations
- Network byte counters require `NETLINK_SOCK_DIAG` (future enhancement)
