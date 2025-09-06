#!/usr/bin/env python3
"""
Canon R5 Driver Suite - Memory Usage Tests
Tests memory allocation patterns and detects potential leaks
"""

import os
import sys
import time
import subprocess
import json
import argparse
from pathlib import Path
from typing import Dict, List, Optional, Tuple

class MemoryTester:
    """Memory testing utilities for Canon R5 driver modules"""
    
    def __init__(self, build_dir: str = "build/modules"):
        self.build_dir = Path(build_dir)
        self.modules = [
            "canon-r5-core.ko",
            "canon-r5-usb.ko", 
            "canon-r5-video.ko",
            "canon-r5-still.ko",
            "canon-r5-audio.ko",
            "canon-r5-storage.ko"
        ]
        self.loaded_modules = []
        self.memory_baseline = None
        
    def get_system_memory(self) -> Dict[str, int]:
        """Get current system memory statistics"""
        try:
            with open('/proc/meminfo', 'r') as f:
                meminfo = {}
                for line in f:
                    key, value = line.split(':')
                    # Convert to KB and remove 'kB' suffix
                    meminfo[key.strip()] = int(value.strip().split()[0])
                return meminfo
        except Exception as e:
            print(f"Warning: Could not read memory info: {e}")
            return {}
    
    def get_module_memory(self, module_name: str) -> Optional[Dict[str, int]]:
        """Get memory usage for a specific module"""
        try:
            # Check if module is loaded
            result = subprocess.run(['lsmod'], capture_output=True, text=True, check=True)
            if module_name.replace('-', '_') not in result.stdout:
                return None
            
            # Get module memory info from /proc/modules
            with open('/proc/modules', 'r') as f:
                for line in f:
                    parts = line.strip().split()
                    if len(parts) >= 2 and parts[0] == module_name.replace('-', '_'):
                        return {
                            'size': int(parts[1]),
                            'instances': int(parts[2]),
                            'dependencies': parts[3].split(',') if parts[3] != '-' else [],
                            'state': parts[4] if len(parts) > 4 else 'unknown'
                        }
        except Exception as e:
            print(f"Warning: Could not get module memory info for {module_name}: {e}")
        
        return None
    
    def get_slab_info(self) -> Dict[str, Dict[str, int]]:
        """Get SLAB allocator information for kernel objects"""
        try:
            slab_info = {}
            with open('/proc/slabinfo', 'r') as f:
                # Skip header lines
                next(f)  # version line
                next(f)  # column headers
                
                for line in f:
                    parts = line.strip().split()
                    if len(parts) >= 6:
                        name = parts[0]
                        # Look for Canon R5 related slabs
                        if 'canon' in name.lower() or 'r5' in name.lower():
                            slab_info[name] = {
                                'active_objects': int(parts[1]),
                                'num_objects': int(parts[2]),
                                'object_size': int(parts[3]),
                                'objects_per_slab': int(parts[4]),
                                'pages_per_slab': int(parts[5])
                            }
            return slab_info
        except Exception as e:
            print(f"Warning: Could not read slab info: {e}")
            return {}
    
    def check_modules_exist(self) -> bool:
        """Check if all required modules exist"""
        missing = []
        for module in self.modules:
            if not (self.build_dir / module).exists():
                missing.append(module)
        
        if missing:
            print(f"Missing modules: {', '.join(missing)}")
            print("Run 'make modules' to build them first.")
            return False
        
        return True
    
    def load_module(self, module_name: str) -> bool:
        """Load a kernel module and check for memory changes"""
        module_path = self.build_dir / module_name
        
        try:
            print(f"  Loading {module_name}...")
            
            # Get memory before loading
            mem_before = self.get_system_memory()
            
            # Load the module
            result = subprocess.run(
                ['sudo', 'insmod', str(module_path)],
                capture_output=True, text=True
            )
            
            if result.returncode != 0:
                print(f"    ❌ Failed to load {module_name}: {result.stderr}")
                return False
            
            # Wait a moment for module initialization
            time.sleep(0.5)
            
            # Get memory after loading
            mem_after = self.get_system_memory()
            
            # Calculate memory difference
            if mem_before and mem_after:
                mem_diff = mem_before.get('MemFree', 0) - mem_after.get('MemFree', 0)
                if mem_diff > 0:
                    print(f"    📊 Memory used: {mem_diff} KB")
                else:
                    print(f"    📊 Memory freed: {abs(mem_diff)} KB")
            
            # Get module-specific memory info
            mod_info = self.get_module_memory(module_name.replace('.ko', ''))
            if mod_info:
                print(f"    📦 Module size: {mod_info['size']} bytes")
                print(f"    🔢 Instances: {mod_info['instances']}")
            
            self.loaded_modules.append(module_name.replace('.ko', ''))
            print(f"    ✅ Successfully loaded {module_name}")
            return True
            
        except subprocess.CalledProcessError as e:
            print(f"    ❌ Failed to load {module_name}: {e}")
            return False
    
    def unload_module(self, module_name: str) -> bool:
        """Unload a kernel module and check for memory leaks"""
        try:
            print(f"  Unloading {module_name}...")
            
            # Get memory before unloading
            mem_before = self.get_system_memory()
            
            # Unload the module
            result = subprocess.run(
                ['sudo', 'rmmod', module_name.replace('.ko', '')],
                capture_output=True, text=True
            )
            
            if result.returncode != 0:
                print(f"    ❌ Failed to unload {module_name}: {result.stderr}")
                return False
            
            # Wait a moment for cleanup
            time.sleep(0.5)
            
            # Get memory after unloading
            mem_after = self.get_system_memory()
            
            # Calculate memory difference (should be positive if memory was freed)
            if mem_before and mem_after:
                mem_diff = mem_after.get('MemFree', 0) - mem_before.get('MemFree', 0)
                if mem_diff > 0:
                    print(f"    📊 Memory freed: {mem_diff} KB")
                elif mem_diff < 0:
                    print(f"    ⚠️  Memory not freed: {abs(mem_diff)} KB (potential leak)")
                else:
                    print(f"    📊 No significant memory change")
            
            if module_name.replace('.ko', '') in self.loaded_modules:
                self.loaded_modules.remove(module_name.replace('.ko', ''))
            
            print(f"    ✅ Successfully unloaded {module_name}")
            return True
            
        except subprocess.CalledProcessError as e:
            print(f"    ❌ Failed to unload {module_name}: {e}")
            return False
    
    def test_module_loading_memory(self) -> Dict[str, bool]:
        """Test memory usage during module loading/unloading"""
        print("\n🧪 Testing Module Loading Memory Usage")
        print("=" * 50)
        
        results = {}
        
        # Test each module individually
        for module in self.modules:
            print(f"\n📦 Testing {module}:")
            
            # Load module
            load_success = self.load_module(module)
            if not load_success:
                results[module] = False
                continue
            
            # Let module run for a moment
            time.sleep(1)
            
            # Unload module
            unload_success = self.unload_module(module)
            results[module] = load_success and unload_success
            
            # Brief pause between tests
            time.sleep(0.5)
        
        return results
    
    def test_dependency_loading(self) -> bool:
        """Test loading modules in dependency order"""
        print("\n🔗 Testing Dependency-Order Loading")
        print("=" * 50)
        
        # Expected loading order
        load_order = [
            "canon-r5-core.ko",
            "canon-r5-usb.ko", 
            "canon-r5-video.ko",
            "canon-r5-still.ko",
            "canon-r5-audio.ko",
            "canon-r5-storage.ko"
        ]
        
        # Record initial memory
        initial_memory = self.get_system_memory()
        print(f"Initial free memory: {initial_memory.get('MemFree', 0)} KB")
        
        # Load modules in order
        loaded_successfully = []
        for module in load_order:
            if self.load_module(module):
                loaded_successfully.append(module)
            else:
                break  # Stop if any module fails
        
        # Check total memory usage
        current_memory = self.get_system_memory()
        if initial_memory and current_memory:
            total_used = initial_memory.get('MemFree', 0) - current_memory.get('MemFree', 0)
            print(f"\n📊 Total memory used by all modules: {total_used} KB")
        
        # Unload in reverse order
        print(f"\n🔄 Unloading {len(loaded_successfully)} modules in reverse order:")
        for module in reversed(loaded_successfully):
            self.unload_module(module)
        
        # Check for memory leaks
        final_memory = self.get_system_memory()
        if initial_memory and final_memory:
            leak_amount = initial_memory.get('MemFree', 0) - final_memory.get('MemFree', 0)
            if leak_amount > 100:  # Allow some tolerance (100KB)
                print(f"⚠️  Potential memory leak detected: {leak_amount} KB")
                return False
            else:
                print(f"✅ No significant memory leaks detected (difference: {leak_amount} KB)")
        
        return len(loaded_successfully) == len(load_order)
    
    def test_repeated_loading(self, cycles: int = 5) -> bool:
        """Test repeated loading/unloading for memory leaks"""
        print(f"\n🔄 Testing Repeated Loading ({cycles} cycles)")
        print("=" * 50)
        
        # Use core module for repeated testing (most critical)
        test_module = "canon-r5-core.ko"
        
        initial_memory = self.get_system_memory()
        memory_samples = [initial_memory.get('MemFree', 0)]
        
        for cycle in range(cycles):
            print(f"\n📋 Cycle {cycle + 1}/{cycles}:")
            
            # Load module
            if not self.load_module(test_module):
                return False
            
            # Brief operation simulation
            time.sleep(0.5)
            
            # Sample memory
            current_memory = self.get_system_memory()
            memory_samples.append(current_memory.get('MemFree', 0))
            
            # Unload module
            if not self.unload_module(test_module):
                return False
            
            # Sample memory after unload
            final_memory = self.get_system_memory()
            memory_samples.append(final_memory.get('MemFree', 0))
        
        # Analyze memory trend
        print(f"\n📊 Memory Usage Analysis:")
        print(f"Initial free memory: {memory_samples[0]} KB")
        print(f"Final free memory: {memory_samples[-1]} KB")
        
        # Calculate trend
        leak_per_cycle = (memory_samples[0] - memory_samples[-1]) / cycles
        if leak_per_cycle > 50:  # More than 50KB per cycle
            print(f"⚠️  Memory leak detected: ~{leak_per_cycle:.1f} KB per cycle")
            return False
        else:
            print(f"✅ No significant memory leak: ~{leak_per_cycle:.1f} KB per cycle")
        
        return True
    
    def cleanup_loaded_modules(self):
        """Clean up any modules that were loaded during testing"""
        print("\n🧹 Cleaning up loaded modules...")
        
        for module in reversed(self.loaded_modules):
            try:
                subprocess.run(['sudo', 'rmmod', module], 
                             capture_output=True, check=True)
                print(f"  ✅ Unloaded {module}")
            except subprocess.CalledProcessError:
                print(f"  ⚠️  Could not unload {module} (may not be loaded)")
        
        self.loaded_modules.clear()
    
    def run_all_tests(self) -> Dict[str, bool]:
        """Run all memory tests"""
        print("Canon R5 Driver Suite - Memory Tests")
        print("=" * 50)
        print(f"Testing modules in: {self.build_dir}")
        print(f"Kernel version: {os.uname().release}")
        
        if not self.check_modules_exist():
            return {"error": True}
        
        results = {}
        
        try:
            # Test individual module loading
            results["individual_loading"] = self.test_module_loading_memory()
            
            # Test dependency loading
            results["dependency_loading"] = self.test_dependency_loading()
            
            # Test repeated loading
            results["repeated_loading"] = self.test_repeated_loading()
            
        except KeyboardInterrupt:
            print("\n\n⚠️  Tests interrupted by user")
            results["interrupted"] = True
        
        finally:
            # Always try to clean up
            self.cleanup_loaded_modules()
        
        return results

def main():
    parser = argparse.ArgumentParser(
        description="Canon R5 Driver Memory Testing Suite"
    )
    parser.add_argument(
        "--build-dir", 
        default="build/modules",
        help="Directory containing built kernel modules"
    )
    parser.add_argument(
        "--cycles",
        type=int,
        default=5,
        help="Number of cycles for repeated loading test"
    )
    parser.add_argument(
        "--output",
        help="Output file for test results (JSON format)"
    )
    
    args = parser.parse_args()
    
    # Check if running as root (required for module loading)
    if os.geteuid() != 0:
        print("❌ This test requires root privileges to load/unload kernel modules")
        print("Please run with: sudo python3 memory_test.py")
        sys.exit(1)
    
    # Run tests
    tester = MemoryTester(args.build_dir)
    results = tester.run_all_tests()
    
    # Print summary
    print("\n" + "=" * 50)
    print("MEMORY TEST SUMMARY")
    print("=" * 50)
    
    if "error" in results:
        print("❌ Tests could not be completed due to missing modules")
        sys.exit(1)
    
    if "interrupted" in results:
        print("⚠️  Tests were interrupted")
        sys.exit(1)
    
    # Count successes
    total_tests = 0
    passed_tests = 0
    
    for test_name, result in results.items():
        if isinstance(result, dict):
            # Individual module results
            for module, success in result.items():
                total_tests += 1
                if success:
                    passed_tests += 1
        elif isinstance(result, bool):
            total_tests += 1
            if result:
                passed_tests += 1
    
    print(f"Tests passed: {passed_tests}/{total_tests}")
    
    if passed_tests == total_tests:
        print("✅ All memory tests passed!")
        exit_code = 0
    else:
        print("❌ Some memory tests failed!")
        exit_code = 1
    
    # Save results if requested
    if args.output:
        with open(args.output, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"📄 Results saved to {args.output}")
    
    sys.exit(exit_code)

if __name__ == "__main__":
    main()