"""Stage an already downloaded Epic sample character and its local dependencies."""
from pathlib import Path
import hashlib
import json
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path("D:/EpicGamesCache/VaultCache/GameAnimationSample_5.7/data/Content")
SEEDS = ["/Game/Characters/Paragon/Heroes/TwinBlast/Meshes/SKM_TwinBlast_ActionHero",
         "/Game/Characters/Paragon/Heroes/TwinBlast/Rigs/RTG_UEFN_to_TwinBlast",
         "/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget"]


def main():
    workbench = ROOT / "Saved" / "CharacterWorkbench"
    workbench.mkdir(parents=True, exist_ok=True)
    descriptor = {"FileVersion": 3, "EngineAssociation": "5.8", "Category": "Local asset validation",
                  "Plugins": [{"Name": name, "Enabled": True} for name in ("PythonScriptPlugin", "EditorScriptingUtilities", "IKRig")]}
    (workbench / "CharacterWorkbench.uproject").write_text(json.dumps(descriptor, indent=2), encoding="utf-8")
    pending, seen, files, missing = list(SEEDS), set(), [], []
    while pending:
        package = pending.pop()
        if package in seen: continue
        seen.add(package)
        relative = Path(package.removeprefix("/Game/") + ".uasset")
        source = SOURCE / relative
        if not source.is_file(): missing.append(package); continue
        data = source.read_bytes()
        pending.extend(x.decode("ascii") for x in re.findall(rb"/Game/[A-Za-z0-9_/\-]+", data))
        destination = workbench / "Content" / relative
        if destination.exists() and destination.read_bytes() != data:
            raise RuntimeError(f"Existing project asset would be replaced: {relative}")
        files.append((source, destination, package, hashlib.sha256(data).hexdigest(), len(data)))
    # Validate the whole closure before the first copy; never replace user assets.
    for source, destination, *_ in files:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        for suffix in (".uexp", ".ubulk", ".uptnl"):
            if source.with_suffix(suffix).exists(): shutil.copy2(source.with_suffix(suffix), destination.with_suffix(suffix))
    directory = ROOT / "ArtSource" / "Characters" / "Guard"
    directory.mkdir(parents=True, exist_ok=True)
    record = {"source_project": str(SOURCE.parent), "staging_project": str(workbench / "CharacterWorkbench.uproject"), "seeds": SEEDS, "network_used": False,
              "downloads_or_purchases": False, "additional_inference": False,
              "permission_basis": ["https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine",
                                   "https://www.fab.com/listings/9fa88852-5711-42e1-94fa-2491498a64da"],
              "restrictions": "Use within Unreal Engine products; do not use the original game's or character's names to market this game.",
              "current_account_entitlement_checked": False, "public_sample_permission_checked": True,
              "files": [{"source": str(s), "package": p, "sha256": h, "bytes": b} for s, _, p, h, b in files],
              "unresolved_package_strings": missing, "production_approved": False,
              "next": "Load validation, source export, Blender deformation-aware cleanup, retarget and contact validation"}
    (directory / "source-manifest.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    print(json.dumps({"copied": len(files), "bytes": sum(f[4] for f in files), "unresolved": len(missing)}))


if __name__ == "__main__": main()
