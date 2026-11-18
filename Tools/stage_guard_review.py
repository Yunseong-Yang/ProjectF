"""Copy the validated workbench candidate closure to a game-only review namespace.

Existing game assets are never replaced. This does not switch the live guard BP.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "Saved/CharacterWorkbench/Content"
pending = ["/Game/ProjectF/Characters/Guard/SKM_Guard_Rooted",
           "/Game/ProjectF/Characters/Guard/Animations/Guard_MF_Rifle_Idle_ADS",
           "/Game/ProjectF/Characters/Guard/Animations/Guard_MF_Rifle_Walk_Fwd",
           "/Game/ProjectF/Characters/Guard/Animations/Guard_MF_Rifle_Jog_Fwd",
           "/Game/ProjectF/Characters/Guard/Animations/Guard_MM_Rifle_Fire",
           "/Game/ProjectF/Characters/Guard/Animations/Guard_MM_Rifle_Reload",
           "/Game/ProjectF/Characters/Guard/Animations/Guard_MM_Death_Back_01"]
seen, files, missing = set(), [], []
while pending:
    package = pending.pop()
    if package in seen: continue
    seen.add(package)
    relative = Path(package.removeprefix("/Game/") + ".uasset")
    source, destination = SOURCE / relative, ROOT / "Content" / relative
    if not source.is_file(): missing.append(package); continue
    data = source.read_bytes()
    pending.extend(x.decode("ascii") for x in re.findall(rb"/Game/[A-Za-z0-9_/\-]+", data))
    if destination.exists():
        if destination.read_bytes() != data: raise RuntimeError(f"Refusing to replace existing game asset: {relative}")
        continue
    files.append((source, destination, package, hashlib.sha256(data).hexdigest()))
for source, destination, *_ in files:
    destination.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, destination)
    for extension in (".uexp", ".ubulk", ".uptnl"):
        if source.with_suffix(extension).exists(): shutil.copy2(source.with_suffix(extension), destination.with_suffix(extension))
(ROOT / "ArtSource/Characters/Guard/review-stage.json").write_text(json.dumps({
    "copied": [{"package": p, "sha256": h} for _, _, p, h in files], "unresolved_package_strings": missing,
    "guard_blueprint_changed": False, "production_approved": False,
}, indent=2), encoding="utf-8")
print(json.dumps({"staged": len(files), "missing": len(missing)}))
