#!/usr/bin/env python3
"""
Canon R5 PTP Protocol Analyzer
Analyzes PTP command structures and Canon extensions

Copyright (C) 2025 Canon R5 Driver Project
SPDX-License-Identifier: GPL-2.0
"""

import struct
import sys
from typing import Dict, List, Tuple, Optional

# PTP Container Types
PTP_CONTAINER_TYPES = {
    0x0001: "COMMAND",
    0x0002: "DATA", 
    0x0003: "RESPONSE",
    0x0004: "EVENT"
}

# Standard PTP Operation Codes
PTP_OPERATIONS = {
    0x1001: "GetDeviceInfo",
    0x1002: "OpenSession",
    0x1003: "CloseSession",
    0x1004: "GetStorageIDs",
    0x1005: "GetStorageInfo",
    0x1006: "GetNumObjects",
    0x1007: "GetObjectHandles",
    0x1008: "GetObjectInfo",
    0x1009: "GetObject",
    0x100A: "DeleteObject",
    0x100E: "InitiateCapture",
    0x1014: "GetDevicePropDesc",
    0x1015: "GetDevicePropValue",
    0x1016: "SetDevicePropValue",
}

# Canon PTP Extensions
CANON_PTP_OPERATIONS = {
    0x9101: "GetChanges",
    0x9102: "GetFolderInfo", 
    0x9103: "CreateFolder",
    0x9107: "GetPartialObject",
    0x9108: "SetObjectTime",
    0x9109: "GetDeviceInfoEx",
    0x9110: "SetProperty",
    0x9116: "Capture",
    0x9127: "GetProperty",
    0x9128: "InitiateReleaseControl",
    0x9129: "TerminateReleaseControl",
    0x9130: "RemoteReleaseOn",
    0x9131: "RemoteReleaseOff",
    0x9153: "LiveViewStart",
    0x9154: "LiveViewStop",
    0x9155: "GetLiveView",
    0x9156: "LiveViewLock",
    0x9157: "LiveViewUnlock",
    0x9158: "DriveLens",
    0x9159: "SetAFPoint",
    0x915A: "GetAFInfo",
    0x915E: "MovieStart",
    0x915F: "MovieStop",
}

# PTP Response Codes
PTP_RESPONSE_CODES = {
    0x2001: "OK",
    0x2002: "General Error",
    0x2003: "Session Not Open",
    0x2004: "Invalid Transaction ID",
    0x2005: "Operation Not Supported",
    0x2006: "Parameter Not Supported",
    0x2007: "Incomplete Transfer",
    0x2008: "Invalid Storage ID",
    0x2009: "Invalid Object Handle",
    0x200A: "Device Property Not Supported",
    0x200B: "Invalid Object Format Code",
    0x200C: "Storage Full",
    0x200D: "Object Write Protected",
    0x200E: "Store Read Only",
    0x200F: "Access Denied",
    0x2010: "No Thumbnail Present",
    0x2011: "Self Test Failed",
    0x2012: "Partial Deletion",
    0x2013: "Store Not Available",
    0x2014: "Specification By Format Unsupported",
    0x2015: "No Valid Object Info",
    0x2016: "Invalid Code Format",
    0x2017: "Unknown Vendor Code",
    0x2018: "Capture Already Active",
    0x2019: "Device Busy",
    0x201A: "Invalid Parent Object",
    0x201B: "Invalid Device Property Format",
    0x201C: "Invalid Device Property Value",
    0x201D: "Invalid Parameter",
    0x201E: "Session Already Open",
    0x201F: "Transaction Cancelled",
    0x2020: "Specification Of Destination Unsupported",
}

# Canon Specific Response Codes
CANON_PTP_RESPONSE_CODES = {
    0xA001: "Unknown Command",
    0xA005: "Operation Refused",
    0xA006: "Lens Cover Close",
    0xA101: "Low Battery",
    0xA102: "Object Not Ready", 
    0xA104: "Cannot Make Object",
    0xA105: "Memory Status Not Ready",
    0xA106: "Directory Creation Failed",
    0xA107: "Cancel All Transfers",
    0xA108: "Device Busy",
}

# PTP Event Codes
PTP_EVENT_CODES = {
    0x4001: "CancelTransaction",
    0x4002: "ObjectAdded",
    0x4003: "ObjectRemoved", 
    0x4004: "StoreAdded",
    0x4005: "StoreRemoved",
    0x4006: "DevicePropertyChanged",
    0x4007: "ObjectInfoChanged",
    0x4008: "DeviceInfoChanged",
    0x4009: "RequestObjectTransfer",
    0x400A: "StoreFull",
    0x400B: "DeviceReset",
    0x400C: "StorageInfoChanged",
    0x400D: "CaptureComplete",
    0x400E: "UnreportedStatus",
}

# Canon Event Codes  
CANON_PTP_EVENT_CODES = {
    0xC181: "ObjectCreated",
    0xC182: "ObjectRemoved",
    0xC183: "RequestObjectTransfer",
    0xC184: "Shutdown",
    0xC185: "DeviceInfoChanged",
    0xC186: "CaptureCompleteImmediately", 
    0xC187: "CameraStatusChanged",
    0xC188: "WillShutdown",
    0xC189: "ShutterButtonDown",
    0xC18A: "ShutterButtonUp",
    0xC18B: "BulbExposureTime",
}

class PTPContainer:
    """PTP Container parser and analyzer"""
    
    def __init__(self, data: bytes):
        if len(data) < 12:
            raise ValueError("PTP container too short")
            
        self.length, self.type, self.code, self.trans_id = struct.unpack('<LHHL', data[:12])
        
        # Parse parameters (up to 5)
        self.params = []
        param_data = data[12:min(len(data), 12 + 20)]  # Max 5 parameters * 4 bytes
        for i in range(0, len(param_data), 4):
            if i + 4 <= len(param_data):
                param = struct.unpack('<L', param_data[i:i+4])[0]
                self.params.append(param)
        
        # Data payload (if any)
        self.data = data[12 + len(self.params) * 4:] if len(data) > 12 + len(self.params) * 4 else b''
    
    def get_type_name(self) -> str:
        return PTP_CONTAINER_TYPES.get(self.type, f"Unknown(0x{self.type:04X})")
    
    def get_operation_name(self) -> str:
        # Check Canon extensions first
        if self.code in CANON_PTP_OPERATIONS:
            return f"Canon::{CANON_PTP_OPERATIONS[self.code]}"
        elif self.code in PTP_OPERATIONS:
            return f"PTP::{PTP_OPERATIONS[self.code]}"
        else:
            return f"Unknown(0x{self.code:04X})"
    
    def get_response_name(self) -> str:
        # Check Canon response codes first
        if self.code in CANON_PTP_RESPONSE_CODES:
            return f"Canon::{CANON_PTP_RESPONSE_CODES[self.code]}"
        elif self.code in PTP_RESPONSE_CODES:
            return f"PTP::{PTP_RESPONSE_CODES[self.code]}"
        else:
            return f"Unknown(0x{self.code:04X})"
    
    def get_event_name(self) -> str:
        # Check Canon event codes first
        if self.code in CANON_PTP_EVENT_CODES:
            return f"Canon::{CANON_PTP_EVENT_CODES[self.code]}"
        elif self.code in PTP_EVENT_CODES:
            return f"PTP::{PTP_EVENT_CODES[self.code]}"
        else:
            return f"Unknown(0x{self.code:04X})"
    
    def __str__(self) -> str:
        result = []
        result.append(f"PTP Container:")
        result.append(f"  Length: {self.length}")
        result.append(f"  Type: {self.get_type_name()} (0x{self.type:04X})")
        result.append(f"  Transaction ID: {self.trans_id}")
        
        if self.type == 0x0001:  # Command
            result.append(f"  Operation: {self.get_operation_name()} (0x{self.code:04X})")
        elif self.type == 0x0003:  # Response
            result.append(f"  Response: {self.get_response_name()} (0x{self.code:04X})")
        elif self.type == 0x0004:  # Event
            result.append(f"  Event: {self.get_event_name()} (0x{self.code:04X})")
        else:
            result.append(f"  Code: 0x{self.code:04X}")
        
        if self.params:
            result.append(f"  Parameters: {[f'0x{p:08X}' for p in self.params]}")
        
        if self.data:
            result.append(f"  Data Length: {len(self.data)}")
            result.append(f"  Data (hex): {self.data[:32].hex()}")
            if len(self.data) > 32:
                result.append(f"    ... ({len(self.data) - 32} more bytes)")
        
        return '\n'.join(result)

def analyze_ptp_data(data: bytes) -> List[PTPContainer]:
    """Analyze PTP data and extract containers"""
    containers = []
    offset = 0
    
    while offset < len(data):
        try:
            container = PTPContainer(data[offset:])
            containers.append(container)
            
            # Move to next container
            if container.length < 12:
                break
            offset += container.length
            
            # Align to 4-byte boundary
            offset = (offset + 3) & ~3
            
        except (ValueError, struct.error) as e:
            print(f"Error parsing container at offset {offset}: {e}")
            break
    
    return containers

def generate_driver_code(containers: List[PTPContainer]) -> str:
    """Generate C code for PTP operations found in trace"""
    code_lines = []
    code_lines.append("/* Generated PTP operation codes from trace analysis */")
    code_lines.append("")
    
    # Extract unique operation codes
    operations = set()
    for container in containers:
        if container.type == 0x0001:  # Command
            operations.add(container.code)
    
    # Generate #defines
    for op_code in sorted(operations):
        if op_code in CANON_PTP_OPERATIONS:
            name = CANON_PTP_OPERATIONS[op_code].upper().replace(" ", "_")
            code_lines.append(f"#define CANON_PTP_OP_{name}\t\t0x{op_code:04X}")
        elif op_code in PTP_OPERATIONS:
            name = PTP_OPERATIONS[op_code].upper().replace(" ", "_")
            code_lines.append(f"#define PTP_OP_{name}\t\t0x{op_code:04X}")
    
    code_lines.append("")
    return '\n'.join(code_lines)

def main():
    if len(sys.argv) < 2:
        print("Canon R5 PTP Protocol Analyzer")
        print("Usage:")
        print(f"  {sys.argv[0]} <hex_data>          # Analyze hex string")
        print(f"  {sys.argv[0]} -f <file>          # Analyze binary file")
        print(f"  {sys.argv[0]} --generate-code    # Generate C code from trace")
        print("")
        print("Examples:")
        print(f"  {sys.argv[0]} '0C00000001001002010000000100'")
        print(f"  {sys.argv[0]} -f usb_trace.bin")
        return
    
    data = b''
    
    if sys.argv[1] == '-f':
        # Read from file
        if len(sys.argv) < 3:
            print("Error: filename required with -f option")
            return
        
        try:
            with open(sys.argv[2], 'rb') as f:
                data = f.read()
        except IOError as e:
            print(f"Error reading file: {e}")
            return
            
    elif sys.argv[1] == '--generate-code':
        print("Code generation requires trace data. Use with -f option:")
        print(f"  {sys.argv[0]} --generate-code -f trace.bin")
        return
        
    else:
        # Parse hex string
        hex_string = sys.argv[1].replace(' ', '').replace('0x', '')
        try:
            data = bytes.fromhex(hex_string)
        except ValueError as e:
            print(f"Error parsing hex data: {e}")
            return
    
    if not data:
        print("No data to analyze")
        return
    
    print(f"Analyzing {len(data)} bytes of PTP data...")
    print("=" * 50)
    
    # Analyze containers
    containers = analyze_ptp_data(data)
    
    if not containers:
        print("No valid PTP containers found")
        return
    
    # Print analysis results
    for i, container in enumerate(containers):
        print(f"\n--- Container {i + 1} ---")
        print(container)
    
    print(f"\n{'=' * 50}")
    print(f"Summary: Found {len(containers)} PTP container(s)")
    
    # Generate statistics
    types = {}
    operations = {}
    
    for container in containers:
        type_name = container.get_type_name()
        types[type_name] = types.get(type_name, 0) + 1
        
        if container.type == 0x0001:  # Command
            op_name = container.get_operation_name()
            operations[op_name] = operations.get(op_name, 0) + 1
    
    if types:
        print(f"\nContainer Types:")
        for type_name, count in types.items():
            print(f"  {type_name}: {count}")
    
    if operations:
        print(f"\nOperations:")
        for op_name, count in operations.items():
            print(f"  {op_name}: {count}")
    
    # Generate code if requested
    if '--generate-code' in sys.argv:
        print(f"\n{'=' * 50}")
        print("Generated C Code:")
        print("=" * 50)
        print(generate_driver_code(containers))

if __name__ == '__main__':
    main()