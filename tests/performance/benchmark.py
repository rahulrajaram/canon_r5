#!/usr/bin/env python3
"""
Canon R5 Driver Performance Benchmarks

Copyright (C) 2025 Canon R5 Driver Project
SPDX-License-Identifier: GPL-2.0
"""

import os
import sys
import time
import json
import argparse
import subprocess
import statistics
from pathlib import Path
from typing import Dict, List, Optional, Any
from dataclasses import dataclass, asdict

@dataclass
class BenchmarkResult:
    """Container for benchmark results"""
    name: str
    duration: float
    throughput: Optional[float] = None
    latency: Optional[float] = None
    memory_usage: Optional[int] = None
    success: bool = True
    error_message: Optional[str] = None
    metadata: Dict[str, Any] = None

class Canon5R5Benchmark:
    """Canon R5 driver performance benchmark suite"""
    
    def __init__(self, root_dir: Path):
        self.root_dir = root_dir
        self.results: List[BenchmarkResult] = []
        self.verbose = False
        
    def log(self, message: str, level: str = "INFO"):
        """Log a message with timestamp"""
        timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
        if self.verbose or level == "ERROR":
            print(f"[{timestamp}] [{level}] {message}")
    
    def run_command(self, cmd: List[str], timeout: int = 30) -> subprocess.CompletedProcess:
        """Run a shell command with timeout"""
        try:
            result = subprocess.run(
                cmd, 
                capture_output=True, 
                text=True, 
                timeout=timeout,
                cwd=self.root_dir
            )
            return result
        except subprocess.TimeoutExpired:
            self.log(f"Command timed out: {' '.join(cmd)}", "ERROR")
            raise
        except Exception as e:
            self.log(f"Command failed: {' '.join(cmd)} - {e}", "ERROR")
            raise
    
    def benchmark_module_loading(self) -> BenchmarkResult:
        """Benchmark module loading/unloading performance"""
        self.log("Benchmarking module loading performance...")
        
        start_time = time.time()
        
        try:
            # Ensure modules are built
            build_result = self.run_command(["make", "modules"], timeout=120)
            if build_result.returncode != 0:
                return BenchmarkResult(
                    name="module_loading",
                    duration=time.time() - start_time,
                    success=False,
                    error_message="Module build failed"
                )
            
            # Measure loading times for each module
            modules = ["canon-r5-core", "canon-r5-usb", "canon-r5-video", 
                      "canon-r5-still", "canon-r5-audio", "canon-r5-storage"]
            
            load_times = []
            
            # Check if running as root
            if os.geteuid() != 0:
                self.log("Skipping module loading benchmark (requires root)", "WARN")
                return BenchmarkResult(
                    name="module_loading",
                    duration=time.time() - start_time,
                    success=True,
                    metadata={"skipped": "requires_root"}
                )
            
            for module in modules:
                module_file = f"{module}.ko"
                if not os.path.exists(module_file):
                    continue
                
                # Measure load time
                load_start = time.time()
                load_result = self.run_command(["insmod", module_file])
                load_duration = time.time() - load_start
                
                if load_result.returncode == 0:
                    load_times.append(load_duration)
                    self.log(f"Loaded {module} in {load_duration:.3f}s")
                    
                    # Unload immediately
                    self.run_command(["rmmod", module])
                else:
                    self.log(f"Failed to load {module}", "WARN")
            
            duration = time.time() - start_time
            avg_load_time = statistics.mean(load_times) if load_times else 0
            
            return BenchmarkResult(
                name="module_loading",
                duration=duration,
                latency=avg_load_time,
                metadata={
                    "modules_tested": len(load_times),
                    "load_times": load_times,
                    "avg_load_time": avg_load_time
                }
            )
            
        except Exception as e:
            return BenchmarkResult(
                name="module_loading",
                duration=time.time() - start_time,
                success=False,
                error_message=str(e)
            )
    
    def benchmark_compilation_speed(self) -> BenchmarkResult:
        """Benchmark compilation performance"""
        self.log("Benchmarking compilation speed...")
        
        start_time = time.time()
        
        try:
            # Clean build
            self.run_command(["make", "clean"])
            
            # Measure compilation time
            compile_start = time.time()
            result = self.run_command(["make", "modules", "-j", str(os.cpu_count() or 4)], timeout=300)
            compile_duration = time.time() - compile_start
            
            if result.returncode != 0:
                return BenchmarkResult(
                    name="compilation_speed",
                    duration=time.time() - start_time,
                    success=False,
                    error_message="Compilation failed"
                )
            
            # Count lines of code compiled
            loc_result = self.run_command(["find", "drivers", "include", "-name", "*.c", "-o", "-name", "*.h"])
            files = loc_result.stdout.strip().split('\n') if loc_result.stdout.strip() else []
            
            total_lines = 0
            for file_path in files:
                if os.path.exists(file_path):
                    with open(file_path, 'r') as f:
                        total_lines += sum(1 for _ in f)
            
            lines_per_second = total_lines / compile_duration if compile_duration > 0 else 0
            
            return BenchmarkResult(
                name="compilation_speed",
                duration=time.time() - start_time,
                throughput=lines_per_second,
                metadata={
                    "compile_duration": compile_duration,
                    "total_lines": total_lines,
                    "files_compiled": len(files),
                    "lines_per_second": lines_per_second
                }
            )
            
        except Exception as e:
            return BenchmarkResult(
                name="compilation_speed",
                duration=time.time() - start_time,
                success=False,
                error_message=str(e)
            )
    
    def benchmark_memory_footprint(self) -> BenchmarkResult:
        """Benchmark memory footprint of compiled modules"""
        self.log("Benchmarking memory footprint...")
        
        start_time = time.time()
        
        try:
            modules = ["canon-r5-core.ko", "canon-r5-usb.ko", "canon-r5-video.ko",
                      "canon-r5-still.ko", "canon-r5-audio.ko", "canon-r5-storage.ko"]
            
            module_sizes = {}
            total_size = 0
            
            for module in modules:
                if os.path.exists(module):
                    size = os.path.getsize(module)
                    module_sizes[module] = size
                    total_size += size
                    self.log(f"{module}: {size:,} bytes")
            
            return BenchmarkResult(
                name="memory_footprint",
                duration=time.time() - start_time,
                memory_usage=total_size,
                metadata={
                    "module_sizes": module_sizes,
                    "total_modules": len(module_sizes),
                    "average_size": total_size // len(module_sizes) if module_sizes else 0
                }
            )
            
        except Exception as e:
            return BenchmarkResult(
                name="memory_footprint",
                duration=time.time() - start_time,
                success=False,
                error_message=str(e)
            )
    
    def benchmark_build_system(self) -> BenchmarkResult:
        """Benchmark build system performance"""
        self.log("Benchmarking build system...")
        
        start_time = time.time()
        
        try:
            # Test clean build
            self.run_command(["make", "clean"])
            clean_build_start = time.time()
            result = self.run_command(["make", "modules"], timeout=300)
            clean_build_time = time.time() - clean_build_start
            
            if result.returncode != 0:
                return BenchmarkResult(
                    name="build_system",
                    duration=time.time() - start_time,
                    success=False,
                    error_message="Clean build failed"
                )
            
            # Test incremental build (no changes)
            incremental_start = time.time()
            result = self.run_command(["make", "modules"])
            incremental_time = time.time() - incremental_start
            
            # Test with single file change
            test_file = "drivers/core/canon-r5-core.c"
            if os.path.exists(test_file):
                # Touch the file to simulate a change
                Path(test_file).touch()
                
                touch_build_start = time.time()
                result = self.run_command(["make", "modules"])
                touch_build_time = time.time() - touch_build_start
            else:
                touch_build_time = None
            
            return BenchmarkResult(
                name="build_system",
                duration=time.time() - start_time,
                metadata={
                    "clean_build_time": clean_build_time,
                    "incremental_build_time": incremental_time,
                    "touch_build_time": touch_build_time,
                    "speedup_ratio": clean_build_time / incremental_time if incremental_time > 0 else 0
                }
            )
            
        except Exception as e:
            return BenchmarkResult(
                name="build_system",
                duration=time.time() - start_time,
                success=False,
                error_message=str(e)
            )
    
    def benchmark_test_execution(self) -> BenchmarkResult:
        """Benchmark test execution performance"""
        self.log("Benchmarking test execution...")
        
        start_time = time.time()
        
        try:
            # Run integration tests
            test_script = "tests/integration/test_driver_loading.sh"
            if os.path.exists(test_script) and os.access(test_script, os.X_OK):
                test_start = time.time()
                result = self.run_command([test_script], timeout=120)
                test_duration = time.time() - test_start
                
                return BenchmarkResult(
                    name="test_execution",
                    duration=time.time() - start_time,
                    latency=test_duration,
                    success=result.returncode == 0,
                    metadata={
                        "test_duration": test_duration,
                        "test_exit_code": result.returncode
                    }
                )
            else:
                return BenchmarkResult(
                    name="test_execution",
                    duration=time.time() - start_time,
                    success=True,
                    metadata={"skipped": "test_script_not_executable"}
                )
                
        except Exception as e:
            return BenchmarkResult(
                name="test_execution",
                duration=time.time() - start_time,
                success=False,
                error_message=str(e)
            )
    
    def run_all_benchmarks(self) -> List[BenchmarkResult]:
        """Run all benchmarks"""
        self.log("Starting Canon R5 driver performance benchmarks...")
        
        benchmarks = [
            self.benchmark_compilation_speed,
            self.benchmark_memory_footprint,
            self.benchmark_build_system,
            self.benchmark_module_loading,
            self.benchmark_test_execution,
        ]
        
        for benchmark_func in benchmarks:
            try:
                result = benchmark_func()
                self.results.append(result)
                
                if result.success:
                    self.log(f"✓ {result.name} completed in {result.duration:.2f}s")
                else:
                    self.log(f"✗ {result.name} failed: {result.error_message}", "ERROR")
                    
            except Exception as e:
                error_result = BenchmarkResult(
                    name=benchmark_func.__name__.replace("benchmark_", ""),
                    duration=0,
                    success=False,
                    error_message=str(e)
                )
                self.results.append(error_result)
                self.log(f"✗ {error_result.name} crashed: {e}", "ERROR")
        
        return self.results
    
    def generate_report(self, output_file: Optional[str] = None) -> Dict[str, Any]:
        """Generate benchmark report"""
        report = {
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "system_info": {
                "cpu_count": os.cpu_count(),
                "platform": sys.platform,
                "python_version": sys.version,
            },
            "summary": {
                "total_benchmarks": len(self.results),
                "successful": sum(1 for r in self.results if r.success),
                "failed": sum(1 for r in self.results if not r.success),
            },
            "results": [asdict(result) for result in self.results]
        }
        
        if output_file:
            with open(output_file, 'w') as f:
                json.dump(report, f, indent=2)
            self.log(f"Report saved to {output_file}")
        
        return report
    
    def print_summary(self):
        """Print benchmark summary"""
        print("\n" + "="*60)
        print("CANON R5 DRIVER PERFORMANCE BENCHMARK RESULTS")
        print("="*60)
        
        successful = sum(1 for r in self.results if r.success)
        total = len(self.results)
        
        print(f"Total benchmarks: {total}")
        print(f"Successful: {successful}")
        print(f"Failed: {total - successful}")
        print()
        
        for result in self.results:
            status = "✓" if result.success else "✗"
            print(f"{status} {result.name:<20} {result.duration:>8.2f}s")
            
            if result.throughput:
                print(f"  Throughput: {result.throughput:,.0f} items/sec")
            if result.latency:
                print(f"  Latency: {result.latency:.3f}s")
            if result.memory_usage:
                print(f"  Memory: {result.memory_usage:,} bytes")
            if result.error_message:
                print(f"  Error: {result.error_message}")
        
        print("\n" + "="*60)

def main():
    parser = argparse.ArgumentParser(description="Canon R5 Driver Performance Benchmarks")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--output", "-o", help="Output file for JSON report")
    parser.add_argument("--output-format", choices=['json', 'text'], default='text', help="Output format")
    parser.add_argument("--output-file", help="Output file path")
    parser.add_argument("--ci-mode", action="store_true", help="CI mode (reduced privileges)")
    parser.add_argument("--full-suite", action="store_true", help="Run full benchmark suite")
    parser.add_argument("--root-dir", help="Root directory of Canon R5 driver", 
                       default=os.path.dirname(os.path.dirname(os.path.dirname(__file__))))
    
    args = parser.parse_args()
    
    root_dir = Path(args.root_dir).resolve()
    
    if not root_dir.exists():
        print(f"Error: Root directory does not exist: {root_dir}")
        sys.exit(1)
    
    benchmark = Canon5R5Benchmark(root_dir)
    benchmark.verbose = args.verbose
    
    # Run benchmarks
    benchmark.run_all_benchmarks()
    
    # Generate report
    output_file = args.output or args.output_file
    report = benchmark.generate_report(output_file)
    
    # Print summary
    benchmark.print_summary()
    
    # Exit with error code if any benchmarks failed
    failed_count = report["summary"]["failed"]
    if failed_count > 0:
        print(f"\n{failed_count} benchmark(s) failed.")
        sys.exit(1)
    else:
        print("\nAll benchmarks completed successfully! 🎉")

if __name__ == "__main__":
    main()