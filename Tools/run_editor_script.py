"""Run a bounded local Unreal Python job and verify completion, not only process exit."""
from pathlib import Path
import argparse
import json
import re
import subprocess
import time

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("script", type=Path)
parser.add_argument("--label", required=True)
parser.add_argument("--marker", required=True)
parser.add_argument("--evidence-folder", default="20261030_Completion")
parser.add_argument("--timeout", type=int, default=1200)
parser.add_argument("--null-rhi", action="store_true", help="CPU-only editor work; never use for render-dependent export/capture")
parser.add_argument("--without-editor-assistants", action="store_true", help="Isolate optional editor assistants for a bounded batch job")
args = parser.parse_args()
script = (root / args.script).resolve()
if not script.is_relative_to(root / "Tools") or script.suffix != ".py" or not script.is_file():
    parser.error("Expected an existing script inside this project's Tools folder")
if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label) or not re.fullmatch(r"[A-Za-z0-9_-]+", args.evidence_folder) or not 30 <= args.timeout <= 1800:
    parser.error("Invalid label or timeout")
output = root / "Saved/Implementation" / args.evidence_folder
output.mkdir(parents=True, exist_ok=True)
log = output / (args.label + ".log")
result_file = output / (args.label + "-result.json")
if log.exists() or result_file.exists():
    parser.error("Choose a new label to preserve earlier attempts")
command = ["C:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(root / "ProjectF.uproject"),
    "-ExecutePythonScript=" + str(script), "-unattended", "-nop4", "-nosplash", "-nosound", "-RenderOffscreen",
    "-saveddirsuffix=PF_Completion", "-abslog=" + str(log)]
if args.null_rhi:
    command.append("-NullRHI")
if args.without_editor_assistants:
    command.append("-DisablePlugins=Monolith,NarshaMCP,ModelContextProtocol,ToolsetRegistry")
started = time.monotonic()
with (output / (args.label + "-console.log")).open("w", encoding="utf-8") as console:
    process = subprocess.run(command, cwd=root, stdout=console, stderr=subprocess.STDOUT,
        timeout=args.timeout, creationflags=subprocess.CREATE_NO_WINDOW)
text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
errors = [line for line in text.splitlines() if "LogPython: Error:" in line or "Fatal error:" in line]
record = {"script": str(script), "exit_code": process.returncode, "elapsed_seconds": time.monotonic()-started,
    "editor_assistants_disabled_for_this_process": args.without_editor_assistants,
    "completion_marker": args.marker, "marker_present": args.marker in text, "python_errors": errors,
    "passed": process.returncode == 0 and args.marker in text and not errors}
result_file.write_text(json.dumps(record, indent=2), encoding="utf-8")
print(json.dumps(record))
raise SystemExit(0 if record["passed"] else 1)
