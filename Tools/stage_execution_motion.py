"""Stage only reviewed motion files; never replace a project skeleton or source asset."""
from pathlib import Path
import hashlib
import json
import shutil

root = Path(__file__).resolve().parents[1]
source = root / "Saved/CharacterWorkbench/Content"
packages = ["ProjectF/Player/Animations/AS_" + w + "_RearStrike" for w in ("Rifle", "Pistol", "Shotgun")]
packages.append("ProjectF/Characters/Guard/Animations/Guard_MM_Death_Front_01")
records = []
for package in packages:
    relative = Path(package + ".uasset")
    original, destination = source / relative, root / "Content" / relative
    assert original.is_file()
    if destination.exists(): assert destination.read_bytes() == original.read_bytes(), destination
    destination.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(original, destination)
    records.append({"asset": "/Game/" + package, "sha256": hashlib.sha256(original.read_bytes()).hexdigest()})
(root / "ArtSource/Animations/RearExecution/staged-motion.json").write_text(json.dumps({"assets": records, "skeletons_replaced": False,
    "production_approved": False, "runtime_connected": False}, indent=2), encoding="utf-8")
print(json.dumps({"staged": len(records), "runtime_connected": False}))
