# GitHub Copilot Instructions for Kernel Module Project

## Project Overview

This is a Linux kernel module project that provides process information through `/proc/elf_det/`. The module displays detailed process information including memory mappings, CPU usage, memory pressure statistics, and ELF binary details.

## Code Style and Quality Standards

### Kernel Coding Style (HIGHEST PRIORITY)

**CRITICAL**: All kernel module code MUST pass `make checkpatch` with zero errors and zero warnings.

- Follow Linux kernel coding style strictly
- Use tabs for indentation (width: 8)
- Maximum line length: 100 characters (kernel relaxed standard)
- Opening braces on the same line for structs, functions use K&R style
- No spaces inside parentheses: `if (condition)` not `if ( condition )`
- Use `/* */` for comments, not `//` in kernel code
- Variable declarations at the beginning of code blocks

### Special Formatting Rules

When using macros like `for_each_thread`, the opening brace style may conflict with `clang-format`. Use formatting disable pragmas when needed:

```c
// clang-format off
for_each_thread(task, thread) {
	// code here
}
// clang-format on
```

### Code Quality Tools

Run these before every commit:

1. `make format` - Auto-format code with clang-format
2. `make checkpatch` - Validate against kernel coding standards (MUST PASS)
3. `make sparse` - Static analysis for kernel code
4. `make cppcheck` - Additional static analysis

The pre-commit hook automatically runs format-check and cppcheck.

## File Organization

### Source Files (`src/`)

- `elf_det.c` / `elf_det.h` - Main kernel module and shared helpers
- `elf_det_tests.c` - Unit tests for elf_det helpers
- `proc_elf_ctrl.c` / `proc_elf_ctrl.h` - Control utility and helpers
- `proc_elf_ctrl_tests.c` - Unit tests for proc_elf_ctrl helpers
- `test_multithread.c` - Multi-threaded test application

### Documentation (`docs/`)

- `TECHNICAL.md` - Architecture and implementation details
- `TESTING.md` - Test strategy and coverage
- `CODE_QUALITY.md` - Code quality tools and standards
- `SCRIPTS.md` - E2E testing and QEMU workflows
- `RELEASE.md` - Release process and versioning

## Kernel Module Development Guidelines

### Memory Access Patterns

- Always use `get_task_mm()` and `mmput()` when accessing task memory management
- Check for NULL before dereferencing mm pointers: `if (mm)`
- Use `get_mm_counter()` for RSS statistics (MM_FILEPAGES, MM_ANONPAGES, etc.)
- Lock appropriately when accessing task structures

### String Formatting

- Use `seq_printf()` for /proc file output
- Use `snprintf()` for userspace utilities
- Always check buffer boundaries
- Format consistently: use tables, align columns with spacing

### Error Handling

- Return appropriate error codes (-ENOMEM, -EFAULT, -EINVAL, etc.)
- Clean up resources on error paths
- Use `pr_err()`, `pr_warn()`, `pr_info()` for kernel logging
- Check return values from all kernel API calls

### Header Files

- `elf_det.h` contains helper functions that work in BOTH kernel and userspace
- Use `#ifdef __KERNEL__` to separate kernel-only code
- Keep helpers pure (no side effects) for testability
- Add comprehensive documentation comments for public functions

## Testing Requirements

### Unit Tests

When adding new helper functions to headers:
- Add corresponding unit tests in `*_tests.c` files
- Test edge cases: zero, negative, maximum values
- Test boundary conditions
- Aim for 100% coverage of helper functions
- Use descriptive test names: `test_calculate_rss_pages_zero_counters()`

### Integration Tests

- Update `e2e/qemu-test.sh` when adding new output fields
- Test should validate presence of new sections in module output
- Use grep patterns to match expected output format

### Multi-threaded Testing

- Use `test_multithread.c` to validate behavior under concurrent access
- Build with `make build-multithread`
- Run with `make run-multithread`

## Common Patterns

### Adding New Statistics

1. Add calculation helper to header file (e.g., `elf_det.h`)
2. Add unit tests for the helper
3. Call helper in kernel module print function
4. Update `e2e/qemu-test.sh` to validate output
5. Update `docs/TECHNICAL.md` with new field documentation
6. Update `docs/TESTING.md` with test coverage info
7. Update `README.md` to reflect new features in the features list and example output

### Memory Pressure Statistics

Current implementation includes:
- RSS breakdown (anonymous, file-backed, shared memory pages)
- Virtual size (VSZ)
- Swap usage
- Page faults (major/minor)
- OOM score adjustment

Use these as examples when adding similar statistics.

## Build System (Makefile)

### Target Categories

- **Build**: `all`, `module`, `user`, `build-multithread`
- **Run**: `install`, `uninstall`, `test`
- **Test**: `unit`, `run-multithread`
- **Quality**: `check`, `format`, `format-check`, `checkpatch`, `sparse`, `cppcheck`
- **Cleanup**: `clean`

### Adding New Targets

- Group by category in help output
- Use descriptive names with hyphens
- Add `@echo` for user feedback
- Check for required tools before running

## Development Workflow

### Setting Up

1. Dev container automatically installs kernel headers via `.devcontainer/post-create.sh`
2. Git hooks installed automatically (runs format-check and cppcheck)
3. Kernel headers match running kernel version

### Before Committing

```bash
make clean
make all
make unit
make checkpatch  # MUST PASS
make sparse
make cppcheck
```

### Creating Pull Requests

- Use the PR template (`.github/PULL_REQUEST_TEMPLATE.md`)
- Fill out the "Release Notes" section with clean, user-facing descriptions
- DO NOT paste command output or test logs in release notes
- Add appropriate release label: `release:major`, `release:minor`, `release:patch`, or `not-a-release`
- Only one release label per PR

## What to Avoid

### DON'T

- Mix kernel and userspace code without `#ifdef __KERNEL__`
- Use `//` comments in kernel code
- Access task structures without proper locking
- Dereference pointers without NULL checks
- Ignore compiler warnings
- Submit code that fails checkpatch
- Add TODO comments without tracking them
- Use floating point in kernel code (not allowed)
- Call userspace-only functions (printf, malloc, etc.) in kernel code
- Use camelCase - kernel uses snake_case

### DO

- Run all quality checks before committing
- Add tests for all new functionality
- Update documentation when adding features
- Keep functions small and focused
- Use descriptive variable names
- Comment complex algorithms
- Check return values
- Clean up resources on error paths

## QEMU E2E Testing

Located in `e2e/` directory:

- `qemu-setup.sh` - Initialize QEMU VM
- `qemu-run.sh` - Start VM in background
- `qemu-test.sh` - Run tests in VM
- `quick-reference.sh` - Common commands

Tests validate actual kernel module behavior in isolated environment.

## Key Functions Reference

### Kernel Module (`elf_det.c`)

- `print_memory_maps()` - Display memory region information
- `print_cpu_usage()` - Show CPU and timing statistics  
- `print_memory_pressure()` - Display memory pressure metrics
- `print_memory_layout()` - Show heap, stack, code segments
- `print_elf_info()` - Display ELF binary details
- `print_network_stats()` - Display brief per-process network statistics
- `print_sockets()` - Display open sockets and endpoint details

### Helpers (`elf_det.h`)

- `calculate_rss_pages()` - Compute resident set size from mm counters
- `pages_to_kb()` - Convert pages to kilobytes
- `calculate_total_faults()` - Sum major and minor page faults
- `is_valid_oom_score_adj()` - Validate OOM score range
- `calculate_memory_usage_percent()` - Compute memory percentage
- `is_high_memory_pressure()` - Determine if memory pressure is high

## Version and Release

- Follow semantic versioning (MAJOR.MINOR.PATCH)
- Releases triggered by PR labels
- Release notes extracted from PR "Release Notes" section
- See `docs/RELEASE.md` for full release process

## Additional Resources

- Linux Kernel Coding Style: https://www.kernel.org/doc/html/latest/process/coding-style.html
- Kernel APIs: https://www.kernel.org/doc/html/latest/
- Project Documentation: `/docs/*.md`

---

**Remember**: Kernel code quality is non-negotiable. Always run `make checkpatch` and ensure it passes with 0 errors and 0 warnings.
