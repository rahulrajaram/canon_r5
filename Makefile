# SPDX-License-Identifier: GPL-2.0
#
# Canon R5 Linux Driver Suite
# Main Makefile for kernel module compilation
#
# Copyright (C) 2025 Canon R5 Driver Project

# Module name and version
MODULE_NAME := canon-r5
MODULE_VERSION := 0.1.0

# Define kernel directory and architecture
KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
ARCH := $(shell uname -m)

# Core modules
obj-m += canon-r5-core.o
obj-m += canon-r5-usb.o

# Video drivers
obj-m += canon-r5-video.o

# Core module object files
canon-r5-core-objs := drivers/core/canon-r5-core.o drivers/core/canon-r5-ptp.o
canon-r5-usb-objs := drivers/core/canon-r5-usb.o

# Video module object files
canon-r5-video-objs := drivers/video/canon-r5-v4l2.o \
                       drivers/video/canon-r5-videobuf2.o \
                       drivers/video/canon-r5-liveview.o

# Still image drivers
obj-m += canon-r5-still.o
canon-r5-still-objs := drivers/still/canon-r5-still.o

# Audio drivers (when implemented)
# obj-m += canon-r5-audio.o
# canon-r5-audio-objs := drivers/audio/canon-r5-alsa.o \
#                        drivers/audio/canon-r5-audio-sync.o

# Storage drivers (when implemented)
# obj-m += canon-r5-storage.o
# canon-r5-storage-objs := drivers/storage/canon-r5-mtp.o \
#                          drivers/storage/canon-r5-cards.o \
#                          drivers/storage/canon-r5-metadata.o

# Control drivers (when implemented)
# obj-m += canon-r5-control.o
# canon-r5-control-objs := drivers/control/canon-r5-sysfs.o \
#                          drivers/control/canon-r5-ioctl.o \
#                          drivers/control/canon-r5-settings.o

# Power management drivers (when implemented)
# obj-m += canon-r5-power.o
# canon-r5-power-objs := drivers/power/canon-r5-power.o \
#                        drivers/power/canon-r5-thermal.o \
#                        drivers/power/canon-r5-battery.o

# Input drivers (when implemented)
# obj-m += canon-r5-input.o
# canon-r5-input-objs := drivers/input/canon-r5-buttons.o \
#                        drivers/input/canon-r5-touchscreen.o \
#                        drivers/input/canon-r5-events.o

# Lens drivers (when implemented)
# obj-m += canon-r5-lens.o
# canon-r5-lens-objs := drivers/lens/canon-r5-lens.o \
#                       drivers/lens/canon-r5-rf.o \
#                       drivers/lens/canon-r5-ef.o

# Display drivers (when implemented)
# obj-m += canon-r5-display.o
# canon-r5-display-objs := drivers/display/canon-r5-lcd.o \
#                          drivers/display/canon-r5-evf.o \
#                          drivers/display/canon-r5-overlay.o

# Wireless drivers (when implemented)
# obj-m += canon-r5-wireless.o
# canon-r5-wireless-objs := drivers/wireless/canon-r5-gps.o \
#                           drivers/wireless/canon-r5-wifi.o \
#                           drivers/wireless/canon-r5-bluetooth.o

# Compiler flags
ccflags-y += -DCANON_R5_DRIVER_VERSION=\"$(MODULE_VERSION)\"
ccflags-y += -I$(PWD)/include
ccflags-y += -Wall -Wextra

# Enable debugging in development
ifdef DEBUG
ccflags-y += -DDEBUG -g
endif

# Default target
all: modules

# Build kernel modules
modules:
	@echo "Building Canon R5 Driver Suite v$(MODULE_VERSION)"
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

# Clean build artifacts
clean:
	@echo "Cleaning Canon R5 Driver Suite build artifacts"
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f Module.markers modules.order

# Install modules
install: modules
	@echo "Installing Canon R5 Driver Suite modules"
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	@echo "Running depmod to update module dependencies"
	/sbin/depmod -a

# Uninstall modules
uninstall:
	@echo "Uninstalling Canon R5 Driver Suite modules"
	@for mod in canon-r5-core canon-r5-usb; do \
		if [ -f /lib/modules/$(shell uname -r)/extra/$$mod.ko ]; then \
			rm -f /lib/modules/$(shell uname -r)/extra/$$mod.ko; \
			echo "Removed $$mod.ko"; \
		fi; \
	done
	/sbin/depmod -a

# Load modules
load: modules
	@echo "Loading Canon R5 Driver Suite modules"
	@if lsmod | grep -q canon_r5_core; then \
		echo "Canon R5 modules already loaded"; \
	else \
		insmod canon-r5-core.ko && \
		insmod canon-r5-usb.ko && \
		echo "Canon R5 modules loaded successfully"; \
	fi

# Unload modules
unload:
	@echo "Unloading Canon R5 Driver Suite modules"
	@-rmmod canon-r5-usb 2>/dev/null || true
	@-rmmod canon-r5-core 2>/dev/null || true
	@echo "Canon R5 modules unloaded"

# Reload modules
reload: unload load

# Check module status
status:
	@echo "Canon R5 Driver Suite module status:"
	@lsmod | grep canon_r5 || echo "No Canon R5 modules loaded"
	@echo ""
	@echo "USB devices:"
	@lsusb | grep -i canon || echo "No Canon devices found"

# Run checkpatch on source files
checkpatch:
	@echo "Running checkpatch on source files"
	@for file in drivers/*/*.c; do \
		if [ -f "$$file" ]; then \
			echo "Checking $$file"; \
			$(KERNEL_DIR)/scripts/checkpatch.pl --no-tree --file $$file || true; \
		fi; \
	done

# Build documentation
docs:
	@echo "Building documentation"
	@mkdir -p docs/html
	@if command -v doxygen >/dev/null 2>&1; then \
		doxygen Doxyfile 2>/dev/null || echo "Doxygen configuration not found"; \
	else \
		echo "Doxygen not installed, skipping documentation build"; \
	fi

# Development helpers
dev-setup:
	@echo "Setting up development environment"
	@echo "Enabling kernel debugging messages"
	@echo 'module canon_r5_core +p' | sudo tee /sys/kernel/debug/dynamic_debug/control 2>/dev/null || true
	@echo 'module canon_r5_usb +p' | sudo tee /sys/kernel/debug/dynamic_debug/control 2>/dev/null || true

# Test basic module loading
test: modules
	@echo "Testing Canon R5 module loading/unloading"
	@$(MAKE) unload 2>/dev/null || true
	@$(MAKE) load
	@sleep 2
	@$(MAKE) status
	@$(MAKE) unload
	@echo "Module test completed successfully"

# Package for distribution
package: clean
	@echo "Creating distribution package"
	@mkdir -p dist
	@tar czf dist/canon-r5-driver-$(MODULE_VERSION).tar.gz \
		--exclude=dist --exclude=.git \
		--transform 's,^,canon-r5-driver-$(MODULE_VERSION)/,' \
		*
	@echo "Package created: dist/canon-r5-driver-$(MODULE_VERSION).tar.gz"

# Help target
help:
	@echo "Canon R5 Driver Suite Build System"
	@echo ""
	@echo "Available targets:"
	@echo "  all        - Build all modules (default)"
	@echo "  modules    - Build kernel modules"
	@echo "  clean      - Clean build artifacts"
	@echo "  install    - Install modules to system"
	@echo "  uninstall  - Remove modules from system"
	@echo "  load       - Load modules into kernel"
	@echo "  unload     - Unload modules from kernel"
	@echo "  reload     - Unload and reload modules"
	@echo "  status     - Show module and device status"
	@echo "  checkpatch - Run kernel checkpatch on source"
	@echo "  docs       - Build documentation (requires doxygen)"
	@echo "  dev-setup  - Setup development environment"
	@echo "  test       - Test module loading/unloading"
	@echo "  package    - Create distribution package"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  DEBUG=1    - Enable debug build"
	@echo "  KERNEL_DIR - Override kernel build directory"

.PHONY: all modules clean install uninstall load unload reload status checkpatch docs dev-setup test package help