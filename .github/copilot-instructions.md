# GitHub Copilot Instructions for ProcLens

## Project Snapshot

ProcLens is a hybrid project:

- Linux kernel module exposing process data via `/proc/proclens_module/`
- Userspace CLI (`proclens`) for one-shot and live monitoring views
- Unit, static-analysis, and QEMU-based end-to-end test workflows

The module currently reports memory pressure/layout, network stats, open sockets,
per-process I/O statistics, ELF metadata, and thread information.

## Architecture and Key Files

### Kernel Module (`src/proclens_module.c`, `src/proclens_module.h`)

- `/proc/proclens_module/pid` accepts a PID selection
- `/proc/proclens_module/det` prints process details
- `/proc/proclens_module/threads` prints thread details
- Main render path: memory, network, sockets, I/O, and thread sections

### Userspace CLI (`src/proclens.c`, `src/proclens.h`)

- One-shot mode for a specific PID
- Live mode dashboard with section filtering and refresh loop
- Uses procfs endpoints provided by the kernel module

### Tests

- `src/proclens_module_tests.c`: helper-focused unit tests for module helpers
- `src/proclens_tests.c`: userspace/helper unit tests
- `src/test_multithread.c`: multithreaded runtime behavior test

### Documentation

- `README.md`: user-facing overview, install, usage
- `docs/TECHNICAL.md`: implementation details
- `docs/TESTING.md`: test strategy and commands
- `docs/CODE_QUALITY.md`: quality tooling details
- `docs/SCRIPTS.md`: QEMU workflow details
- `docs/RELEASE.md`: release process and labels

## Code Style and Standards

### Kernel Coding Priority

**CRITICAL**: `src/proclens_module.c` and `src/proclens_module.h` must stay
checkpatch-clean (0 errors, 0 warnings) for release-quality changes.

- Use Linux kernel coding style
- Use tabs for kernel C indentation (tab width 8)
- Treat 100 columns as the project baseline
- Keep kernel naming in snake_case
- Prefer `/* */` comments in kernel code

### Header and Helper Rules

- `src/proclens_module.h` contains helpers used by kernel and userspace tests
- Keep helpers deterministic and side-effect free when possible
- Guard kernel-only functionality with `#ifdef __KERNEL__`
- Preserve API/format contracts used by unit tests (exact output strings matter)

## Formatting and Checkpatch Interoperability

This repository uses `clang-format`, but checkpatch has stricter opinions in some
areas. Preserve checkpatch-sensitive formatting in kernel files when needed.

Use local formatting guards for known conflict zones:

```c
/* clang-format off */
/* checkpatch-sensitive code */
/* clang-format on */
```

Examples of sensitive patterns:

- String formatting lines where split literals trigger checkpatch warnings
- Multi-line call layout where checkpatch flags continuation style

## Build, Test, and Quality Commands

Use these commands during development:

1. `make clean`
2. `make all`
3. `make unit`
4. `make checkpatch`
5. `make sparse`
6. `make cppcheck`
7. `make check`

Additional useful targets:

- `make module`
- `make user`
- `make build-multithread`
- `make run-multithread`
- `make format`
- `make format-check`

## Enforcement Reality (Important)

`make checkpatch`, `make sparse`, and `make cppcheck` currently mask non-zero
exits in the Makefile. Also, CI marks checkpatch/cppcheck/sparse as
`continue-on-error`.

Implication:

- Green `make check` or CI does **not** automatically mean style/analysis clean
- Always read tool output directly for kernel-file regressions
- Treat kernel-file checkpatch findings as blocking even if command exits 0

## Testing Requirements

### Unit Tests

When changing helper behavior or output formats:

- Update/add tests in `src/proclens_module_tests.c` or `src/proclens_tests.c`
- Cover edge cases and division-by-zero behavior
- Keep expectations aligned with exact output formatting where asserted

### Integration / E2E

When adding output fields/sections:

1. Update kernel output implementation
2. Update userspace filtering/rendering if applicable
3. Update `e2e/qemu-test.sh` assertions
4. Update docs (`README.md`, `docs/TECHNICAL.md`, `docs/TESTING.md`)

## Kernel Development Guidance

### Memory Access and Locking

- Null-check task/mm pointers before dereference
- Follow existing codebase locking patterns when reading task memory maps
- If accessing mm beyond local locked scope, use proper lifetime management
  (`get_task_mm()`/`mmput()`) and document why

### Error Handling

- Return appropriate kernel/user error codes
- Validate all kernel API return values
- Keep cleanup paths explicit and complete
- Use `pr_err()`, `pr_warn()`, `pr_info()` consistently

## QEMU E2E Workflow

Scripts in `e2e/`:

- `qemu-setup.sh`: prepares image and host dependencies
- `qemu-run.sh`: starts VM (KVM when available, TCG fallback)
- `qemu-test.sh`: copies source into VM, builds, and validates output

Run sequence:

1. `sudo ./e2e/qemu-setup.sh`
2. `sudo ./e2e/qemu-run.sh`
3. `sudo ./e2e/qemu-test.sh`

## Release Workflow

- Releases are label-driven on merged PRs or manual dispatch
- Exactly one release label: `release:major`, `release:minor`, or `release:patch`
- `not-a-release` skips release creation
- Use the PR template and provide clean release notes

## Common Gotchas

- `make format`/`format-check` intentionally skip generated `*.mod.c` files
- Checkpatch can warn on split string literals even if clang-format is happy
- Unit tests can fail on whitespace/spacing changes in formatted output strings
- Some tools print warnings but still return success due to Makefile design

## Do / Don't

### DO

- Keep kernel-module files checkpatch-clean
- Run unit tests after changing helper format strings
- Update docs/tests alongside feature additions
- Keep userspace and kernel output expectations in sync

### DON'T

- Assume `make check` success means no warnings
- Introduce formatting-only changes that break asserted output strings
- Mix kernel and userspace code paths without proper guards
- Ignore lock/lifetime requirements around task/mm access

## Additional References

- Linux Kernel Coding Style:
  https://www.kernel.org/doc/html/latest/process/coding-style.html
- Kernel API docs:
  https://www.kernel.org/doc/html/latest/
- Project docs:
  `docs/*.md`

---

**Remember**: For this repository, release-quality changes require manual review
of tool output plus strict cleanliness in `src/proclens_module.c` and
`src/proclens_module.h`, regardless of masked exit codes.
