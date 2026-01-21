#!/usr/bin/env python3
"""
Enhanced Load Test Script for FS-Unified-STT

Features:
- Scale and Longevity testing
- System metrics collection (CPU, Memory, Disk I/O, Network)
- Mock Pusher log analysis
- Server log analysis
- Performance summary with validation

Usage:
    python load_test.py --connections 100 --duration 60
    python load_test.py --mode longevity --connections 50 --duration 3600
"""

import asyncio
import json
import wave
import argparse
import uuid
import sys
import time
import os
import math
import statistics
import subprocess
import psutil
from datetime import datetime
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Dict, Optional
from collections import defaultdict

try:
    import websockets
except ImportError:
    print("ERROR: websockets not installed. Run: pip install websockets")
    sys.exit(1)

# Configuration
DEFAULT_URL = "ws://127.0.0.1:9088/audio-fork"
DEFAULT_WAV = "response_call_60s.wav"
CHUNK_DURATION_MS = 20
METRICS_INTERVAL = 5  # Collect system metrics every 5 seconds


@dataclass
class SystemMetrics:
    """System metrics snapshot"""
    timestamp: float
    cpu_percent: float
    cpu_count: int
    cpu_freq_mhz: float
    memory_used_mb: float
    memory_percent: float
    disk_read_mb: float
    disk_write_mb: float
    net_sent_mb: float
    net_recv_mb: float
    open_files: int
    threads: int


@dataclass
class ConnectionStats:
    """Stats for a single connection"""
    connection_id: int
    connected: bool = False
    connect_time_ms: float = 0
    messages_received: int = 0
    finals_received: int = 0
    errors: List[str] = field(default_factory=list)
    start_time: float = 0
    end_time: float = 0
    bytes_sent: int = 0


@dataclass 
class LoadTestResults:
    """Aggregate test results"""
    test_mode: str = ""
    target_connections: int = 0
    target_duration: int = 0
    
    # Connection stats
    total_connections: int = 0
    successful_connections: int = 0
    failed_connections: int = 0
    peak_connections: int = 0
    
    # Message stats
    total_messages: int = 0
    total_finals: int = 0
    total_bytes_sent: int = 0
    
    # Timing
    avg_connect_time_ms: float = 0
    min_connect_time_ms: float = 0
    max_connect_time_ms: float = 0
    actual_duration_seconds: float = 0
    
    # Throughput
    connections_per_second: float = 0
    messages_per_second: float = 0
    bytes_per_second: float = 0
    
    # System metrics averages
    avg_cpu_percent: float = 0
    max_cpu_percent: float = 0
    avg_memory_mb: float = 0
    max_memory_mb: float = 0
    total_disk_read_mb: float = 0
    total_disk_write_mb: float = 0
    total_net_sent_mb: float = 0
    total_net_recv_mb: float = 0
    
    # Validation
    pusher_events_logged: int = 0
    pusher_sessions_started: int = 0
    pusher_sessions_stopped: int = 0
    pusher_transcripts: int = 0
    validation_passed: bool = False


class SystemMetricsCollector:
    """Collects system metrics at regular intervals"""
    
    def __init__(self, process_name: str = "fs-unified-stt"):
        self.process_name = process_name
        self.metrics: List[SystemMetrics] = []
        self.running = False
        self._task = None
        self._process = None
        self._initial_disk_io = None
        self._initial_net_io = None
        
    async def start(self):
        """Start collecting metrics"""
        self.running = True
        self._find_process()
        self._initial_disk_io = psutil.disk_io_counters()
        self._initial_net_io = psutil.net_io_counters()
        self._task = asyncio.create_task(self._collect_loop())
        
    async def stop(self):
        """Stop collecting metrics"""
        self.running = False
        if self._task:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
    
    def _find_process(self):
        """Find the target process"""
        for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
            try:
                cmdline = ' '.join(proc.info['cmdline'] or [])
                if self.process_name in cmdline:
                    self._process = psutil.Process(proc.info['pid'])
                    return
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
    
    async def _collect_loop(self):
        """Collect metrics every METRICS_INTERVAL seconds"""
        while self.running:
            try:
                metrics = self._collect_snapshot()
                if metrics:
                    self.metrics.append(metrics)
            except Exception as e:
                pass
            await asyncio.sleep(METRICS_INTERVAL)
    
    def _collect_snapshot(self) -> Optional[SystemMetrics]:
        """Collect a single metrics snapshot"""
        try:
            # CPU
            cpu_percent = psutil.cpu_percent(interval=0.1)
            cpu_count = psutil.cpu_count()
            cpu_freq = psutil.cpu_freq()
            cpu_freq_mhz = cpu_freq.current if cpu_freq else 0
            
            # Memory
            mem = psutil.virtual_memory()
            memory_used_mb = mem.used / (1024 * 1024)
            memory_percent = mem.percent
            
            # Disk I/O (delta from start)
            disk_io = psutil.disk_io_counters()
            disk_read_mb = (disk_io.read_bytes - self._initial_disk_io.read_bytes) / (1024 * 1024)
            disk_write_mb = (disk_io.write_bytes - self._initial_disk_io.write_bytes) / (1024 * 1024)
            
            # Network I/O (delta from start)
            net_io = psutil.net_io_counters()
            net_sent_mb = (net_io.bytes_sent - self._initial_net_io.bytes_sent) / (1024 * 1024)
            net_recv_mb = (net_io.bytes_recv - self._initial_net_io.bytes_recv) / (1024 * 1024)
            
            # Process-specific stats
            open_files = 0
            threads = 0
            if self._process:
                try:
                    open_files = len(self._process.open_files())
                    threads = self._process.num_threads()
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    self._find_process()
            
            return SystemMetrics(
                timestamp=time.time(),
                cpu_percent=cpu_percent,
                cpu_count=cpu_count,
                cpu_freq_mhz=cpu_freq_mhz,
                memory_used_mb=memory_used_mb,
                memory_percent=memory_percent,
                disk_read_mb=disk_read_mb,
                disk_write_mb=disk_write_mb,
                net_sent_mb=net_sent_mb,
                net_recv_mb=net_recv_mb,
                open_files=open_files,
                threads=threads,
            )
        except Exception as e:
            return None
    
    def get_summary(self) -> Dict:
        """Get summary statistics"""
        if not self.metrics:
            return {}
        
        return {
            "samples": len(self.metrics),
            "cpu_avg": statistics.mean(m.cpu_percent for m in self.metrics),
            "cpu_max": max(m.cpu_percent for m in self.metrics),
            "cpu_count": self.metrics[0].cpu_count,
            "cpu_freq_mhz": self.metrics[0].cpu_freq_mhz,
            "memory_avg_mb": statistics.mean(m.memory_used_mb for m in self.metrics),
            "memory_max_mb": max(m.memory_used_mb for m in self.metrics),
            "disk_read_mb": self.metrics[-1].disk_read_mb,
            "disk_write_mb": self.metrics[-1].disk_write_mb,
            "net_sent_mb": self.metrics[-1].net_sent_mb,
            "net_recv_mb": self.metrics[-1].net_recv_mb,
            "max_open_files": max(m.open_files for m in self.metrics) if self.metrics else 0,
            "max_threads": max(m.threads for m in self.metrics) if self.metrics else 0,
        }


class LoadTester:
    def __init__(self, url: str, wav_file: str, provider: str = "mock"):
        self.url = url
        self.wav_file = wav_file
        self.provider = provider
        self.stats: Dict[int, ConnectionStats] = {}
        self.active_connections = 0
        self.peak_connections = 0
        self.lock = asyncio.Lock()
        self.metrics_collector = SystemMetricsCollector()
        
    def read_wav(self):
        """Read WAV file and return audio data."""
        with wave.open(self.wav_file, 'rb') as wf:
            return {
                "sample_rate": wf.getframerate(),
                "channels": wf.getnchannels(),
                "sample_width": wf.getsampwidth(),
                "n_frames": wf.getnframes(),
                "audio_data": wf.readframes(wf.getnframes())
            }

    async def single_connection(self, conn_id: int, wav: dict, duration_sec: int):
        """Simulate a single WebSocket connection."""
        stats = ConnectionStats(connection_id=conn_id)
        self.stats[conn_id] = stats
        
        stats.start_time = time.time()
        
        try:
            connect_start = time.time()
            # Disable client-side pings to reduce overhead/timeouts at scale
            # Increase close_timeout to allow graceful shutdown
            async with websockets.connect(
                self.url, 
                max_size=10*1024*1024,
                ping_interval=None,
                close_timeout=10
            ) as ws:
                stats.connect_time_ms = (time.time() - connect_start) * 1000
                stats.connected = True
                
                async with self.lock:
                    self.active_connections += 1
                    if self.active_connections > self.peak_connections:
                        self.peak_connections = self.active_connections
                
                # Send start metadata
                session_id = str(uuid.uuid4())[:8]
                start_meta = {
                    "uuid": f"load-{conn_id}-{session_id}",
                    "sip_call_id": f"load-sip-{conn_id}-{session_id}",
                    "sample_rate": wav['sample_rate'],
                    "channels": wav['channels'],
                    "lang": "en-US",
                    "provider": self.provider,
                    "location": "global",
                }
                await ws.send(json.dumps(start_meta))
                
                # Start receiver task
                receive_task = asyncio.create_task(self._receive_loop(ws, stats))
                
                # Stream audio for duration
                bytes_per_sample = wav['sample_width'] * wav['channels']
                samples_per_chunk = int(wav['sample_rate'] * CHUNK_DURATION_MS / 1000)
                chunk_size = samples_per_chunk * bytes_per_sample
                
                audio_data = wav['audio_data']
                end_time = time.time() + duration_sec
                offset = 0
                
                while time.time() < end_time:
                    chunk = audio_data[offset:offset + chunk_size]
                    if not chunk:
                        offset = 0  # Loop audio
                        chunk = audio_data[offset:offset + chunk_size]
                    
                    try:
                        await ws.send(chunk)
                        stats.bytes_sent += len(chunk)
                    except websockets.exceptions.ConnectionClosed:
                        break
                        
                    offset += chunk_size
                    await asyncio.sleep(CHUNK_DURATION_MS / 1000)
                
                # Send stop if still connected
                try:
                    await ws.send(json.dumps({"action": "stop"}))
                    # Wait a bit for final results
                    await asyncio.sleep(1)
                except websockets.exceptions.ConnectionClosed:
                    pass
                
                receive_task.cancel()
                
        except (websockets.exceptions.ConnectionClosedOK, websockets.exceptions.ConnectionClosedError):
            # These are expected if server closes connection
            pass
        except Exception as e:
            # Only log actual unexpected errors
            if "no close frame" not in str(e):
                stats.errors.append(str(e))
        finally:
            stats.end_time = time.time()
            async with self.lock:
                self.active_connections -= 1

    async def _receive_loop(self, ws, stats: ConnectionStats):
        """Receive messages from WebSocket."""
        try:
            async for message in ws:
                try:
                    data = json.loads(message)
                    stats.messages_received += 1
                    if data.get("is_final"):
                        stats.finals_received += 1
                except json.JSONDecodeError:
                    pass
        except asyncio.CancelledError:
            pass
        except websockets.exceptions.ConnectionClosed:
            pass

    async def run_scale_test(self, num_connections: int, duration_sec: int) -> LoadTestResults:
        """Run scale test - spawn all connections at once."""
        print(f"\n{'='*70}")
        print(f"SCALE TEST: {num_connections} connections, {duration_sec}s duration")
        print(f"{'='*70}")
        print(f"URL: {self.url}")
        print(f"Provider: {self.provider}")
        print(f"WAV: {self.wav_file}")
        print(f"{'='*70}\n")
        
        wav = self.read_wav()
        
        # Start metrics collection
        await self.metrics_collector.start()
        start_time = time.time()
        
        # Spawn all connections
        tasks = []
        print(f"[{self._ts()}] Spawning {num_connections} connections...")
        for i in range(num_connections):
            task = asyncio.create_task(self.single_connection(i, wav, duration_sec))
            tasks.append(task)
            # Small delay between spawns to avoid overwhelming
            if i < 100:
                await asyncio.sleep(0.01)
            elif i < 500:
                await asyncio.sleep(0.005)
        
        print(f"[{self._ts()}] All connections spawned. Running for {duration_sec}s...")
        
        # Progress reporting
        progress_task = asyncio.create_task(self._report_progress(start_time, duration_sec))
        
        # Wait for all to complete
        await asyncio.gather(*tasks, return_exceptions=True)
        progress_task.cancel()
        
        # Stop metrics collection
        await self.metrics_collector.stop()
        
        elapsed = time.time() - start_time
        return self._calculate_results("scale", num_connections, duration_sec, elapsed)

    async def run_longevity_test(self, num_connections: int, duration_sec: int) -> LoadTestResults:
        """Run longevity test - maintain steady connections over time."""
        print(f"\n{'='*70}")
        print(f"LONGEVITY TEST: {num_connections} connections, {duration_sec}s duration")
        print(f"{'='*70}")
        print(f"URL: {self.url}")
        print(f"Provider: {self.provider}")
        print(f"Expected runtime: {duration_sec/60:.1f} minutes")
        print(f"{'='*70}\n")
        
        wav = self.read_wav()
        
        # Start metrics collection
        await self.metrics_collector.start()
        start_time = time.time()
        end_time = start_time + duration_sec
        
        # Use a connection pool
        active_tasks = set()
        conn_id = 0
        
        print(f"[{self._ts()}] Starting longevity test...")
        
        # Progress reporting
        progress_task = asyncio.create_task(self._report_progress(start_time, duration_sec))
        
        while time.time() < end_time:
            # Maintain target number of connections
            while len(active_tasks) < num_connections and time.time() < end_time:
                # Create connection that runs for 60s then reconnects
                task = asyncio.create_task(self.single_connection(conn_id, wav, 60))
                active_tasks.add(task)
                conn_id += 1
                await asyncio.sleep(0.05)
            
            # Clean up completed tasks
            done_tasks = {t for t in active_tasks if t.done()}
            active_tasks -= done_tasks
            
            await asyncio.sleep(1)
        
        # Wait for remaining
        if active_tasks:
            await asyncio.gather(*active_tasks, return_exceptions=True)
        
        progress_task.cancel()
        
        # Stop metrics collection
        await self.metrics_collector.stop()
        
        elapsed = time.time() - start_time
        return self._calculate_results("longevity", num_connections, duration_sec, elapsed)

    async def run_simulation(self, target_concurrent: int, duration_sec: int, spike_factor: float = 1.0, fixed_call_dur: int = 0) -> LoadTestResults:
        """
        Run realistic traffic simulation.
        - Poisson arrival process (random start times)
        - variable call durations (short, medium, long) OR fixed duration
        - traffic spikes if spike_factor > 1.0
        """
        print(f"\n{'='*70}")
        print(f"TRAFFIC SIMULATION: Target {target_concurrent} concurrent calls")
        print(f"Duration: {duration_sec}s, Spike Factor: {spike_factor}x")
        if fixed_call_dur > 0:
            print(f"Call Duration: {fixed_call_dur}s (Fixed)")
        print(f"{'='*70}")
        print(f"URL: {self.url}")
        print(f"Provider: {self.provider}")
        print(f"{'='*70}\n")
        
        wav = self.read_wav()
        
        # Start metrics collection
        await self.metrics_collector.start()
        start_time = time.time()
        end_time = start_time + duration_sec
        
        active_tasks = set()
        conn_id = 0
        total_spawned = 0
        
        # Progress reporting
        progress_task = asyncio.create_task(self._report_progress(start_time, duration_sec))
        
        # Determine arrival rate
        if fixed_call_dur > 0:
            avg_call_duration = float(fixed_call_dur)
        else:
            avg_call_duration = 60.0 # approx avg of random mix
            
        arrival_rate = target_concurrent / avg_call_duration
        
        print(f"[{self._ts()}] Starting simulation (Arrival Rate: ~{arrival_rate:.2f} calls/sec)...")
        
        import random
        
        while time.time() < end_time:
            # Dynamic arrival rate based on time (spike simulation)
            current_rate = arrival_rate
            elapsed = time.time() - start_time
            
            # Create a spike in the middle
            if duration_sec * 0.4 < elapsed < duration_sec * 0.6:
                 current_rate *= spike_factor
            
            # Poisson arrival
            inter_arrival = -math.log(1.0 - random.random()) / current_rate
            await asyncio.sleep(inter_arrival)
            
            if time.time() >= end_time:
                break
                
            if fixed_call_dur > 0:
                call_dur = float(fixed_call_dur)
            else:
                # Random duration logic
                rand_val = random.random()
                if rand_val < 0.2:
                    call_dur = random.uniform(5, 15)
                elif rand_val < 0.8:
                    call_dur = random.uniform(30.0, 90.0)
                else:
                    call_dur = random.uniform(120.0, 300.0)
            
            task = asyncio.create_task(self.single_connection(conn_id, wav, int(call_dur)))
            active_tasks.add(task)
            conn_id += 1
            total_spawned += 1
            
            # Clean up completed tasks
            done_tasks = {t for t in active_tasks if t.done()}
            active_tasks -= done_tasks
            
        print(f"[{self._ts()}] ramping down... waiting for {len(active_tasks)} active calls to finish")
        
        # Wait for remaining calls to finish
        if active_tasks:
            await asyncio.gather(*active_tasks, return_exceptions=True)
            
        progress_task.cancel()
        
        # Stop metrics collection
        await self.metrics_collector.stop()
        
        elapsed = time.time() - start_time
        return self._calculate_results("simulation", target_concurrent, duration_sec, elapsed)

    async def _report_progress(self, start_time: float, total_duration: int):
        """Report progress periodically."""
        try:
            while True:
                await asyncio.sleep(10)
                elapsed = time.time() - start_time
                pct = (elapsed / total_duration) * 100
                total_msgs = sum(s.messages_received for s in self.stats.values())
                
                # Get latest metrics
                metrics_summary = self.metrics_collector.get_summary()
                cpu = metrics_summary.get("cpu_avg", 0)
                mem = metrics_summary.get("memory_avg_mb", 0)
                
                print(f"[{self._ts()}] Progress: {pct:.1f}% | Active: {self.active_connections} | Peak: {self.peak_connections} | "
                      f"Total Calls: {len(self.stats)} | CPU: {cpu:.1f}% | Memory: {mem:.0f}MB")
        except asyncio.CancelledError:
            pass


    def _calculate_results(self, mode: str, target_conns: int, target_dur: int, elapsed: float) -> LoadTestResults:
        """Calculate aggregate results."""
        results = LoadTestResults()
        results.test_mode = mode
        results.target_connections = target_conns
        results.target_duration = target_dur
        results.actual_duration_seconds = elapsed
        results.peak_connections = self.peak_connections
        
        connect_times = []
        for stats in self.stats.values():
            results.total_connections += 1
            if stats.connected:
                results.successful_connections += 1
                connect_times.append(stats.connect_time_ms)
            else:
                results.failed_connections += 1
            results.total_messages += stats.messages_received
            results.total_finals += stats.finals_received
            results.total_bytes_sent += stats.bytes_sent
        
        if connect_times:
            results.avg_connect_time_ms = statistics.mean(connect_times)
            results.min_connect_time_ms = min(connect_times)
            results.max_connect_time_ms = max(connect_times)
        
        if elapsed > 0:
            results.connections_per_second = results.successful_connections / elapsed
            results.messages_per_second = results.total_messages / elapsed
            results.bytes_per_second = results.total_bytes_sent / elapsed
        
        # System metrics
        metrics = self.metrics_collector.get_summary()
        if metrics:
            results.avg_cpu_percent = metrics.get("cpu_avg", 0)
            results.max_cpu_percent = metrics.get("cpu_max", 0)
            results.avg_memory_mb = metrics.get("memory_avg_mb", 0)
            results.max_memory_mb = metrics.get("memory_max_mb", 0)
            results.total_disk_read_mb = metrics.get("disk_read_mb", 0)
            results.total_disk_write_mb = metrics.get("disk_write_mb", 0)
            results.total_net_sent_mb = metrics.get("net_sent_mb", 0)
            results.total_net_recv_mb = metrics.get("net_recv_mb", 0)
        
        # Analyze Pusher logs
        pusher_stats = self._analyze_pusher_logs()
        if pusher_stats:
            results.pusher_events_logged = pusher_stats.get("total", 0)
            results.pusher_sessions_started = pusher_stats.get("session-started", 0)
            results.pusher_sessions_stopped = pusher_stats.get("session-stopped", 0)
            results.pusher_transcripts = pusher_stats.get("transcripts", 0)
            
            # Validate: sessions started should match successful connections
            results.validation_passed = (
                results.pusher_sessions_started == results.successful_connections and
                results.pusher_sessions_stopped == results.successful_connections
            )
        
        return results
    
    def _analyze_pusher_logs(self) -> Dict:
        """Analyze mock Pusher log file"""
        log_file = Path("../mock_pusher_events.jsonl")
        if not log_file.exists():
            log_file = Path("mock_pusher_events.jsonl")
        if not log_file.exists():
            return {}
        
        stats = defaultdict(int)
        try:
            with open(log_file, 'r') as f:
                for line in f:
                    try:
                        event = json.loads(line)
                        event_type = event.get("event", "unknown")
                        stats[event_type] += 1
                        stats["total"] += 1
                        if "transcript" in event_type:
                            stats["transcripts"] += 1
                    except json.JSONDecodeError:
                        pass
        except Exception as e:
            print(f"Warning: Could not analyze pusher logs: {e}")
        
        return dict(stats)

    def print_results(self, results: LoadTestResults):
        """Print comprehensive test results."""
        metrics = self.metrics_collector.get_summary()
        
        print(f"\n{'='*70}")
        print("LOAD TEST RESULTS SUMMARY")
        print(f"{'='*70}")
        
        print(f"\n--- Test Configuration ---")
        print(f"Mode:                    {results.test_mode}")
        print(f"Target Connections:      {results.target_connections}")
        print(f"Target Duration:         {results.target_duration}s")
        print(f"Actual Duration:         {results.actual_duration_seconds:.1f}s")
        
        print(f"\n--- Connection Statistics ---")
        print(f"Peak Connections:        {results.peak_connections}")
        print(f"Total Connections:       {results.total_connections}")
        print(f"Successful:              {results.successful_connections}")
        print(f"Failed:                  {results.failed_connections}")
        print(f"Success Rate:            {(results.successful_connections/max(1,results.total_connections))*100:.1f}%")
        
        print(f"\n--- Message Statistics ---")
        print(f"Total Messages:          {results.total_messages}")
        print(f"Total Finals:            {results.total_finals}")
        print(f"Total Bytes Sent:        {results.total_bytes_sent / (1024*1024):.2f} MB")
        
        print(f"\n--- Timing ---")
        print(f"Connect Time (avg):      {results.avg_connect_time_ms:.2f} ms")
        print(f"Connect Time (min):      {results.min_connect_time_ms:.2f} ms")
        print(f"Connect Time (max):      {results.max_connect_time_ms:.2f} ms")
        
        print(f"\n--- Throughput ---")
        print(f"Connections/sec:         {results.connections_per_second:.2f}")
        print(f"Messages/sec:            {results.messages_per_second:.2f}")
        print(f"Bytes/sec:               {results.bytes_per_second / 1024:.2f} KB/s")
        
        print(f"\n--- System Performance ---")
        print(f"CPU Cores:               {metrics.get('cpu_count', 'N/A')}")
        print(f"CPU Frequency:           {metrics.get('cpu_freq_mhz', 0):.0f} MHz")
        print(f"CPU Usage (avg):         {results.avg_cpu_percent:.1f}%")
        print(f"CPU Usage (max):         {results.max_cpu_percent:.1f}%")
        print(f"Memory Usage (avg):      {results.avg_memory_mb:.0f} MB")
        print(f"Memory Usage (max):      {results.max_memory_mb:.0f} MB")
        print(f"Disk Read:               {results.total_disk_read_mb:.2f} MB")
        print(f"Disk Write:              {results.total_disk_write_mb:.2f} MB")
        print(f"Network Sent:            {results.total_net_sent_mb:.2f} MB")
        print(f"Network Received:        {results.total_net_recv_mb:.2f} MB")
        print(f"Max Open Files:          {metrics.get('max_open_files', 'N/A')}")
        print(f"Max Threads:             {metrics.get('max_threads', 'N/A')}")
        
        print(f"\n--- Pusher Log Validation ---")
        print(f"Events Logged:           {results.pusher_events_logged}")
        print(f"Sessions Started:        {results.pusher_sessions_started}")
        print(f"Sessions Stopped:        {results.pusher_sessions_stopped}")
        print(f"Transcripts:             {results.pusher_transcripts}")
        print(f"Validation:              {'✅ PASSED' if results.validation_passed else '❌ FAILED'}")
        
        print(f"\n{'='*70}")
        
        # Print errors if any
        errors = [(s.connection_id, s.errors) for s in self.stats.values() if s.errors]
        if errors:
            print(f"\nERRORS ({len(errors)} connections had errors):")
            for conn_id, errs in errors[:10]:
                print(f"  Connection {conn_id}: {errs[0][:100]}")
            if len(errors) > 10:
                print(f"  ... and {len(errors) - 10} more")
        
        # Save results to JSON
        self._save_results(results, metrics)
    
    def _save_results(self, results: LoadTestResults, metrics: Dict):
        """Save results to JSON file"""
        filename = f"load_test_results_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        
        output = {
            "test_config": {
                "mode": results.test_mode,
                "target_connections": results.target_connections,
                "target_duration": results.target_duration,
                "provider": self.provider,
                "url": self.url,
                "wav_file": self.wav_file,
            },
            "results": {
                "actual_duration": results.actual_duration_seconds,
                "peak_connections": results.peak_connections,
                "total_connections": results.total_connections,
                "successful_connections": results.successful_connections,
                "failed_connections": results.failed_connections,
                "total_messages": results.total_messages,
                "total_finals": results.total_finals,
                "total_bytes_sent": results.total_bytes_sent,
                "avg_connect_time_ms": results.avg_connect_time_ms,
                "messages_per_second": results.messages_per_second,
            },
            "system_metrics": metrics,
            "pusher_validation": {
                "events_logged": results.pusher_events_logged,
                "sessions_started": results.pusher_sessions_started,
                "sessions_stopped": results.pusher_sessions_stopped,
                "transcripts": results.pusher_transcripts,
                "validation_passed": results.validation_passed,
            },
            "timestamp": datetime.now().isoformat(),
        }
        
        with open(filename, 'w') as f:
            json.dump(output, f, indent=2)
        
        print(f"\nResults saved to: {filename}")

    @staticmethod
    def _ts():
        return datetime.now().strftime('%H:%M:%S')


def main():
    parser = argparse.ArgumentParser(
        description="Enhanced Load Test for FS-Unified-STT",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  Scale test (100 connections, 60s):
    python load_test.py --connections 100 --duration 60

  Longevity test (50 connections, 1 hour):
    python load_test.py --mode longevity --connections 50 --duration 3600

  Quick test:
    python load_test.py --connections 10 --duration 30
        """
    )
    parser.add_argument("--mode", default="scale", choices=["scale", "longevity", "simulation"],
                        help="Test mode: scale (burst), longevity (sustained), or simulation (realistic)")
    parser.add_argument("--connections", type=int, required=True,
                        help="Number of concurrent connections (Target concurrent for simulation)")
    parser.add_argument("--duration", type=int, required=True,
                        help="Test duration in seconds")
    parser.add_argument("--spike", type=float, default=1.5,
                        help="Spike factor for simulation (default: 1.5x)")
    parser.add_argument("--call-duration", type=int, default=0,
                        help="Fixed call duration in seconds for simulation (default: 0 = random mix)")
    parser.add_argument("--url", default=DEFAULT_URL,
                        help=f"WebSocket URL (default: {DEFAULT_URL})")
    parser.add_argument("--wav", default=DEFAULT_WAV,
                        help=f"WAV file to stream (default: {DEFAULT_WAV})")
    parser.add_argument("--provider", default="mock",
                        help="Provider to use (default: mock for load testing)")
    
    args = parser.parse_args()
    
    if not Path(args.wav).exists():
        print(f"ERROR: WAV file not found: {args.wav}")
        sys.exit(1)
    
    # Check for psutil
    try:
        import psutil
    except ImportError:
        print("ERROR: psutil not installed. Run: pip install psutil")
        sys.exit(1)
    
    tester = LoadTester(args.url, args.wav, args.provider)
    
    if args.mode == "scale":
        results = asyncio.run(tester.run_scale_test(args.connections, args.duration))
    elif args.mode == "longevity":
        results = asyncio.run(tester.run_longevity_test(args.connections, args.duration))
    else:
        results = asyncio.run(tester.run_simulation(args.connections, args.duration, args.spike, args.call_duration))
    
    tester.print_results(results)


if __name__ == "__main__":
    main()
