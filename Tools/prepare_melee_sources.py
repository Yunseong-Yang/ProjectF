"""Stage three already-downloaded Epic action clips in the isolated workbench."""
from pathlib import Path
import hashlib
import json
import re
import shutil

root = Path(__file__).resolve().parents[1]
source_root = Path("D:/EpicGamesCache/VaultCache/Lyra_5.8/data/Content")
workbench = root / "Saved/CharacterWorkbench/Content"
seeds = ["/Game/Characters/Heroes/Mannequin/Animations/Actions/MM_" + weapon + "_Melee" for weapon in ("Rifle", "Pistol", "Shotgun")]
pending, seen, files, missing = list(seeds), set(), [], []
while pending:
    package = pending.pop()
    if package in seen: continue
    seen.add(package)
    relative = Path(package.removeprefix("/Game/") + ".uasset")
    source, target = source_root / relative, workbench / relative
    if not source.is_file(): missing.append(package); continue
    data = source.read_bytes()
    if target.exists() and target.read_bytes() != data: raise RuntimeError(f"Preserve existing workbench asset: {relative}")
    files.append((source, target, package, hashlib.sha256(data).hexdigest()))
    pending.extend(x.decode("ascii") for x in re.findall(rb"/Game/[A-Za-z0-9_/\-]+", data))
assert not any(seed in missing for seed in seeds)
for source, target, *_ in files:
    target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, target)
    for suffix in (".uexp", ".ubulk", ".uptnl"):
        if source.with_suffix(suffix).exists(): shutil.copy2(source.with_suffix(suffix), target.with_suffix(suffix))
directory = root / "ArtSource/Animations/RearExecution"
directory.mkdir(parents=True, exist_ok=True)
(directory / "source-manifest.json").write_text(json.dumps({
    "permission": "https://dev.epicgames.com/documentation/unreal-engine/sample-game-projects-for-unreal-engine?lang=en-US",
    "sample": "https://dev.epicgames.com/documentation/unreal-engine/lyra-sample-game-in-unreal-engine?lang=en-US",
    "source": str(source_root), "current_account_entitlement_checked": False, "public_sample_permission_checked": True,
    "network_downloads": False, "purchases": False, "additional_inference": False,
    "files": [{"source": str(s), "package": p, "sha256": h} for s, _, p, h in files],
    "unresolved_package_strings": missing, "production_approved": False,
    "purpose": "Workbench source clips; not a finished paired takedown or release approval"
}, indent=2), encoding="utf-8")
print(json.dumps({"staged": len(files), "unresolved": len(missing)}))
