# Code Quality

## Tools

### cargo fmt

Code formatter — 100-column limit, Rust 2021 edition.

```bash
cargo fmt
cargo fmt -- --check  # CI check
```

Configuration: `rustfmt.toml`

### cargo clippy

Linter for common mistakes and style issues.

```bash
cargo clippy
```

### cargo test

Unit tests for all pure helpers and procfs parsing.

```bash
cargo test
```

## Quick Reference

```bash
make format        # cargo fmt
make format-check  # cargo fmt -- --check
make check         # cargo clippy
make clippy        # cargo clippy
make unit          # cargo test
```

## Pre-commit

Install the pre-commit hook:

```bash
cp .github/pre-commit.sh .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

The hook runs `cargo fmt --check` and `cargo clippy`.

## CI

GitHub Actions runs on every push/PR:

- `cargo build --release` — build verification
- `cargo fmt -- --check` — formatting
- `cargo clippy` — linting
- `cargo test` — unit tests
