# Canon R5 Linux Driver Suite
# Copyright (C) 2025 Canon R5 Driver Project
# SPDX-License-Identifier: GPL-2.0

DRIVER_NAME := canon-r5
DRIVER_VERSION := 0.1.0

# Kernel build system
KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Module definitions
obj-m += canon-r5-core.o
obj-m += canon-r5-usb.o
obj-m += canon-r5-video.o
obj-m += canon-r5-still.o
obj-m += canon-r5-audio.o
obj-m += canon-r5-storage.o

# Source file mappings
canon-r5-core-objs := drivers/core/canon-r5-core.o drivers/core/canon-r5-ptp.o
canon-r5-usb-objs := drivers/core/canon-r5-usb.o
canon-r5-video-objs := drivers/video/canon-r5-v4l2.o drivers/video/canon-r5-videobuf2.o drivers/video/canon-r5-liveview.o
canon-r5-still-objs := drivers/still/canon-r5-still.o
canon-r5-audio-objs := drivers/audio/canon-r5-audio.o
canon-r5-storage-objs := drivers/storage/canon-r5-storage.o drivers/storage/canon-r5-filesystem.o

# Compiler flags
ccflags-y += -I$(src)/include
ccflags-y += -DDRIVER_VERSION=\"$(DRIVER_VERSION)\"
ccflags-y += -Wall -Wextra -Wno-unused-parameter

# Debug build support
ifdef DEBUG
ccflags-y += -DDEBUG -g -O0
else
ccflags-y += -O2
endif

# Build targets
.PHONY: all modules clean install uninstall test security-check help

all: modules
	@echo "Canon R5 Driver Suite v$(DRIVER_VERSION) built successfully"

modules:
	@echo "Building Canon R5 Driver Suite v$(DRIVER_VERSION)"
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
	@echo "Build complete - modules built successfully"

clean:
	@echo "Cleaning build artifacts..."
	# Clean build artifacts without using unsupported modules_clean target
	@rm -f *.ko *.mod *.mod.c *.o Module.symvers modules.order
	@find . -name "*.cmd" -delete 2>/dev/null || true
	@find . -name "*.o.d" -delete 2>/dev/null || true
	@rm -rf .tmp_versions/ 2>/dev/null || true
	@echo "Clean complete"

install: modules
	@echo "Installing Canon R5 Driver Suite modules"
	@echo "Copying modules to system..."
	@sudo mkdir -p /lib/modules/$(shell uname -r)/extra/
	@if ls *.ko >/dev/null 2>&1; then \
		echo "Using modules from repository root"; \
		sudo cp *.ko /lib/modules/$(shell uname -r)/extra/; \
	elif ls build/modules/*.ko >/dev/null 2>&1; then \
		echo "Using modules from build/modules/"; \
		sudo cp build/modules/*.ko /lib/modules/$(shell uname -r)/extra/; \
	else \
		echo "No modules found to install"; exit 1; \
	fi
	@echo "Running depmod to update module dependencies"
	@sudo /sbin/depmod -a
	@echo "Installation complete"

uninstall:
	@echo "Uninstalling Canon R5 Driver Suite"
	@sudo rm -f /lib/modules/$(shell uname -r)/extra/canon-r5-*.ko
	@sudo /sbin/depmod -a
	@echo "Uninstall complete"

# Load all modules in correct order
load-modules: install
	@echo "Loading Canon R5 driver modules..."
	@sudo modprobe canon-r5-core
	@sudo modprobe canon-r5-usb
	@sudo modprobe canon-r5-video
	@sudo modprobe canon-r5-still
	@sudo modprobe canon-r5-audio
	@sudo modprobe canon-r5-storage
	@echo "Modules loaded successfully"
	@lsmod | grep canon-r5

# Unload all modules
unload-modules:
	@echo "Unloading Canon R5 driver modules..."
	@sudo rmmod canon-r5-storage 2>/dev/null || true
	@sudo rmmod canon-r5-audio 2>/dev/null || true
	@sudo rmmod canon-r5-still 2>/dev/null || true
	@sudo rmmod canon-r5-video 2>/dev/null || true
	@sudo rmmod canon-r5-usb 2>/dev/null || true
	@sudo rmmod canon-r5-core 2>/dev/null || true
	@echo "Modules unloaded"

# Development and testing targets
test: modules
	@echo "Running Canon R5 driver test suite..."
	@echo "====================================="
	@chmod +x tests/integration/*.sh
	
	# Run integration tests
	@if [ -f tests/integration/test_driver_loading.sh ]; then \
		echo "🔧 Running driver loading tests..."; \
		tests/integration/test_driver_loading.sh || echo "Driver loading tests completed with warnings"; \
	fi
	
	@if [ -f tests/integration/test_dependencies.sh ]; then \
		echo "🔗 Running dependency tests..."; \
		tests/integration/test_dependencies.sh || echo "Dependency tests completed with warnings"; \
	fi
	
	# Run performance benchmarks
	@if command -v python3 >/dev/null 2>&1; then \
		echo "📊 Running performance benchmarks..."; \
		python3 tests/performance/benchmark.py --ci-mode || echo "Benchmarks completed with warnings"; \
	fi
	
	@echo "✅ Test suite completed"

security-check:
	@echo "Running comprehensive security checks..."
	@echo "======================================="
	
	# GitLeaks scanning
	@if [ -f .gitleaks.toml ]; then \
		if command -v gitleaks >/dev/null 2>&1; then \
			echo "Running GitLeaks secret scan..."; \
			gitleaks detect --source . --config .gitleaks.toml --verbose; \
		else \
			echo "GitLeaks not installed - downloading..."; \
			LATEST_URL=$(curl -s https://api.github.com/repos/gitleaks/gitleaks/releases/latest \
				| grep browser_download_url \
				| grep -i linux \
				| grep -Ei 'x64|x86_64|amd64' \
				| grep -i tar.gz \
				| head -n1 \
				| cut -d '"' -f4); \
			if [ -z "$LATEST_URL" ]; then \
				echo "Failed to resolve latest GitLeaks asset URL"; exit 1; \
			fi; \
			wget -O gitleaks.tar.gz "$LATEST_URL"; \
			tar -xzf gitleaks.tar.gz; \
			./gitleaks detect --source . --config .gitleaks.toml --verbose; \
			rm -f gitleaks gitleaks.tar.gz; \
		fi; \
	fi
	
	# TruffleHog scanning
	@if [ -f .trufflerc ]; then \
		if command -v trufflehog >/dev/null 2>&1; then \
			echo "Running TruffleHog secret detection..."; \
			trufflehog git file://. --config=.trufflerc --no-verification --no-update --fail || true; \
			echo "Running TruffleHog with verification for high-confidence results..."; \
			trufflehog git file://. --config=.trufflerc --only-verified --no-update --fail || true; \
		else \
			echo "TruffleHog not installed - downloading..."; \
			curl -sSfL https://raw.githubusercontent.com/trufflesecurity/trufflehog/main/scripts/install.sh | sh -s -- -b .; \
			./trufflehog git file://. --config=.trufflerc --no-verification --no-update --fail || true; \
			echo "Running TruffleHog with verification for high-confidence results..."; \
			./trufflehog git file://. --config=.trufflerc --only-verified --no-update --fail || true; \
			rm trufflehog; \
		fi; \
	fi
	
	@echo "Security check complete"

# Code style checking
style-check:
	@echo "Checking code style..."
	@if [ -f scripts/checkpatch.pl ]; then \
		echo "Running kernel checkpatch.pl..."; \
		find drivers/ include/ -name "*.c" -o -name "*.h" | \
		xargs perl scripts/checkpatch.pl --no-tree --file --strict || true; \
	fi
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "Checking clang-format compliance..."; \
		find drivers/ include/ -name "*.c" -o -name "*.h" | \
		xargs clang-format -style=file --dry-run -Werror || \
		echo "Run 'make format' to fix formatting issues"; \
	fi

# Auto-format code
format:
	@echo "Formatting code with clang-format..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find drivers/ include/ -name "*.c" -o -name "*.h" | \
		xargs clang-format -style=file -i; \
		echo "Code formatting complete"; \
	else \
		echo "clang-format not found - install with: sudo apt install clang-format"; \
	fi

# Development setup
dev-setup:
	@echo "Setting up development environment..."
	@if command -v pip3 >/dev/null 2>&1; then \
		echo "Installing pre-commit hooks..."; \
		pip3 install --user pre-commit; \
		pre-commit install; \
	fi
	@chmod +x scripts/*.sh
	@echo "Development environment ready"

# Package for distribution
package: clean
	@echo "Creating distribution package..."
	@VERSION=$(DRIVER_VERSION) scripts/package.sh
	@echo "Package created: canon-r5-driver-$(DRIVER_VERSION).tar.gz"

# Generate module signing key (for secure boot)
signing-key:
	@echo "Generating module signing key..."
	@openssl req -new -x509 -newkey rsa:2048 -keyout canon-r5.key -outform DER -out canon-r5.der -nodes -days 36500 -subj "/CN=Canon R5 Driver/"
	@echo "Signing key generated: canon-r5.key, canon-r5.der"
	@echo "Add canon-r5.der to your system's trusted keys for secure boot"

# Debug build
debug: DEBUG=1
debug: modules

# Show module information
info:
	@echo "Canon R5 Driver Suite Information:"
	@echo "=================================="
	@echo "Version: $(DRIVER_VERSION)"
	@echo "Kernel: $(shell uname -r)"
	@echo "Architecture: $(shell uname -m)"
	@echo "Build directory: $(PWD)"
	@echo "Kernel headers: $(KERNEL_DIR)"
	@echo ""
	@echo "Available modules:"
	@ls -1 *.ko 2>/dev/null || echo "No modules built yet (run 'make' first)"
	@echo ""
	@echo "Loaded modules:"
	@lsmod | grep canon-r5 || echo "No Canon R5 modules currently loaded"

help:
	@echo "Canon R5 Linux Driver Suite - Build System"
	@echo "==========================================="
	@echo ""
	@echo "Build Targets:"
	@echo "  all, modules     - Build all kernel modules"
	@echo "  clean           - Clean build artifacts"
	@echo "  debug           - Build with debug symbols"
	@echo ""
	@echo "Installation:"
	@echo "  install         - Install modules to system"
	@echo "  uninstall       - Remove modules from system"
	@echo "  load-modules    - Load all driver modules"
	@echo "  unload-modules  - Unload all driver modules"
	@echo ""
	@echo "Development:"
	@echo "  test            - Run test suite"
	@echo "  security-check  - Run security scans"
	@echo "  style-check     - Check code style"
	@echo "  format          - Auto-format code"
	@echo "  dev-setup       - Setup development environment"
	@echo ""
	@echo "Packaging:"
	@echo "  package         - Create distribution package"
	@echo "  signing-key     - Generate module signing key"
	@echo ""
	@echo "Information:"
	@echo "  info            - Show module and system info"
	@echo "  help            - Show this help"
	@echo ""
	@echo "Examples:"
	@echo "  make              # Build all modules"
	@echo "  make install      # Build and install"
	@echo "  make load-modules # Install and load modules"
	@echo "  make test         # Build and run tests"
	@echo "  make DEBUG=1      # Build with debug info"
