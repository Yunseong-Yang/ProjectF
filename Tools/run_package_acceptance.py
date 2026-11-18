"""Wait for each local GUI game process, then run matched package checks sequentially."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
evidence = root / "Saved/Implementation/20261030_Continuation"
parser = argparse.ArgumentParser()
parser.add_argument("--label", required=True)
parser.add_argument("--build", default="package07")
parser.add_argument("--execution", action="store_true")
args = parser.parse_args()
assert re.fullmatch(r"[A-Za-z0-9_-]+", args.label)
assert re.fullmatch(r"[A-Za-z0-9_-]+", args.build)
output = evidence / ("Acceptance_" + args.label)
assert not output.exists(), "Preserve earlier acceptance attempts; choose another label"
output.mkdir()
fingerprint = json.loads((evidence / (args.build + "-fingerprint.json")).read_text(encoding="utf-8"))
game = root / fingerprint["binary"]["file"]
binary_hash = hashlib.sha256(game.read_bytes()).hexdigest()
assert binary_hash == fingerprint["binary"]["sha256"]
steps = []

def run(name, command, timeout):
    with (output / (name + "-console.log")).open("w", encoding="utf-8") as stream:
        process = subprocess.run(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT,
                                 timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
    steps.append({"name": name, "exit_code": process.returncode})
    (output / "execution.json").write_text(json.dumps({"binary_sha256": binary_hash, "steps": steps}, indent=2), encoding="utf-8")
    assert process.returncode == 0, f"{name} failed; retained evidence: {output}"

captures = [("title", ["-PFFrontEndCheck"], "/Game/ProjectF/Maps/L_MountainFrontier"),
            ("camp", ["-PFVisualCapture", "-PFVisualWarmup=15"], "/Game/ProjectF/Maps/L_MountainFrontier"),
            ("radio", ["-PFVisualCapture", "-PFVisualWarmup=15", "-PFReviewCamera=-16600,-3000,2800,-2,55"], "/Game/ProjectF/Maps/L_MountainFrontier")]
if args.execution:
    captures += [("rear-body", ["-PFExecutionCapture"], "/Game/ProjectF/Maps/Tests/L_RearExecutionReview"),
                 ("rear-fp", ["-PFExecutionCapture", "-PFExecutionFirstPerson"], "/Game/ProjectF/Maps/Tests/L_RearExecutionReview")]
for name, flags, map_path in captures:
    # The engine sanitizes saved-directory suffixes; supply an already safe value.
    suffix = re.sub(r"[^A-Za-z0-9_]", "", "PF_" + args.label + "_" + name)
    log = output / (name + ".log")
    run(name, [str(game), map_path, "-game", "-RenderOffscreen", "-windowed", "-ResX=1920", "-ResY=1080", "-ForceRes",
        "-unattended", "-nosplash", "-nosound", "-saveddirsuffix=" + suffix, "-abslog=" + str(log)] + flags, 180)
    text = log.read_text(encoding="utf-8", errors="replace")
    if name == "title": assert "PF_NET_RESULT passed=1" in text
    if name.startswith("rear-"): assert "PF_EXECUTION_REVIEW passed=1" in text
    saved = game.parents[2] / ("Saved_" + suffix) / "Screenshots"
    shots = sorted(str(p) for p in saved.glob("*.png"))
    assert shots, "Rendered capture is missing"
    steps[-1]["screenshots"] = shots
    (output / "execution.json").write_text(json.dumps({"binary_sha256": binary_hash, "steps": steps}, indent=2), encoding="utf-8")
    assert "Warning: Material " not in text and "Failed to compile Material" not in text, "Material fallback requires review"

shell = shutil.which("pwsh")
assert shell, "PowerShell 7 is required for the existing local network harness"
matrix_name = "network-matrix-" + args.label + ".json"
run("network", [shell, "-NoProfile", "-File", str(root / "Tools/RunNetworkMatrix.ps1"), "-FullMatrix", "-ResultName", matrix_name], 1200)
matrix = json.loads((evidence / matrix_name).read_text(encoding="utf-8-sig"))
assert len(matrix) == 9 and all(case["passed"] for case in matrix)
for name, flags in (("profile-camp", []), ("profile-dense", ["--players", "4"]), ("profile-split", ["--split"])):
    run(name, [sys.executable, str(root / "Tools/profile_frontier.py")] + flags, 280)
record = {"binary_sha256": binary_hash, "steps": steps, "matrix": str(evidence / matrix_name), "release_approved": False}
(output / "execution.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
print(json.dumps({"output": str(output), "steps_completed": len(steps), "release_approved": False}))
