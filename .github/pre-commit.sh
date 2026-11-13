#!/bin/bash
# Pre-commit hook for Rust project

set -e

echo "Running pre-commit checks..."
echo ""

FAILED=0

# Check formatting
if command -v cargo &>/dev/null; then
    echo "Checking formatting..."
    if ! cargo fmt -- --check 2>/dev/null; then
        echo "[x] Formatting check failed! Run 'cargo fmt' to fix."
        FAILED=1
    else
        echo "[✓] Formatting check passed"
    fi

    echo ""
    echo "Running clippy..."
    if ! cargo clippy 2>&1 | grep -q "error:"; then
        echo "[✓] Clippy passed"
    else
        echo "[x] Clippy found issues"
        cargo clippy 2>&1 | grep "error:"
        FAILED=1
    fi
else
    echo "Skipping checks (cargo not found, use devbox shell)"
fi

echo ""
echo "================================================"

if [ $FAILED -eq 1 ]; then
    echo "[x] Pre-commit checks failed!"
    echo "Fix issues or use 'git commit --no-verify' to skip"
    exit 1
else
    echo "[✓] All pre-commit checks passed!"
fi
