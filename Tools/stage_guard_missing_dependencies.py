"""Repair an explicitly observed missing local sample dependency, without replacements."""
from pathlib import Path
import hashlib
import json
import re
import shutil

root = Path(__file__).resolve().parents[1]
source_root = Path("D:/EpicGamesCache/VaultCache/GameAnimationSample_5.7/data/Content")
pending = ["/Game/Characters/Paragon/Global/Hair/Textures/T_Caustics_2"]
seen, records, unresolved_strings = set(), [], []
while pending:
    package = pending.pop()
    if package in seen:
        continue
    seen.add(package)
    relative = Path(package.removeprefix("/Game/") + ".uasset")
    source, destination = source_root / relative, root / "Content" / relative
    if not source.is_file():
        unresolved_strings.append(package)
        continue
    data = source.read_bytes()
    if destination.exists() and destination.read_bytes() != data:
        continue
    pending.extend(x.decode("ascii") for x in re.findall(rb"/Game/[A-Za-z0-9_/\-]+", data))
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    for suffix in (".uexp", ".ubulk", ".uptnl"):
        if source.with_suffix(suffix).exists():
            shutil.copy2(source.with_suffix(suffix), destination.with_suffix(suffix))
    records.append({"source": str(source), "package": package,
                    "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
(root / "ArtSource/Characters/Guard/additional-dependencies.json").write_text(
    json.dumps({"files": records, "unresolved_package_strings": unresolved_strings,
                "network_used": False, "purchases": False}, indent=2), encoding="utf-8")
print(json.dumps({"copied": len(records)}))
