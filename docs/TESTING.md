# Testing Guide

## Unit Tests

```bash
cargo test
# or
make unit
```

Tests pure helpers and procfs parsing with fake `/proc` fixtures via `PROCLENS_PROC_DIR`.

## Manual Testing

```bash
# One-shot with current shell
./target/release/proclens $$

# Live dashboard
./target/release/proclens

# With fake proc for deterministic testing
PROCLENS_PROC_DIR=/tmp/fake_proc ./target/release/proclens 123
```

## Multi-threaded Test

```bash
make build-multithread
make run-multithread
```

Builds `test_multithread` (C helper with 4 threads + sockets) and validates output.

## Troubleshooting

### No such process

```
error: no such process: 1234
hint: check PID or try sudo for other users' processes
```

The process may have exited or you lack permission. Try `ps aux | grep <name>`.

### Permission denied

Reading other users' processes may require `sudo`:

```bash
sudo ./target/release/proclens 1
```

### Missing I/O stats

```
[io]
status: unavailable (CONFIG_TASK_XACCT disabled)
```

The kernel was built without `CONFIG_TASK_XACCT`. I/O accounting is unavailable.

## Test Coverage

- `util.rs` — all pure helpers (CPU, BSS/heap, formatting, socket strings, sparklines)
- `procfs.rs` — status/stat/io/maps parsing with fake proc fixtures
