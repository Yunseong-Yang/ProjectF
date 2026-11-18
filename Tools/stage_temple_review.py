"""Complete the local environment review dependencies without replacing game assets."""
from pathlib import Path
import hashlib
import json
import re
import shutil

root = Path(__file__).resolve().parents[1]
cache = Path("D:/EpicGamesCache/VaultCache/Slay_5.7/data/Content")
pending = ["/Game/Slay/Envs/Temple/Sublevels/Temple_Geo", "/Game/Slay/Envs/Temple/Sublevels/Temple_Lighting"]
seen, records, missing = set(), [], []
while pending:
    package = pending.pop()
    if package in seen: continue
    seen.add(package)
    relative = Path(package.removeprefix("/Game/")); source = cache / relative.with_suffix(".uasset")
    if not source.is_file(): source = cache / relative.with_suffix(".umap")
    if not source.is_file(): missing.append(package); continue
    data = source.read_bytes(); target = root / "Content" / source.relative_to(cache)
    pending.extend(x.decode("ascii") for x in re.findall(rb"/Game/[A-Za-z0-9_/\-]+", data))
    if target.exists(): continue
    target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, target)
    records.append({"source": str(source), "package": package, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
    for suffix in (".uexp", ".ubulk", ".uptnl"):
        if source.with_suffix(suffix).exists() and not target.with_suffix(suffix).exists(): shutil.copy2(source.with_suffix(suffix), target.with_suffix(suffix))
(root / "ArtSource/Environment/TerrainReview/additional-scene-source.json").write_text(json.dumps({"files": records,
    "unresolved_package_strings": missing, "existing_assets_replaced": False, "network_used": False, "purchases": False,
    "permission_record": "ArtSource/Environment/source-manifest.json", "production_approved": False}, indent=2), encoding="utf-8")
print(json.dumps({"copied": len(records), "bytes": sum(r["bytes"] for r in records), "unresolved": len(missing)}))
