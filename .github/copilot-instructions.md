# GitHub Copilot Instructions for ProcLens

## Project Snapshot

ProcLens is a pure userspace Rust application that inspects running processes via `/proc`:

- Reads from standard Linux interfaces: `/proc/<pid>/status`, `stat`, `maps`, `io`, `cmdline`, `task/`, `fd/`
- One-shot mode (`proclens <PID>`) and live dashboard (`proclens`)
- Reports memory pressure/layout, network stats, open sockets, I/O stats, and thread info
- No kernel module — no `insmod`, no kernel headers, no QEMU required

## Architecture and Key Files

### Rust Binary (`src/`)

| File | Purpose |
|------|---------|
| `src/main.rs` | CLI entry, arg parsing via `clap` |
| `src/collector.rs` | Snapshot assembly from all sources |
| `src/procfs.rs` | `/proc` filesystem readers |
| `src/render.rs` | Output formatting and section filtering |
| `src/live.rs` | Live dashboard with raw terminal mode |
| `src/config.rs` | Proc root and color configuration |
| `src/util.rs` | Pure helpers (formatting, calculations) |
| `src/test_multithread.c` | C helper for multi-thread testing |

### Dev Environment

- Uses **Devbox** (`devbox.json`) instead of Docker devcontainers
- `devbox shell` provides Rust toolchain, gcc, make, pkg-config
- No kernel headers or privileged containers needed

## Code Style and Standards

- Rust 2021 edition, 100-column limit (`rustfmt.toml`)
- `cargo fmt` for formatting, `cargo clippy` for linting
- Keep helpers in `util.rs` deterministic and side-effect free
- Preserve output format contracts (exact strings matter for tests)

## Build, Test, and Quality Commands

```bash
cargo build --release  # or: make all
cargo test             # or: make unit
cargo fmt              # or: make format
cargo fmt -- --check   # or: make format-check
cargo clippy           # or: make check / make clippy
```

Or via Devbox:

```bash
devbox run build
devbox run test
devbox run fmt
devbox run lint
devbox run check
```

## Testing Requirements

When changing helper behavior or output formats:

- Update/add tests in `src/util.rs` or `src/procfs.rs` (`#[cfg(test)]`)
- Cover edge cases and division-by-zero behavior
- Keep expectations aligned with exact output formatting where asserted
- Use `PROCLENS_PROC_DIR` env override for deterministic procfs tests

## Release Workflow

- Releases are label-driven on merged PRs or manual dispatch
- Exactly one release label: `release:major`, `release:minor`, or `release:patch`
- `not-a-release` skips release creation

## Do / Don't

### DO

- Run `cargo test` after changing helper format strings
- Update docs/tests alongside feature additions
- Keep output expectations in sync

### DON'T

- Assume `cargo build` success means no warnings — check `cargo clippy`
- Introduce formatting-only changes that break asserted output strings
