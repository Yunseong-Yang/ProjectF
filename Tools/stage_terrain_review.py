"""Stage only missing locally-owned terrain dependencies; preserve existing derivatives."""
from pathlib import Path
import hashlib
import json
import re
import shutil

root = Path(__file__).resolve().parents[1]
source_root = Path("D:/EpicGamesCache/VaultCache/Slay_5.7/data/Content")
seeds = ["/Game/Slay/Envs/Temple/Sublevels/Terrain", "/Game/Slay/Envs/Temple/Sublevels/Terrain_TF0010"]
pending, seen, files, missing = list(seeds), set(), [], []
while pending:
    package = pending.pop()
    if package in seen: continue
    seen.add(package)
    relative = Path(package.removeprefix("/Game/"))
    source = source_root / relative.with_suffix(".uasset")
    if not source.exists(): source = source_root / relative.with_suffix(".umap")
    if not source.is_file(): missing.append(package); continue
    data = source.read_bytes(); destination = root / "Content" / source.relative_to(source_root)
    pending.extend(x.decode("ascii") for x in re.findall(rb"/Game/[A-Za-z0-9_/\-]+", data))
    if destination.exists(): continue
    files.append((source, destination, package, hashlib.sha256(data).hexdigest()))
directory = root / "ArtSource/Environment/TerrainReview"; directory.mkdir(parents=True, exist_ok=True)
record = {"permission_record": "ArtSource/Environment/source-manifest.json", "network_used": False, "purchases": False,
          "copied": [{"package": p, "source": str(s), "sha256": h, "bytes": s.stat().st_size} for s, _, p, h in files],
          "unresolved_package_strings": missing, "existing_assets_replaced": False, "production_approved": False,
          "purpose": "Separate review map only; no game default or release map change"}
(directory / "source-manifest.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
for source, destination, *_ in files:
    destination.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, destination)
    for suffix in (".uexp", ".ubulk", ".uptnl"):
        if source.with_suffix(suffix).exists() and not destination.with_suffix(suffix).exists(): shutil.copy2(source.with_suffix(suffix), destination.with_suffix(suffix))
print(json.dumps({"copied": len(files), "bytes": sum(s.stat().st_size for s, *_ in files), "unresolved": len(missing)}))
