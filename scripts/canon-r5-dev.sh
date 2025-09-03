#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Canon R5 Development Helper Script
# Quick development tasks and shortcuts
#
# Copyright (C) 2025 Canon R5 Driver Project

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log() { echo -e "${BLUE}[CANON-R5]${NC} $1"; }
warn() { echo -e "${YELLOW}[CANON-R5]${NC} $1"; }
error() { echo -e "${RED}[CANON-R5]${NC} $1"; }
success() { echo -e "${GREEN}[CANON-R5]${NC} $1"; }

# Build and test modules
build_test() {
    log "Building and testing Canon R5 modules..."
    
    cd "$PROJECT_DIR"
    
    # Clean and build
    make clean
    make modules
    
    if [[ $? -eq 0 ]]; then
        success "Build successful!"
        
        # Check module information
        log "Module information:"
        file canon-r5-core.ko
        file canon-r5-usb.ko
        
        # Show module symbols (first 10)
        log "Core module symbols (first 10):"
        nm canon-r5-core.ko | head -10
        
        success "Build and test completed successfully"
    else
        error "Build failed!"
        return 1
    fi
}

# Check kernel coding style
check_style() {
    log "Checking kernel coding style..."
    
    cd "$PROJECT_DIR"
    
    if make checkpatch; then
        success "Code style check passed"
    else
        warn "Code style issues found - please review"
    fi
}

# Monitor system for Canon R5 activity
monitor_system() {
    log "Monitoring system for Canon R5 activity..."
    log "Connect/disconnect your Canon R5 to see activity"
    log "Press Ctrl+C to stop"
    echo ""
    
    # Monitor kernel messages
    dmesg -w | grep -E "(canon.r5|usb|USB|Canon|PTP)" --color=always
}

# Quick development cycle
dev_cycle() {
    log "Running development cycle: clean -> build -> style check"
    
    cd "$PROJECT_DIR"
    
    # Build
    if ! build_test; then
        error "Build failed - stopping dev cycle"
        return 1
    fi
    
    # Style check
    check_style
    
    success "Development cycle completed"
}

# Show project status
show_status() {
    log "Canon R5 Driver Project Status"
    echo ""
    
    cd "$PROJECT_DIR"
    
    # Project info
    echo "📁 Project Directory: $PROJECT_DIR"
    echo "📊 Total Source Files: $(find drivers/ -name "*.c" | wc -l)"
    echo "📋 Header Files: $(find include/ -name "*.h" | wc -l)"
    echo "🔧 Scripts: $(find scripts/ -name "*.sh" -o -name "*.py" | wc -l)"
    
    echo ""
    
    # Build status
    if [[ -f "canon-r5-core.ko" && -f "canon-r5-usb.ko" ]]; then
        echo "✅ Modules: Built ($(stat -c%y canon-r5-core.ko | cut -d' ' -f1))"
        echo "   - canon-r5-core.ko: $(stat -c%s canon-r5-core.ko) bytes"
        echo "   - canon-r5-usb.ko: $(stat -c%s canon-r5-usb.ko) bytes"
    else
        echo "❌ Modules: Not built"
    fi
    
    echo ""
    
    # USB devices
    CANON_DEVICES=$(lsusb | grep -i canon || true)
    if [[ -n "$CANON_DEVICES" ]]; then
        echo "🔌 Canon Devices Connected:"
        echo "$CANON_DEVICES" | sed 's/^/   /'
    else
        echo "🔌 Canon Devices: None connected"
    fi
    
    echo ""
    
    # Module status
    LOADED_MODULES=$(lsmod | grep canon_r5 || true)
    if [[ -n "$LOADED_MODULES" ]]; then
        echo "🔧 Canon R5 Modules Loaded:"
        echo "$LOADED_MODULES" | sed 's/^/   /'
    else
        echo "🔧 Canon R5 Modules: Not loaded"
    fi
    
    echo ""
    
    # Development tools status
    echo "🛠️  Development Tools:"
    [[ -x "scripts/usb-debug.sh" ]] && echo "   ✅ USB Debugging" || echo "   ❌ USB Debugging"
    [[ -x "scripts/ptp-analyzer.py" ]] && echo "   ✅ PTP Analyzer" || echo "   ❌ PTP Analyzer"
    [[ -d "debug" ]] && echo "   ✅ Debug Directory" || echo "   ❌ Debug Directory"
}

# Setup development environment
setup_dev() {
    log "Setting up Canon R5 development environment..."
    
    cd "$PROJECT_DIR"
    
    # Make sure all scripts are executable
    find scripts/ -name "*.sh" -exec chmod +x {} \;
    find scripts/ -name "*.py" -exec chmod +x {} \;
    
    # Create debug directory
    mkdir -p debug logs
    
    # Setup git hooks if in git repo
    if [[ -d ".git" ]]; then
        log "Setting up git hooks..."
        mkdir -p .git/hooks
        
        # Pre-commit hook for style checking
        cat > .git/hooks/pre-commit << 'EOF'
#!/bin/bash
# Canon R5 pre-commit hook - check coding style
echo "Checking coding style..."
make checkpatch
if [[ $? -ne 0 ]]; then
    echo "❌ Coding style check failed!"
    echo "Fix style issues before committing or use 'git commit --no-verify'"
    exit 1
fi
echo "✅ Coding style check passed"
EOF
        chmod +x .git/hooks/pre-commit
        success "Git hooks installed"
    fi
    
    # Create development aliases
    cat > debug/aliases.sh << 'EOF'
# Canon R5 Development Aliases
# Source this file: source debug/aliases.sh

alias r5-build='make modules'
alias r5-clean='make clean'
alias r5-test='make test'
alias r5-status='./scripts/canon-r5-dev.sh status'
alias r5-monitor='./scripts/canon-r5-dev.sh monitor'
alias r5-usb='./scripts/usb-debug.sh scan'
alias r5-ptp='./scripts/ptp-analyzer.py'

echo "Canon R5 development aliases loaded:"
echo "  r5-build   - Build modules"
echo "  r5-clean   - Clean build"
echo "  r5-test    - Run tests"
echo "  r5-status  - Show status"
echo "  r5-monitor - Monitor activity"
echo "  r5-usb     - USB device scan"
echo "  r5-ptp     - PTP analyzer"
EOF
    
    success "Development environment setup completed"
    log "Source aliases with: source debug/aliases.sh"
}

# Print usage
print_usage() {
    echo "Canon R5 Development Helper"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  build      - Build and test modules"
    echo "  style      - Check coding style"
    echo "  monitor    - Monitor system for Canon R5 activity"
    echo "  cycle      - Run full development cycle"
    echo "  status     - Show project status"
    echo "  setup      - Setup development environment"
    echo "  help       - Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 build         # Build modules"
    echo "  $0 cycle         # Build + style check"
    echo "  $0 monitor       # Monitor for USB activity"
    echo "  $0 status        # Show current status"
}

# Main function
main() {
    case "${1:-status}" in
        build)
            build_test
            ;;
        style)
            check_style
            ;;
        monitor)
            monitor_system
            ;;
        cycle)
            dev_cycle
            ;;
        status)
            show_status
            ;;
        setup)
            setup_dev
            ;;
        help|--help|-h)
            print_usage
            ;;
        *)
            error "Unknown command: $1"
            print_usage
            exit 1
            ;;
    esac
}

# Run main
main "$@"