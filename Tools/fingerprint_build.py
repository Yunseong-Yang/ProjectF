"""Identify the exact local development binary, package tables, source and derived content."""
from pathlib import Path
import argparse
import hashlib
import json
import re

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--label", required=True)
parser.add_argument("--package-directory", default="ProjectF_Development")
parser.add_argument("--evidence-folder", default="20261030_Continuation")
args = parser.parse_args()
if not re.fullmatch(r"[A-Za-z0-9_-]+", args.label): raise ValueError("Invalid build label")
if not re.fullmatch(r"ProjectF_[A-Za-z0-9_]+", args.package_directory): raise ValueError("Invalid package directory")
if not re.fullmatch(r"[A-Za-z0-9_]+", args.evidence_folder): raise ValueError("Invalid evidence folder")

def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""): value.update(chunk)
    return {"file": path.relative_to(root).as_posix(), "bytes": path.stat().st_size, "sha256": value.hexdigest()}

binary = root / "Builds" / args.package_directory / "Windows/ProjectF/Binaries/Win64/ProjectF.exe"
assert binary.is_file()
code = [p for prefix in ("Source", "Config") for p in (root / prefix).rglob("*") if p.is_file()]
code += [root / "ProjectF.uproject"]
content = [p for p in (root / "Content/ProjectF").rglob("*") if p.suffix in (".uasset", ".umap")]
record = {"label": args.label, "binary": digest(binary),
          "protocol": int(re.search(r"ProtocolVersion\s*=\s*(\d+)", (root / "Source/ProjectF/Public/Core/PFTypes.h").read_text()).group(1)),
          "package_tables": [digest(p) for p in sorted((binary.parents[2] / "Content/Paks").glob("*.utoc"))],
          "code": [digest(p) for p in sorted(code)], "derived_project_content": [digest(p) for p in sorted(content)],
          "release_approved": False}
destination = root / "Saved/Implementation" / args.evidence_folder / (args.label + "-fingerprint.json")
assert not destination.exists(), "Keep existing build fingerprints immutable; choose a new label"
destination.write_text(json.dumps(record, indent=2), encoding="utf-8")
print(json.dumps({"output": str(destination), "protocol": record["protocol"], "binary_sha256": record["binary"]["sha256"], "content_files": len(content)}))
