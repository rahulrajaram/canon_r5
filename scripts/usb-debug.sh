#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Canon R5 USB Debugging Script
# Enables detailed USB monitoring and debugging
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

# Logging function
log() {
    echo -e "${BLUE}[USB-DEBUG]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[USB-DEBUG]${NC} $1"
}

error() {
    echo -e "${RED}[USB-DEBUG]${NC} $1"
}

success() {
    echo -e "${GREEN}[USB-DEBUG]${NC} $1"
}

# Check if running as root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        error "This script must be run as root for USB debugging setup"
        error "Please run: sudo $0 $*"
        exit 1
    fi
}

# Enable USB core debugging
enable_usb_debugging() {
    log "Enabling USB core debugging..."
    
    # Enable dynamic debug for USB core
    if [[ -f /sys/kernel/debug/dynamic_debug/control ]]; then
        echo 'module usbcore +p' > /sys/kernel/debug/dynamic_debug/control || warn "Failed to enable usbcore debugging"
        echo 'module usb_common +p' > /sys/kernel/debug/dynamic_debug/control || warn "Failed to enable usb_common debugging"
        echo 'file drivers/usb/core/* +p' > /sys/kernel/debug/dynamic_debug/control || warn "Failed to enable USB core file debugging"
        success "USB core debugging enabled"
    else
        warn "Dynamic debug not available - debugging messages may be limited"
    fi
    
    # Enable USB debugging in kernel parameters (if supported)
    if [[ -f /sys/module/usbcore/parameters/usbfs_snoop ]]; then
        echo Y > /sys/module/usbcore/parameters/usbfs_snoop || warn "Failed to enable usbfs snooping"
        success "USB filesystem snooping enabled"
    fi
}

# Enable Canon R5 driver debugging
enable_driver_debugging() {
    log "Enabling Canon R5 driver debugging..."
    
    if [[ -f /sys/kernel/debug/dynamic_debug/control ]]; then
        echo 'module canon_r5_core +p' > /sys/kernel/debug/dynamic_debug/control || warn "Canon R5 core module not loaded"
        echo 'module canon_r5_usb +p' > /sys/kernel/debug/dynamic_debug/control || warn "Canon R5 USB module not loaded"
        success "Canon R5 driver debugging enabled"
    fi
}

# Start USB monitoring
start_usb_monitoring() {
    log "Starting USB monitoring..."
    
    # Check for usbmon
    if ! lsmod | grep -q usbmon; then
        modprobe usbmon || warn "Failed to load usbmon module"
    fi
    
    # Check if debugfs is mounted
    if ! mount | grep -q debugfs; then
        mount -t debugfs none /sys/kernel/debug || warn "Failed to mount debugfs"
    fi
    
    # List available USB buses for monitoring
    if [[ -d /sys/kernel/debug/usb ]]; then
        log "Available USB buses for monitoring:"
        ls -la /sys/kernel/debug/usb/
        
        log "To monitor USB traffic, use:"
        log "  cat /sys/kernel/debug/usb/usbmon/0u > usb_trace.log &"
        log "  # or for specific bus: cat /sys/kernel/debug/usb/usbmon/1u > usb_trace.log &"
        
        success "USB monitoring ready"
    else
        warn "USB monitoring interface not available"
    fi
}

# Scan for Canon devices
scan_canon_devices() {
    log "Scanning for Canon devices..."
    
    # Look for Canon devices
    CANON_DEVICES=$(lsusb | grep -i canon || true)
    
    if [[ -n "$CANON_DEVICES" ]]; then
        success "Found Canon devices:"
        echo "$CANON_DEVICES" | while IFS= read -r line; do
            log "  $line"
            
            # Extract bus and device numbers
            BUS=$(echo "$line" | sed -n 's/Bus \([0-9]\+\) Device \([0-9]\+\).*/\1/p')
            DEV=$(echo "$line" | sed -n 's/Bus \([0-9]\+\) Device \([0-9]\+\).*/\2/p')
            
            if [[ -n "$BUS" && -n "$DEV" ]]; then
                log "  Detailed info for Bus $BUS Device $DEV:"
                lsusb -v -s "$BUS:$DEV" 2>/dev/null | head -20 || warn "Failed to get detailed device info"
            fi
        done
    else
        warn "No Canon devices found. Connect your Canon R5 and run this script again."
        
        log "All USB devices:"
        lsusb
    fi
}

# Setup USB debugging environment
setup_debug_environment() {
    log "Setting up USB debugging environment..."
    
    # Create debug log directory
    DEBUG_DIR="$PROJECT_DIR/debug"
    mkdir -p "$DEBUG_DIR"
    
    # Create USB monitor script
    cat > "$DEBUG_DIR/start-usb-monitor.sh" << 'EOF'
#!/bin/bash
# Start USB monitoring for Canon R5 debugging
DEBUG_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_FILE="$DEBUG_DIR/usb-traffic-$(date +%Y%m%d-%H%M%S).log"

echo "Starting USB monitoring, log file: $LOG_FILE"
echo "Press Ctrl+C to stop"

if [[ -f /sys/kernel/debug/usb/usbmon/0u ]]; then
    cat /sys/kernel/debug/usb/usbmon/0u > "$LOG_FILE"
else
    echo "ERROR: USB monitoring not available"
    echo "Run usb-debug.sh as root first"
    exit 1
fi
EOF
    
    chmod +x "$DEBUG_DIR/start-usb-monitor.sh"
    
    # Create dmesg monitor script
    cat > "$DEBUG_DIR/watch-kernel-logs.sh" << 'EOF'
#!/bin/bash
# Watch kernel logs for Canon R5 and USB messages
echo "Watching kernel logs for Canon R5 and USB messages..."
echo "Press Ctrl+C to stop"

dmesg -w | grep -E "(canon.r5|usb|USB|Canon)"
EOF
    
    chmod +x "$DEBUG_DIR/watch-kernel-logs.sh"
    
    success "Debug environment set up in $DEBUG_DIR"
    log "Available scripts:"
    log "  $DEBUG_DIR/start-usb-monitor.sh - Monitor USB traffic"
    log "  $DEBUG_DIR/watch-kernel-logs.sh - Watch kernel messages"
}

# Create USB analysis tools
create_analysis_tools() {
    log "Creating USB analysis tools..."
    
    DEBUG_DIR="$PROJECT_DIR/debug"
    
    # Create USB descriptor analyzer
    cat > "$DEBUG_DIR/analyze-usb.py" << 'EOF'
#!/usr/bin/env python3
"""
Canon R5 USB Descriptor Analyzer
Parses and analyzes USB device descriptors for Canon R5
"""

import sys
import subprocess
import re

def parse_lsusb_output(bus, device):
    """Parse detailed lsusb output for a specific device"""
    try:
        result = subprocess.run(['lsusb', '-v', '-s', f'{bus}:{device}'], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            return result.stdout
        else:
            return None
    except Exception as e:
        print(f"Error running lsusb: {e}")
        return None

def find_canon_devices():
    """Find all Canon USB devices"""
    try:
        result = subprocess.run(['lsusb'], capture_output=True, text=True)
        if result.returncode != 0:
            return []
        
        devices = []
        for line in result.stdout.split('\n'):
            if 'Canon' in line or '04a9:' in line.lower():
                match = re.match(r'Bus (\d+) Device (\d+): ID ([0-9a-f]{4}):([0-9a-f]{4})(.*)', line)
                if match:
                    devices.append({
                        'bus': match.group(1),
                        'device': match.group(2),
                        'vid': match.group(3),
                        'pid': match.group(4),
                        'description': match.group(5).strip()
                    })
        return devices
    except Exception as e:
        print(f"Error finding Canon devices: {e}")
        return []

def analyze_device(device):
    """Analyze a Canon USB device"""
    print(f"\n=== Analyzing Canon Device ===")
    print(f"Bus: {device['bus']}, Device: {device['device']}")
    print(f"VID:PID: {device['vid']}:{device['pid']}")
    print(f"Description: {device['description']}")
    
    detailed_info = parse_lsusb_output(device['bus'], device['device'])
    if detailed_info:
        # Extract key information
        interfaces = re.findall(r'Interface Descriptor:.*?\n(.*?\n)*?', detailed_info, re.MULTILINE)
        endpoints = re.findall(r'Endpoint Descriptor:.*?\n(.*?\n)*?', detailed_info, re.MULTILINE)
        
        print(f"\nFound {len(interfaces)} interfaces and {len(endpoints)} endpoints")
        
        # Look for PTP interface
        if 'Still Image Capture' in detailed_info or 'bInterfaceClass.*6' in detailed_info:
            print("✓ PTP/MTP (Still Image Capture) interface detected")
        
        # Look for bulk endpoints
        bulk_in = re.findall(r'bEndpointAddress\s+0x([0-9a-f]+).*?Transfer Type.*?Bulk.*?Direction.*?IN', detailed_info, re.IGNORECASE | re.DOTALL)
        bulk_out = re.findall(r'bEndpointAddress\s+0x([0-9a-f]+).*?Transfer Type.*?Bulk.*?Direction.*?OUT', detailed_info, re.IGNORECASE | re.DOTALL)
        interrupt_in = re.findall(r'bEndpointAddress\s+0x([0-9a-f]+).*?Transfer Type.*?Interrupt.*?Direction.*?IN', detailed_info, re.IGNORECASE | re.DOTALL)
        
        print(f"\nEndpoints:")
        for ep in bulk_in:
            print(f"  Bulk IN: 0x{ep}")
        for ep in bulk_out:
            print(f"  Bulk OUT: 0x{ep}")
        for ep in interrupt_in:
            print(f"  Interrupt IN: 0x{ep}")
        
        print("\n" + "="*50)
        print("Raw lsusb output:")
        print("="*50)
        print(detailed_info[:2000])  # First 2000 chars
        if len(detailed_info) > 2000:
            print("... (truncated)")

def main():
    print("Canon R5 USB Analyzer")
    print("=" * 30)
    
    devices = find_canon_devices()
    
    if not devices:
        print("No Canon devices found.")
        print("Make sure your Canon R5 is connected and recognized by the system.")
        return
    
    for device in devices:
        analyze_device(device)
    
    print(f"\nAnalyzed {len(devices)} Canon device(s)")

if __name__ == '__main__':
    main()
EOF
    
    chmod +x "$DEBUG_DIR/analyze-usb.py"
    
    success "USB analysis tools created in $DEBUG_DIR"
    log "  $DEBUG_DIR/analyze-usb.py - Analyze Canon USB devices"
}

# Print usage information
print_usage() {
    echo "Canon R5 USB Debugging Setup"
    echo ""
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Options:"
    echo "  setup     - Complete debugging setup (requires root)"
    echo "  scan      - Scan for Canon devices (no root required)"
    echo "  monitor   - Start USB monitoring (requires root)"
    echo "  analyze   - Analyze connected Canon devices"
    echo "  help      - Show this help message"
    echo ""
    echo "Examples:"
    echo "  sudo $0 setup      # Initial setup"
    echo "  $0 scan            # Check for devices"
    echo "  sudo $0 monitor    # Start monitoring"
    echo "  $0 analyze         # Analyze devices"
}

# Main function
main() {
    case "${1:-setup}" in
        setup)
            check_root
            log "Starting USB debugging setup..."
            enable_usb_debugging
            enable_driver_debugging
            start_usb_monitoring
            setup_debug_environment
            create_analysis_tools
            scan_canon_devices
            success "USB debugging setup complete!"
            echo ""
            echo "Next steps:"
            echo "1. Connect your Canon R5"
            echo "2. Run: $0 scan"
            echo "3. Load Canon R5 drivers: make load"
            echo "4. Monitor USB traffic: debug/start-usb-monitor.sh"
            ;;
        scan)
            scan_canon_devices
            ;;
        monitor)
            check_root
            enable_usb_debugging
            start_usb_monitoring
            ;;
        analyze)
            if [[ -f "$PROJECT_DIR/debug/analyze-usb.py" ]]; then
                "$PROJECT_DIR/debug/analyze-usb.py"
            else
                error "Analysis tool not found. Run '$0 setup' first."
            fi
            ;;
        help|--help|-h)
            print_usage
            ;;
        *)
            error "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
}

# Run main function
main "$@"