# ProcLens Makefile — Rust userspace

CARGO ?= cargo
BUILD_DIR := build
SRC_DIR := src
PROCLENS_VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
export PROCLENS_VERSION

.PHONY: all clean build install uninstall test help unit check format format-check clippy build-multithread run-multithread

all: build

build:
	@echo "Building proclens (Rust)..."
	@mkdir -p $(BUILD_DIR)
	$(CARGO) build --release
	@cp target/release/proclens $(BUILD_DIR)/proclens
	@echo "Build complete: $(BUILD_DIR)/proclens"

install: build
	@echo "Installing proclens to /usr/local/bin (requires sudo)..."
	sudo install -m 755 $(BUILD_DIR)/proclens /usr/local/bin/proclens
	@echo "Installed."

uninstall:
	@echo "Removing proclens..."
	sudo rm -f /usr/local/bin/proclens
	@echo "Uninstalled."

test: build
	@echo "Running proclens (one-shot, current shell PID $$)..."
	$(BUILD_DIR)/proclens $$ || true

unit:
	@echo "Running Rust unit tests..."
	$(CARGO) test

build-multithread:
	@echo "Building multi-threaded test program..."
	@mkdir -p $(BUILD_DIR)
	gcc -Wall -pthread -o $(BUILD_DIR)/test_multithread $(SRC_DIR)/test_multithread.c
	@echo "Built $(BUILD_DIR)/test_multithread"

run-multithread: build build-multithread
	@echo "Running multi-threaded test..."
	@set -e; \
	$(BUILD_DIR)/test_multithread & \
	TEST_PID=$$!; \
	echo "Started test_multithread (PID: $$TEST_PID)"; \
	sleep 1; \
	$(BUILD_DIR)/proclens $$TEST_PID || true; \
	wait $$TEST_PID; \
	echo "Multi-threaded test completed."

clean:
	@echo "Cleaning..."
	$(CARGO) clean 2>/dev/null || true
	rm -rf $(BUILD_DIR)
	@echo "Clean complete!"

check: clippy

clippy:
	@echo "Running clippy..."
	$(CARGO) clippy

format:
	@echo "Formatting with cargo fmt..."
	$(CARGO) fmt

format-check:
	@echo "Checking formatting..."
	$(CARGO) fmt -- --check

help:
	@echo "ProcLens - Build Targets (Rust):"
	@echo ""
	@echo "Build Targets:"
	@echo "  make all               - Build Rust binary (default)"
	@echo "  make build             - Build Rust binary"
	@echo "  make build-multithread - Build multi-threaded test program"
	@echo ""
	@echo "Run Targets:"
	@echo "  make install           - Install binary to /usr/local/bin"
	@echo "  make uninstall         - Remove binary"
	@echo "  make test              - Build and run with current PID"
	@echo ""
	@echo "Test Targets:"
	@echo "  make unit              - Run Rust unit tests (cargo test)"
	@echo "  make run-multithread   - Test with multi-thread program"
	@echo ""
	@echo "Code Quality Targets:"
	@echo "  make check             - Run clippy"
	@echo "  make clippy            - Run clippy"
	@echo "  make format            - Format with cargo fmt"
	@echo "  make format-check      - Check formatting"
	@echo ""
	@echo "Cleanup Targets:"
	@echo "  make clean             - Remove build artifacts"
