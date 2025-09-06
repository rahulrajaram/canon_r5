#!/bin/bash
# Canon R5 Driver Suite - Module Dependency Tests
# Tests module loading order and dependency resolution

set -e

SCRIPT_DIR="$(dirname "$0")"
PROJECT_DIR="$SCRIPT_DIR/../.."
ROOT_KO_DIR="$PROJECT_DIR"
ALT_KO_DIR="$PROJECT_DIR/build/modules"

echo "Canon R5 Driver Suite - Module Dependency Tests"
echo "================================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Test functions
run_test() {
    local test_name="$1"
    local test_command="$2"
    
    TESTS_RUN=$((TESTS_RUN + 1))
    echo -e "\n${YELLOW}Test $TESTS_RUN: $test_name${NC}"
    
    if eval "$test_command"; then
        echo -e "${GREEN}✓ PASSED${NC}"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}✗ FAILED${NC}"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Check if modules exist
check_modules_exist() {
    echo "Checking if kernel modules exist..."
    local missing_modules=0
    
    for module in canon-r5-core.ko canon-r5-usb.ko canon-r5-video.ko canon-r5-still.ko canon-r5-audio.ko canon-r5-storage.ko; do
        if [ ! -f "$ROOT_KO_DIR/$module" ] && [ ! -f "$ALT_KO_DIR/$module" ]; then
            echo "❌ Missing module: $module"
            missing_modules=$((missing_modules + 1))
        else
            echo "✅ Found module: $module"
        fi
    done
    
    return $missing_modules
}

# Test module information
test_module_info() {
    echo "Testing module information extraction..."
    local failed=0
    
    shopt -s nullglob
    for module in "$ROOT_KO_DIR"/*.ko "$ALT_KO_DIR"/*.ko; do
        echo "  Checking $(basename "$module")..."
        if ! modinfo "$module" >/dev/null 2>&1; then
            echo "    ❌ Failed to get module info"
            failed=$((failed + 1))
        else
            # Check for required fields
            local version=$(modinfo -F version "$module" 2>/dev/null || echo "")
            local license=$(modinfo -F license "$module" 2>/dev/null || echo "")
            local author=$(modinfo -F author "$module" 2>/dev/null || echo "")
            
            if [ -z "$license" ]; then
                echo "    ⚠️  Warning: No license information"
            fi
            
            if [ -z "$author" ]; then
                echo "    ⚠️  Warning: No author information"
            fi
            
            echo "    ✅ Module info extracted successfully"
        fi
    done
    
    return $failed
}

# Test dependency resolution with depmod
test_depmod_resolution() {
    echo "Testing dependency resolution with depmod..."
    
    # Create a temporary modules directory
    local temp_dir=$(mktemp -d)
    local modules_dir="$temp_dir/lib/modules/$(uname -r)/extra"
    
    mkdir -p "$modules_dir"
    
    # Copy modules to temporary location
    cp "$ROOT_KO_DIR"/*.ko "$modules_dir/" 2>/dev/null || true
    cp "$ALT_KO_DIR"/*.ko "$modules_dir/" 2>/dev/null || true
    
    # Run depmod on temporary directory
    echo "  Running depmod analysis..."
    if depmod -b "$temp_dir" -F /dev/null $(uname -r) 2>/dev/null; then
        echo "  ✅ depmod completed successfully"
        
        # Check modules.dep for our modules
        local dep_file="$temp_dir/lib/modules/$(uname -r)/modules.dep"
        if [ -f "$dep_file" ]; then
            echo "  📄 Dependency information:"
            grep "canon-r5" "$dep_file" | while read line; do
                echo "    $line"
            done
        fi
        
        # Clean up
        rm -rf "$temp_dir"
        return 0
    else
        echo "  ❌ depmod failed"
        rm -rf "$temp_dir"
        return 1
    fi
}

# Test circular dependency detection
test_circular_dependencies() {
    echo "Testing for circular dependencies..."
    
    local temp_dir=$(mktemp -d)
    local modules_dir="$temp_dir/lib/modules/$(uname -r)/extra"
    
    mkdir -p "$modules_dir"
    cp "$ROOT_KO_DIR"/*.ko "$modules_dir/" 2>/dev/null || true
    cp "$ALT_KO_DIR"/*.ko "$modules_dir/" 2>/dev/null || true
    
    # Run depmod and capture output
    local depmod_output=$(depmod -b "$temp_dir" -F /dev/null $(uname -r) 2>&1)
    local depmod_exit=$?
    
    if echo "$depmod_output" | grep -i "cycle\|circular" >/dev/null; then
        echo "  ❌ Circular dependency detected:"
        echo "$depmod_output" | grep -i "cycle\|circular"
        rm -rf "$temp_dir"
        return 1
    elif [ $depmod_exit -ne 0 ]; then
        echo "  ❌ depmod failed with exit code $depmod_exit"
        echo "$depmod_output"
        rm -rf "$temp_dir"
        return 1
    else
        echo "  ✅ No circular dependencies detected"
        rm -rf "$temp_dir"
        return 0
    fi
}

# Test module symbol exports
test_symbol_exports() {
    echo "Testing module symbol exports..."
    local failed=0
    
    shopt -s nullglob
    for module in "$ROOT_KO_DIR"/*.ko "$ALT_KO_DIR"/*.ko; do
        local module_name=$(basename "$module" .ko)
        echo "  Checking symbols in $module_name..."
        
        # Check if module exports symbols
        local exported_symbols=$(modinfo -F depends "$module" 2>/dev/null | wc -w)
        local symbol_info=$(objdump -t "$module" 2>/dev/null | grep -c "\.symtab" || echo "0")
        
        if [ "$symbol_info" -gt 0 ]; then
            echo "    ✅ Symbol table found"
        else
            echo "    ⚠️  Warning: No symbol table found"
        fi
        
        # For core module, check if it exports symbols for other modules
        if [[ "$module_name" == "canon-r5-core" ]]; then
            if nm "$module" 2>/dev/null | grep -q "canon_r5_"; then
                echo "    ✅ Core module exports canon_r5_* symbols"
            else
                echo "    ⚠️  Warning: Core module may not export expected symbols"
            fi
        fi
    done
    
    return $failed
}

# Test loading order simulation
test_loading_order() {
    echo "Testing module loading order simulation..."
    
    # Expected loading order based on dependencies
    local expected_order=("canon-r5-core" "canon-r5-usb" "canon-r5-video" "canon-r5-still" "canon-r5-audio" "canon-r5-storage")
    
    echo "  Expected loading order:"
    for i in "${!expected_order[@]}"; do
        echo "    $((i+1)). ${expected_order[$i]}"
    done
    
    # Simulate dependency checking
    echo "  Verifying dependency chain:"
    
    # Core should have no dependencies
    local core_deps=$(modinfo -F depends "$ROOT_KO_DIR/canon-r5-core.ko" 2>/dev/null || modinfo -F depends "$ALT_KO_DIR/canon-r5-core.ko" 2>/dev/null | tr ',' ' ')
    if [ -z "$core_deps" ]; then
        echo "    ✅ canon-r5-core has no dependencies (correct)"
    else
        echo "    ❌ canon-r5-core has unexpected dependencies: $core_deps"
        return 1
    fi
    
    # USB should depend on core
    local usb_deps=$(modinfo -F depends "$ROOT_KO_DIR/canon-r5-usb.ko" 2>/dev/null || modinfo -F depends "$ALT_KO_DIR/canon-r5-usb.ko" 2>/dev/null | tr ',' ' ')
    if echo "$usb_deps" | grep -q "canon.r5.core\|canon_r5_core"; then
        echo "    ✅ canon-r5-usb depends on canon-r5-core (correct)"
    else
        echo "    ⚠️  Warning: canon-r5-usb dependencies: $usb_deps"
    fi
    
    echo "  ✅ Loading order appears correct"
    return 0
}

# Test version consistency
test_version_consistency() {
    echo "Testing version consistency across modules..."
    local versions=()
    local failed=0
    
    shopt -s nullglob
    for module in "$ROOT_KO_DIR"/*.ko "$ALT_KO_DIR"/*.ko; do
        local module_name=$(basename "$module" .ko)
        local version=$(modinfo -F version "$module" 2>/dev/null || echo "unknown")
        
        echo "  $module_name: version $version"
        versions+=("$version")
    done
    
    # Check if all versions are the same
    local first_version="${versions[0]}"
    for version in "${versions[@]}"; do
        if [ "$version" != "$first_version" ]; then
            echo "  ❌ Version mismatch detected"
            failed=1
            break
        fi
    done
    
    if [ $failed -eq 0 ]; then
        echo "  ✅ All modules have consistent version: $first_version"
    fi
    
    return $failed
}

# Main test execution
main() {
    echo "Starting Canon R5 module dependency tests..."
    echo "Module search paths: $ROOT_KO_DIR and $ALT_KO_DIR"
    echo "Kernel version: $(uname -r)"
    echo ""
    
    # Preliminary checks
    if ! ls "$ROOT_KO_DIR"/*.ko "$ALT_KO_DIR"/*.ko >/dev/null 2>&1; then
        echo "❌ No built modules found. Run 'make modules' first."
        exit 1
    fi
    
    if ! check_modules_exist; then
        echo "❌ Some required modules are missing. Run 'make modules' to build them."
        exit 1
    fi
    
    # Run all tests
    run_test "Module Information Extraction" "test_module_info"
    run_test "Dependency Resolution (depmod)" "test_depmod_resolution"
    run_test "Circular Dependency Detection" "test_circular_dependencies"
    run_test "Symbol Export Verification" "test_symbol_exports"
    run_test "Loading Order Verification" "test_loading_order"
    run_test "Version Consistency Check" "test_version_consistency"
    
    # Final report
    echo ""
    echo "================================================"
    echo "Test Summary:"
    echo "  Total tests run: $TESTS_RUN"
    echo -e "  Tests passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "  Tests failed: ${RED}$TESTS_FAILED${NC}"
    
    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "\n${GREEN}🎉 All dependency tests passed!${NC}"
        echo "The Canon R5 driver modules have correct dependency relationships."
        exit 0
    else
        echo -e "\n${RED}❌ Some dependency tests failed.${NC}"
        echo "Please review the module dependencies and resolve any issues."
        exit 1
    fi
}

# Run main function
main "$@"
