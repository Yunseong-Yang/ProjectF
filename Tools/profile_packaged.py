"""Collect a bounded, local, single-player packaged rendering sample.

This sample cannot certify four-player combat, split groups, production art, or internet play.
"""
from pathlib import Path
import csv
import ctypes
from ctypes import wintypes
import json
import shutil
import subprocess
import time
import sys
from datetime import datetime, timezone, timedelta

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT / "Builds/ProjectF_Development/Windows/ProjectF/Binaries/Win64/ProjectF.exe"
BASE_OUTPUT = ROOT / "Saved/Implementation/20261029_FullPlan/Profile"
run_id = datetime.now(timezone(timedelta(hours=9))).strftime("%Y%m%d_%H%M%S")
OUTPUT = BASE_OUTPUT / run_id
OUTPUT.mkdir(parents=True, exist_ok=True)
csv.field_size_limit(16 * 1024 * 1024)
quality = ",".join(f"sg.{name}Quality 1" for name in (
    "ViewDistance", "AntiAliasing", "Shadow", "GlobalIllumination", "Reflection", "PostProcess", "Texture", "Effects", "Foliage", "Shading"))
arguments = [str(GAME), "/Game/ProjectF/Maps/Tests/L_CoopSystems", "-game", "-RenderOffscreen", "-windowed",
             "-ResX=1920", "-ResY=1080", "-ForceRes", "-unattended", "-nosplash", "-nosound", "-PFVisualCapture",
             "-saveddirsuffix=PF_Profile", "-csvCaptureFrames=600", "-csvGpuStats", f"-ExecCmds={quality}",
             f"-abslog={OUTPUT / 'packaged-idle.log'}"]
trace_enabled = "--trace" in sys.argv
if trace_enabled:
    arguments.extend(["-trace=cpu,gpu,frame,counters,bookmark", f"-tracefile={OUTPUT / 'packaged-idle.utrace'}"])

class MemoryCounters(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
        (name, ctypes.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
            "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]

kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
memory_api = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
memory_api.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
memory_api.restype = wintypes.BOOL
started = time.monotonic()
started_wall = time.time()
peak_working_set = peak_private = 0
gpu_samples = []
samples = 0
with (OUTPUT / "console.log").open("w", encoding="utf-8") as console:
    process = subprocess.Popen(arguments, cwd=GAME.parent, stdout=console, stderr=subprocess.STDOUT,
                               creationflags=subprocess.CREATE_NO_WINDOW)
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, process.pid)
    try:
        next_gpu_sample = 0.0
        while process.poll() is None:
            counters = MemoryCounters()
            counters.cb = ctypes.sizeof(counters)
            if handle and memory_api(handle, ctypes.byref(counters), counters.cb):
                peak_working_set = max(peak_working_set, counters.PeakWorkingSetSize)
                peak_private = max(peak_private, counters.PrivateUsage)
                samples += 1
            elapsed = time.monotonic() - started
            if elapsed >= next_gpu_sample:
                try:
                    value = subprocess.check_output(["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
                                                    text=True, timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
                    gpu_samples.append(float(value.splitlines()[0].strip()))
                except (OSError, ValueError, subprocess.SubprocessError):
                    pass
                next_gpu_sample = elapsed + 1.0
            if elapsed > 120:
                process.terminate()
                raise RuntimeError("Packaged visual capture exceeded the bounded timeout")
            time.sleep(.25)
    finally:
        if handle:
            kernel.CloseHandle(handle)
        if process.poll() is None:
            process.terminate()
    exit_code = process.wait()

saved = ROOT / "Builds/ProjectF_Development/Windows/ProjectF/Saved_PF_Profile"
csv_stats = []
for path in saved.rglob("*.csv"):
    if path.parent.name.lower() != "csv" or path.stat().st_mtime < started_wall - 1:
        continue
    shutil.copy2(path, OUTPUT / path.name)
    rows = list(csv.reader(path.open(encoding="utf-8-sig")))
    if not rows:
        continue
    header = rows[0]
    for index, name in enumerate(header):
        if not any(token in name.lower() for token in ("frametime", "gamethread", "renderthread", "gpu/fram", "gputime")):
            continue
        values = []
        for row in rows[1:]:
            try:
                values.append(float(row[index]))
            except (ValueError, IndexError):
                continue
        if not values:
            continue
        values.sort()
        def percentile(q):
            position = (len(values) - 1) * q
            low = int(position)
            return values[low] + (values[min(low + 1, len(values) - 1)] - values[low]) * (position - low)
        csv_stats.append({"file": path.name, "column": name, "samples": len(values),
                          "mean": sum(values) / len(values), "p95": percentile(.95), "p99": percentile(.99), "maximum": values[-1]})
for path in saved.rglob("PF_*.png"):
    shutil.copy2(path, OUTPUT / path.name)
record = {"exit_code": exit_code, "elapsed_seconds": time.monotonic() - started, "memory_samples": samples,
          "trace_enabled": trace_enabled,
          "peak_process_working_set_bytes": peak_working_set if samples else None, "peak_sampled_private_bytes": peak_private if samples else None,
          "peak_sampled_whole_gpu_memory_mib": max(gpu_samples) if gpu_samples else None,
          "gpu_samples": len(gpu_samples), "csv_stats": csv_stats,
          "settings": {"resolution": "1920x1080", "scalability": "1 (Medium)", "render_offscreen": True},
          "scope": "One local player at safe camp; 16 distant server AI; HUD and menu; bounded capture",
          "limitations": ["GPU memory is whole-device usage, including other applications", "GPU peaks between samples may be missed",
                          "Startup/capture overhead and driver cache are not controlled", "Four-player dense combat and 2+2 split combat are unmeasured",
                          "Test geometry and template art are unapproved", "No baseline improvement claim"],
          "overall_performance_acceptance": "FAIL - instrumented run exited abnormally; production workload UNVERIFIED" if exit_code else "UNVERIFIED - NOT ACCEPTED"}
(OUTPUT / "profile.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
shutil.copy2(OUTPUT / "profile.json", BASE_OUTPUT / "profile.json")
print(json.dumps(record, indent=2))
if exit_code != 0:
    raise SystemExit(exit_code)
