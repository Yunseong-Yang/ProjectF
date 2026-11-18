"""Bounded local frame/RAM/VRAM capture without the failing CSV shutdown path."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import json
import hashlib
import math
import re
import statistics
import subprocess
import time
from datetime import datetime, timezone, timedelta
from windows_gpu_counters import GPUProcessCounters

ROOT = Path(__file__).resolve().parents[1]
COLLECTOR_SHA256 = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
parser = argparse.ArgumentParser()
parser.add_argument("--editor", action="store_true")
parser.add_argument("--view", default="camp", choices=("camp", "radio", "stronghold"))
parser.add_argument("--players", type=int, choices=(1, 4), default=1)
parser.add_argument("--split", action="store_true")
parser.add_argument("--trace", action="store_true")
parser.add_argument("--map", default="/Game/ProjectF/Maps/L_MountainFrontier")
parser.add_argument("--camera", help="Explicit x,y,z,pitch,yaw in centimetres/degrees")
parser.add_argument("--label", help="Scenario label for matched geometry comparisons")
parser.add_argument("--evidence-folder", default="20261030_Continuation")
parser.add_argument("--outpost", help="Render the specified outpost during the four-player stress fixture")
parser.add_argument("--audio", action="store_true", help="Include game audio work in the timing workload")
parser.add_argument("--details", action="store_true", help="Log individual enemy state during the opted-in stress audit")
parser.add_argument("--gunfire-cues", action="store_true", help="Opt into normal player gunshot hearing stimuli every three seconds for maximum-combat coverage")
parser.add_argument("--package-directory", default="ProjectF_Development")
parser.add_argument("--warmup-seconds", type=float, help="Bounded sustained workload before the final 1200-frame sample (5-1800 seconds)")
parser.add_argument("--stress-health", type=int, default=100000, help="Synthetic player maximum health for the explicitly opted-in stress fixture only")
args = parser.parse_args()
if not 100000 <= args.stress_health <= 100000000: parser.error("Synthetic stress health must be between100000 and100000000")
if args.warmup_seconds is not None and (not math.isfinite(args.warmup_seconds) or not 5 <= args.warmup_seconds <= 1800):
    parser.error("Warmup must be finite and between 5 and 1800 seconds")
if not re.fullmatch(r"ProjectF_[A-Za-z0-9_]+", args.package_directory): parser.error("Invalid package directory")
if not re.fullmatch(r"/Game/[A-Za-z0-9_/]+", args.map): parser.error("Invalid game map")
for value in (args.evidence_folder, args.label or "default"):
    if not re.fullmatch(r"[A-Za-z0-9_]+", value): parser.error("Invalid output name")
if args.camera:
    try: camera_values = [float(value) for value in args.camera.split(",")]
    except ValueError: parser.error("Camera needs five finite numbers")
    if len(camera_values) != 5 or not all(math.isfinite(value) for value in camera_values): parser.error("Camera needs five finite numbers")
    if args.players != 1 or args.split: parser.error("An explicit camera requires a single-player geometry capture")
if args.split: args.players = 4
if args.gunfire_cues and args.players != 4: parser.error("Gunfire hearing cues require the four-player stress fixture")
if args.outpost and (args.players != 4 or not re.fullmatch(r"[A-Za-z0-9_]+", args.outpost)): parser.error("An outpost requires four players and a valid id")
warmup_seconds = args.warmup_seconds if args.warmup_seconds is not None else (45 if args.players == 4 else 25)
game = ROOT / "Builds" / args.package_directory / "Windows/ProjectF/Binaries/Win64/ProjectF.exe"
if args.editor: game = Path("C:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
stamp = datetime.now(timezone(timedelta(hours=9))).strftime("%Y%m%d_%H%M%S")
scenario = args.label or ("split4" if args.split else "dense4" if args.players == 4 else args.view)
output = ROOT / "Saved/Implementation" / args.evidence_folder / "Performance" / (stamp + "_" + scenario)
output.mkdir(parents=True, exist_ok=True)
saved_suffix = "PF_FrontierProfile_" + stamp + "_" + scenario
quality = ",".join(["r.VSync 0", "t.MaxFPS 0", "r.ScreenPercentage 100"] + [f"sg.{name}Quality 1" for name in (
    "ViewDistance", "AntiAliasing", "Shadow", "GlobalIllumination", "Reflection", "PostProcess", "Texture", "Effects", "Foliage", "Shading")])
command = [str(game)]
if args.editor: command.append(str(ROOT / "ProjectF.uproject"))
if args.editor: command.append("-DisablePlugins=Monolith,NarshaMCP,ModelContextProtocol,ToolsetRegistry")
protocol = int(__import__("re").search(r"ProtocolVersion\s*=\s*(\d+)", (ROOT / "Source/ProjectF/Public/Core/PFTypes.h").read_text(encoding="utf-8")).group(1))
map_url = args.map + (f"?listen?PFBuild={protocol}" if args.players == 4 else "")
command += [map_url, "-game", "-RenderOffscreen", "-windowed", "-ResX=1920", "-ResY=1080", "-ForceRes",
            "-unattended", "-nosplash", "-PFFrameProbe", f"-PFFrameProbeWarmup={warmup_seconds}", "-PFFrameProbeFrames=1200",
            "-saveddirsuffix=" + saved_suffix, f"-ExecCmds={quality}", f"-abslog={output / 'runtime.log'}"]
if args.trace: command += ["-trace=cpu,gpu,frame,bookmark", "-tracefile=" + str(output / "native.utrace")]
if not args.audio: command.append("-nosound")
if args.outpost: command.append("-PFStressOutpost=" + args.outpost)
if args.details: command.append("-PFStressDetails")
if args.gunfire_cues: command.append("-PFStressGunfire")
if args.players == 4:
    command += ["-PFStress", "-PFNetExpected=4", f"-PFStressHealth={args.stress_health}", "-port=18120"]
    if args.split: command.append("-PFStressSplit")
views = {"radio": "-20000,-6000,5200,-10,43", "stronghold": "10500,4500,6800,-12,40"}
if args.camera: command.append("-PFReviewCamera=" + args.camera)
elif args.players == 1 and args.view in views: command.append("-PFReviewCamera=" + views[args.view])


class MemoryCounters(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [(name, ctypes.c_size_t) for name in (
        "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
        "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


kernel = ctypes.WinDLL("kernel32", use_last_error=True)
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]; kernel.OpenProcess.restype = wintypes.HANDLE
kernel.CloseHandle.argtypes = [wintypes.HANDLE]
memory_api = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
memory_api.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
started = time.monotonic()
gpu_samples, memory_samples, process_gpu_samples = [], [], []
gpu_counter, gpu_counter_error = None, None
try: gpu_counter = GPUProcessCounters()
except OSError as error: gpu_counter_error = str(error)
prelaunch_largest_gpu_allocation = None
if gpu_counter:
    try: prelaunch_largest_gpu_allocation = max((v.get("dedicated", 0) for v in gpu_counter.sample().values()), default=None)
    except OSError as error: gpu_counter_error = str(error)
clients, client_logs = [], []
clients_started = False
early_client_exits = []
observed_client_exits = set()
with (output / "console.log").open("w", encoding="utf-8") as console:
    process = subprocess.Popen(command, cwd=ROOT, stdout=console, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, process.pid)
    next_gpu = 0
    next_progress = 60
    try:
        while process.poll() is None:
            for index, client in enumerate(clients, 1):
                code = client.poll()
                if code is not None and index not in observed_client_exits:
                    observed_client_exits.add(index)
                    captured = (output / "runtime.log").exists() and "PF_FRAME_PROBE: saved=1" in (output / "runtime.log").read_text(encoding="utf-8", errors="replace")
                    if not captured: early_client_exits.append({"client": index, "exit_code": code, "seconds": time.monotonic() - started})
            if args.players == 4 and not clients_started:
                runtime_log = output / "runtime.log"
                if runtime_log.exists() and "Game Engine Initialized" in runtime_log.read_text(encoding="utf-8", errors="replace"):
                    for index in range(1, 4):
                        log = (output / f"client{index}-console.log").open("w", encoding="utf-8"); client_logs.append(log)
                        client_command = [str(game)] + ([str(ROOT / "ProjectF.uproject")] if args.editor else [])
                        if args.editor: client_command.append("-DisablePlugins=Monolith,NarshaMCP,ModelContextProtocol,ToolsetRegistry")
                        client_command += [f"127.0.0.1:18120?PFBuild={protocol}", "-game", "-NullRHI", "-nosound", "-unattended", "-nop4", "-nosplash",
                                           "-PFStress", "-PFNetExpected=4", "-ExecCmds=t.MaxFPS 60", f"-saveddirsuffix={saved_suffix}_Client{index}", f"-abslog={output / f'client{index}.log'}"]
                        clients.append(subprocess.Popen(client_command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW))
                    clients_started = True
            stats = MemoryCounters(); stats.cb = ctypes.sizeof(stats)
            if handle and memory_api(handle, ctypes.byref(stats), stats.cb):
                memory_samples.append({"seconds": time.monotonic() - started, "working_set": stats.WorkingSetSize, "peak_working_set": stats.PeakWorkingSetSize, "private_bytes": stats.PrivateUsage})
            if time.monotonic() >= next_gpu:
                try:
                    value = subprocess.check_output(["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"], text=True, timeout=4, creationflags=subprocess.CREATE_NO_WINDOW)
                    gpu_samples.append(float(value.splitlines()[0].strip()))
                except (OSError, ValueError, subprocess.SubprocessError): pass
                if gpu_counter:
                    try:
                        sample = gpu_counter.sample().get(process.pid)
                        if sample is not None: process_gpu_samples.append({"seconds": time.monotonic() - started, **sample})
                    except OSError as error: gpu_counter_error = str(error)
                next_gpu = time.monotonic() + 1
            elapsed_seconds = time.monotonic() - started
            if elapsed_seconds >= next_progress:
                latest = memory_samples[-1] if memory_samples else {}
                print(json.dumps({"progress_seconds": round(elapsed_seconds), "working_set_bytes": latest.get("working_set"),
                                  "private_bytes": latest.get("private_bytes"), "clients_started": clients_started}), flush=True)
                next_progress += 60
            if elapsed_seconds > max(240, warmup_seconds + 180): raise TimeoutError("Local frame probe did not complete within its bounded run")
            time.sleep(.25)
    finally:
        if handle: kernel.CloseHandle(handle)
        if gpu_counter: gpu_counter.close()
        if process.poll() is None: process.terminate()
        process.wait(timeout=10)
        for client in clients:
            if client.poll() is None: client.terminate()
            client.wait(timeout=10)
        for log in client_logs: log.close()
saved = (ROOT if args.editor else game.parents[2]) / ("Saved_" + saved_suffix) / "Profiling/PF_FrameProbe.json"
probe = json.loads(saved.read_text(encoding="utf-8-sig")) if saved.exists() else None
runtime_text = (output / "runtime.log").read_text(encoding="utf-8", errors="replace") if (output / "runtime.log").exists() else ""
waves = [int(value) for value in re.findall(r"PF_STRESS_WAVE group=\d+ spawned=(\d+)", runtime_text)]
required_groups = 2 if args.split else 1
required_living = 28 if args.split else 26
required_combat = [14, 14] if args.split else [18, 0]
battle_metric = "active_battle_at_outposts" if args.gunfire_cues else "combat_at_outposts"
audits = [int(value) for value in re.findall(r"PF_STRESS_AUDIT[^\n]* passed=(\d+)", runtime_text)]
audit_pass = not any(value == 0 for value in audits) and (warmup_seconds < 300 or len(audits) >= max(1, int(warmup_seconds / 60) - 1))
expected_post_players = [2, 2] if args.split else [4, 0]
workload_pass = args.players == 1 or (probe is not None and not early_client_exits and len(clients) == 3
    and probe.get("players_ready") == 4 and probe.get("players_alive") == 4 and audit_pass
    and probe.get("minimum_player_max_health") == args.stress_health
    and (not args.gunfire_cues or probe.get("stress_gunfire_cues") is True)
    and probe.get("players_at_outposts") == expected_post_players
    and len(probe.get(battle_metric, [])) == 2 and all(probe[battle_metric][index] >= required_combat[index] for index in range(required_groups))
    and len(waves) >= required_groups and all(value >= 4 for value in waves[:required_groups]) and probe.get("living_enemies", 0) >= required_living)
scalability_file = ROOT / "Config/DefaultScalability.ini"
record = {"exit_code": process.returncode, "elapsed_seconds": time.monotonic() - started, "view": args.view, "scenario": scenario, "players": args.players,
          "map": args.map, "explicit_camera": args.camera, "requested_outpost": args.outpost, "audio_enabled": args.audio,
          "workload_verified": workload_pass, "reinforcement_wave_counts": waves,
          "early_client_exits": early_client_exits,
          "synthetic_player_health": args.stress_health if args.players == 4 else None,
          "native_footprint_audit_results": audits,
          "required_living_enemies": required_living if args.players == 4 else None,
          "scalability_config_sha256": hashlib.sha256(scalability_file.read_bytes()).hexdigest() if scalability_file.exists() else None,
          "fixture_revision": 7 if args.gunfire_cues else 6,
          "required_combat_at_outposts": required_combat if args.players == 4 and not args.gunfire_cues else None,
          "required_active_battle_at_outposts": required_combat if args.gunfire_cues else None,
          "battle_activity_definition": "Alive within45m of own engaged outpost, investigating/searching/fighting, with actual seen/heard player evidence within5s; raw Combat state is recorded separately" if args.gunfire_cues else "Raw Combat state snapshot",
          "individual_enemy_details": args.details,
          "simulated_player_gunshot_hearing": args.gunfire_cues,
          "headless_client_fps_limit": 60 if args.players == 4 else None,
          "collector_sha256": COLLECTOR_SHA256,
          "trace_path": str(output / "native.utrace") if args.trace else None,
          "binary_sha256": hashlib.sha256(game.read_bytes()).hexdigest(),
          "package_tables_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (game.parents[2] / "Content/Paks").glob("*.utoc")} if not args.editor else {},
          "deployment": "Editor standalone game" if args.editor else "Windows Development package", "resolution": "1920x1080", "quality": "Medium (1)",
          "requested_warmup_seconds": warmup_seconds,
          "frame_probe": probe, "peak_process_working_set_bytes": max((m["peak_working_set"] for m in memory_samples), default=None),
          "peak_process_private_bytes": max((m["private_bytes"] for m in memory_samples), default=None),
          "peak_sampled_process_gpu_dedicated_bytes": max((s["dedicated"] for s in process_gpu_samples if "dedicated" in s), default=None),
          "peak_sampled_process_gpu_shared_bytes": max((s["shared"] for s in process_gpu_samples if "shared" in s), default=None),
          "process_gpu_counter_error": gpu_counter_error,
          "prelaunch_largest_other_process_gpu_allocation_bytes": prelaunch_largest_gpu_allocation,
          "process_gpu_samples": len(process_gpu_samples),
          "peak_sampled_whole_gpu_memory_mib": max(gpu_samples, default=None), "memory_samples": len(memory_samples), "gpu_samples": len(gpu_samples),
          "limits": ["One rendered offscreen process; display presentation latency is not validated; three NullRHI clients capped at 60 FPS in cooperative stress scenarios", "Fixture revision 1 used uncapped headless clients; do not claim a project optimization by comparing the two fixtures", "Stress uses synthetic high player health and staged one-wave reinforcements; not a natural playthrough", "Sound is disabled during timing; audio CPU cost is not accepted by this capture", "Whole-device GPU memory and Windows per-process allocation gauges are sampled at 1Hz; a true allocation peak is not established", "Warm frame percentiles exclude startup; startup RAM peak is retained", "No baseline improvement claim", "Does not resolve the earlier CSV shutdown failure"]}
settled_memory = [sample for sample in memory_samples if 60 <= sample["seconds"] <= 180]
ending_memory = [sample for sample in memory_samples if sample["seconds"] >= max(60, time.monotonic() - started - 120)]
duration_pass = record["elapsed_seconds"] >= warmup_seconds
if duration_pass and warmup_seconds >= 300 and settled_memory and ending_memory:
    initial_private = statistics.median(sample["private_bytes"] for sample in settled_memory)
    final_private = statistics.median(sample["private_bytes"] for sample in ending_memory)
    record["memory_observation"] = {"settled_private_bytes_median": initial_private, "ending_private_bytes_median": final_private,
                                    "private_bytes_delta": final_private - initial_private,
                                    "scope": "Stationary combat actors; does not cover repeated destruction, streaming or save/reconnect churn"}
if args.audio:
    record["limits"] = [limit for limit in record["limits"] if not limit.startswith("Sound is disabled")]
    record["limits"].append("Audio is enabled for the rendered host; timing does not certify perceived mix/spatial quality or physical output devices")
if args.gunfire_cues:
    record["limits"].append("Explicitly simulated player gunshot hearing cues every3s at the normal100m maximum range; AI states are not forced; this does not add player gunshot audio or certify natural balance play")
frame = (probe or {}).get("frame", {})
frame_pass = (workload_pass and frame.get("samples", 0) >= 1200
              and all(isinstance(frame.get(key), (int, float)) and math.isfinite(frame[key])
                      and 0 < frame[key] <= budget for key, budget in (("p95_ms", 16.7), ("p99_ms", 25.0))))
ram_peak = record["peak_process_working_set_bytes"]
private_peak = record["peak_process_private_bytes"]
gpu_sample_peak = record["peak_sampled_process_gpu_dedicated_bytes"]
ram_pass = ram_peak is not None and 0 < ram_peak <= 8 * 1024**3
private_pass = private_peak is not None and 0 < private_peak <= 8 * 1024**3
gpu_sample_pass = gpu_sample_peak is not None and 0 < gpu_sample_peak <= 5 * 1024**3
known_failure = (process.returncode != 0 or not duration_pass or not frame_pass or (ram_peak is not None and not ram_pass)
                 or (private_peak is not None and not private_pass)
                 or (gpu_sample_peak is not None and not gpu_sample_pass))
record["acceptance"] = {
    "budget_source": "Docs/ProjectF_Design_and_Development_Plan.md section 21, provisional",
    "frame_workload_passed": frame_pass,
    "requested_duration_verified": duration_pass,
    "process_ram_within_budget": ram_pass if ram_peak is not None else None,
    "process_private_commit_within_budget": private_pass if private_peak is not None else None,
    "sampled_gpu_allocation_within_budget": gpu_sample_pass if gpu_sample_peak is not None else None,
    "scoped_samples_passed": not known_failure and ram_pass and private_pass and gpu_sample_pass,
    "overall": "FAIL" if known_failure else "UNVERIFIED_NOT_ACCEPTED",
    "remaining": ["True GPU allocation peak", "Sustained allocation cleanup and growth",
                  "Minimum hardware acceptance", "Perceived visual and audio quality"],
    "placement": "Authority, AI and navigation on CPU; mesh rendering on GPU; offline mesh cleanup in Blender",
}
(output / "profile.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
(output / "memory-samples.json").write_text(json.dumps(memory_samples), encoding="utf-8")
(output / "process-gpu-samples.json").write_text(json.dumps(process_gpu_samples), encoding="utf-8")
print(json.dumps({"output": str(output), **record}, ensure_ascii=False))
if not record["acceptance"]["scoped_samples_passed"]: raise SystemExit(1)
