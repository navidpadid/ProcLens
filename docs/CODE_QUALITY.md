# Code Quality and Static Analysis

This project includes comprehensive static analysis and code formatting tools to ensure high code quality and compliance with Linux kernel coding standards.

**All tools and hooks are automatically installed and configured in the dev container.** No manual setup required!

## Quick Reference

```bash
make format          # Format all source files
make format-check    # Check if code is properly formatted (CI-friendly)
make check           # Run all static analysis checks
make checkpatch      # Kernel coding style
make sparse          # Kernel static analysis
make cppcheck        # General C/C++ analysis
```

## Tools Integrated

### 1. clang-format
**Purpose**: Automatic code formatting  
**Standard**: Linux kernel coding style  
**Configuration**: `.clang-format`

**Features**:
- 8-space tabs (kernel standard)
- 80-column line limit
- Linux brace style
- Consistent spacing and alignment

### 2. checkpatch.pl
**Purpose**: Kernel coding style compliance  
**Source**: Official Linux kernel scripts  

**Note**:
- checkpatch.pl requires kernel headers for the running kernel.
- The dev container post-create script installs `linux-headers-$(uname -r)` to avoid version mismatches.

**Checks**:
- Indentation and spacing rules
- Line length limits (80 columns preferred)
- Function declaration style
- Comment formatting (/* */ style)
- Macro usage patterns
- Variable naming conventions

### 3. sparse
**Purpose**: Semantic analysis for C code  
**Specialty**: Kernel-specific checks  

**Detects**:
- Type confusion errors
- Endianness issues (`__be32`, `__le32`)
- Lock context imbalances
- Address space mismatches (`__user`, `__kernel`)
- Null pointer dereferences

### 4. cppcheck
**Purpose**: General C/C++ static analysis  
**Configuration**: `.cppcheck-suppressions`

**Detects**:
- Memory leaks
- Buffer overflows
- Uninitialized variables
- Dead code
- Logic errors

## Configuration Files

- **`.clang-format`** - Code formatting rules (Linux kernel style)
- **`.cppcheck-suppressions`** - Suppression list for false positives
- **`.editorconfig`** - Editor configuration for consistent coding style

## Git Hooks

Pre-commit hooks are automatically installed on container startup.

The pre-commit hook runs:
- Code formatting checks
- Cppcheck static analysis
- Checkpatch coding style validation

To bypass hooks (use sparingly):
```bash
git commit --no-verify -m "message"
```

## Best Practices

**Follow Linux Kernel Coding Style:**
- Use tabs (8 spaces), not spaces for indentation
- 80 column limit for code
- Opening brace on same line (except functions)
- Space after keywords: `if (`, `while (`, `for (`
- No space after function names: `function(arg)`
- Use C89-style comments: `/* comment */`

**Example:**
```c
int example_function(int param)
{
	if (param > 0) {
		/* Comment style: C89 */
		return param * 2;
	}
	return 0;
}
```

## Development Workflow

```bash
# Everything is ready - just start coding!

# Format code before committing
make format

# Run all checks
make check

# Commit (hooks run automatically)
git commit -m "Your message"
```
