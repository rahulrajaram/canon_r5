#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Canon R5 Driver Loading Integration Tests
#
# Copyright (C) 2025 Canon R5 Driver Project

set -e

# Test configuration
SCRIPT_DIR="$(dirname "$0")"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_LOG="/tmp/canon_r5_test.log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1" | tee -a "$TEST_LOG"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" | tee -a "$TEST_LOG"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$TEST_LOG"
}

# Test functions
test_module_compilation() {
    log_info "Testing module compilation..."
    
    cd "$ROOT_DIR"
    
    # Clean previous build
    make clean >/dev/null 2>&1 || true
    
    # Build modules
    if make modules >>"$TEST_LOG" 2>&1; then
        log_info "✓ Module compilation successful"
        return 0
    else
        log_error "✗ Module compilation failed"
        return 1
    fi
}

test_module_dependencies() {
    log_info "Testing module dependencies..."
    
    local modules=("canon-r5-core.ko" "canon-r5-usb.ko" "canon-r5-video.ko" "canon-r5-still.ko" "canon-r5-audio.ko" "canon-r5-storage.ko")
    
    for module in "${modules[@]}"; do
        local path=""
        if [[ -f "$ROOT_DIR/$module" ]]; then
            path="$ROOT_DIR/$module"
        elif [[ -f "$ROOT_DIR/build/modules/$module" ]]; then
            path="$ROOT_DIR/build/modules/$module"
        fi

        if [[ -n "$path" ]]; then
            log_info "✓ Found module: $module"
            local info=$(modinfo "$path" 2>/dev/null || true)
            if [[ -n "$info" ]]; then
                log_info "  Module info available"
            else
                log_warn "  Module info not available"
            fi
        else
            log_error "✗ Missing module: $module"
            return 1
        fi
    done
    
    return 0
}

test_module_loading() {
    log_info "Testing module loading (requires root)..."
    
    if [[ $EUID -ne 0 ]]; then
        log_warn "Skipping module loading tests (requires root privileges)"
        return 0
    fi
    
    cd "$ROOT_DIR"
    
    # Unload any existing modules
    make unload >/dev/null 2>&1 || true
    
    # Test individual module loading
    local modules=("canon-r5-core" "canon-r5-usb")
    
    for module in "${modules[@]}"; do
        log_info "Loading module: $module"
        
        if insmod "${module}.ko" >>"/dev/null" 2>&1; then
            log_info "✓ Module $module loaded successfully"
            
            # Check if module is actually loaded
            if lsmod | grep -q "$module"; then
                log_info "  Module $module visible in lsmod"
            else
                log_error "  Module $module not visible in lsmod"
                return 1
            fi
            
            # Unload the module
            if rmmod "$module" >/dev/null 2>&1; then
                log_info "  Module $module unloaded successfully"
            else
                log_warn "  Failed to unload module $module"
            fi
        else
            log_error "✗ Failed to load module: $module"
            return 1
        fi
    done
    
    return 0
}

test_sysfs_interface() {
    log_info "Testing sysfs interface..."
    
    if [[ $EUID -ne 0 ]]; then
        log_warn "Skipping sysfs tests (requires root privileges)"
        return 0
    fi
    
    cd "$ROOT_DIR"
    
    # Load core module
    if insmod canon-r5-core.ko >/dev/null 2>&1; then
        log_info "✓ Core module loaded for sysfs testing"
        
        # Check for sysfs entries
        if [[ -d /sys/class/canon-r5 ]]; then
            log_info "✓ Canon R5 class directory exists"
            
            # Check version attribute
            if [[ -f /sys/class/canon-r5/version ]]; then
                local version=$(cat /sys/class/canon-r5/version 2>/dev/null)
                log_info "✓ Version attribute: $version"
            else
                log_warn "Version attribute not found"
            fi
        else
            log_warn "Canon R5 class directory not found"
        fi
        
        # Unload module
        rmmod canon-r5-core >/dev/null 2>&1 || true
    else
        log_error "✗ Failed to load core module for sysfs testing"
        return 1
    fi
    
    return 0
}

test_error_conditions() {
    log_info "Testing error condition handling..."
    
    # Test loading non-existent module
    if insmod non-existent.ko >/dev/null 2>&1; then
        log_error "✗ Should not be able to load non-existent module"
        return 1
    else
        log_info "✓ Properly rejects non-existent module"
    fi
    
    # Test loading with missing dependencies (if applicable)
    if [[ $EUID -eq 0 ]] && [[ -f "$ROOT_DIR/canon-r5-video.ko" ]]; then
        # Try to load video module without core module
        if insmod "$ROOT_DIR/canon-r5-video.ko" >/dev/null 2>&1; then
            log_warn "Video module loaded without dependencies (may be expected)"
            rmmod canon-r5-video >/dev/null 2>&1 || true
        else
            log_info "✓ Properly handles missing dependencies"
        fi
    fi
    
    return 0
}

# Test device detection (mock)
test_device_detection() {
    log_info "Testing device detection (simulated)..."
    
    # Check for USB debugging tools
    if command -v lsusb >/dev/null 2>&1; then
        log_info "✓ lsusb available for USB device detection"
        
        # Look for Canon devices (just for demonstration)
        local canon_devices=$(lsusb | grep -i canon || true)
        if [[ -n "$canon_devices" ]]; then
            log_info "Canon USB devices found:"
            echo "$canon_devices" | while read line; do
                log_info "  $line"
            done
        else
            log_info "No Canon USB devices detected"
        fi
    else
        log_warn "lsusb not available"
    fi
    
    return 0
}

# Main test runner
run_integration_tests() {
    log_info "Starting Canon R5 Driver Integration Tests"
    log_info "=========================================="
    
    local failed_tests=0
    local total_tests=0
    
    # List of test functions
    local tests=(
        "test_module_compilation"
        "test_module_dependencies"
        "test_module_loading"
        "test_sysfs_interface"
        "test_error_conditions"
        "test_device_detection"
    )
    
    # Run each test
    for test_func in "${tests[@]}"; do
        total_tests=$((total_tests + 1))
        log_info ""
        log_info "Running: $test_func"
        log_info "------------------------"
        
        if $test_func; then
            log_info "✓ $test_func PASSED"
        else
            log_error "✗ $test_func FAILED"
            failed_tests=$((failed_tests + 1))
        fi
    done
    
    # Summary
    log_info ""
    log_info "Test Summary"
    log_info "============"
    log_info "Total tests: $total_tests"
    log_info "Passed: $((total_tests - failed_tests))"
    log_info "Failed: $failed_tests"
    
    if [[ $failed_tests -eq 0 ]]; then
        log_info "🎉 ALL TESTS PASSED!"
        return 0
    else
        log_error "❌ $failed_tests TEST(S) FAILED"
        return 1
    fi
}

# Cleanup function
cleanup() {
    log_info "Cleaning up..."
    
    if [[ $EUID -eq 0 ]]; then
        # Unload any loaded modules
        for module in canon-r5-storage canon-r5-audio canon-r5-still canon-r5-video canon-r5-usb canon-r5-core; do
            rmmod "$module" >/dev/null 2>&1 || true
        done
    fi
}

# Set up signal handlers
trap cleanup EXIT
trap 'log_error "Test interrupted"; exit 1' INT TERM

# Initialize test log
echo "Canon R5 Driver Integration Tests - $(date)" > "$TEST_LOG"

# Check if running in test environment
if [[ "${CANON_R5_TEST_ENV:-}" == "1" ]]; then
    log_info "Running in test environment mode"
fi

# Run the tests
run_integration_tests
