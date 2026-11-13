# Scripts

## Devbox

This project uses [Devbox](https://www.jetpack.io/devbox) for reproducible dev environments.

```bash
devbox shell          # enter dev environment
devbox run build      # cargo build --release
devbox run test       # cargo test
devbox run fmt        # cargo fmt
devbox run lint       # cargo clippy
devbox run check      # fmt --check + clippy + test
```

`devbox.json` provides: `rustup`, `gcc`, `gnumake`, `pkg-config`.

## Pre-commit Hook

```bash
cp .github/pre-commit.sh .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit
```

Runs `cargo fmt --check` and `cargo clippy` before each commit.
