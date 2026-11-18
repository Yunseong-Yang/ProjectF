"""Reject release packaging when required approval evidence is missing or stale."""
from pathlib import Path
import hashlib
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
manifest = ROOT / "Docs" / "AssetApprovals.json"
failures = []
if not manifest.exists():
    failures.append("Missing asset approval manifest: Docs/AssetApprovals.json")
else:
    record = json.loads(manifest.read_text(encoding="utf-8-sig"))
    if record.get("schema_version") != 1:
        failures.append("Unsupported approval manifest version")
    maps = record.get("release_maps", [])
    if not maps:
        failures.append("No approved release maps")
    assets = record.get("assets", [])
    if not assets:
        failures.append("No approved model records")
    for item in assets:
        path = (ROOT / item.get("file", "")).resolve()
        if not path.is_relative_to(ROOT) or not path.is_file():
            failures.append(f"Missing or out-of-project asset: {item.get('file')}")
            continue
        if item.get("status") != "approved":
            failures.append(f"Asset not approved: {item.get('file')}")
        if hashlib.sha256(path.read_bytes()).hexdigest() != item.get("sha256"):
            failures.append(f"Asset changed since approval: {item.get('file')}")
        for field in ("source", "license", "geometry", "materials", "collision", "lod", "blender_review", "visual_review", "density_performance"):
            if not item.get(field):
                failures.append(f"Missing {field}: {item.get('file')}")
    for item in maps:
        if "/Tests/" in item.get("package", "") or item.get("status") != "approved":
            failures.append(f"Unapproved or test map: {item.get('package')}")
        for field in ("dependency_audit", "playthrough", "four_player_dense", "two_plus_two", "memory", "internet_coop"):
            if not item.get(field):
                failures.append(f"Missing map acceptance evidence {field}: {item.get('package')}")
result = {"release_allowed": not failures, "failures": failures}
print(json.dumps(result, ensure_ascii=False, indent=2))
sys.exit(0 if not failures else 1)
