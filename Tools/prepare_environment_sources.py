"""Copy an explicitly selected, already downloaded sample dependency closure.

Original package paths and source files are preserved. No downloads or inference.
The Unreal loading/export pass validates the dependencies; string discovery alone
is deliberately not treated as asset validation or release approval.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCES = {
    "Slay": Path("D:/EpicGamesCache/VaultCache/Slay_5.7/data/Content"),
    "ExampleContent": Path("D:/EpicGamesCache/VaultCache/ContentExamples_5.7/data/Content"),
}
SEEDS = [
	"/Game/Slay/Assets/Trees/AmurCork/MI_TreeBark02_1",
	"/Game/Slay/Assets/Arch/SWDorm/Textures/T_WornWallPlaster_BC",
	"/Game/Slay/Assets/Arch/SWDorm/Textures/T_WornWallPlaster_N",
	"/Game/Slay/Assets/Arch/SWDorm/Textures/T_RoughConcrete_BC",
	"/Game/Slay/Assets/Arch/SWDorm/Textures/T_RoughConcrete_N",
    "/Game/Slay/Assets/Trees/AmurCork/SM_Elm08",
    "/Game/Slay/Assets/Vegetation/JapaneseMaple/SM_JapaneseMaple",
    "/Game/Slay/Assets/Vegetation/BostonFern/SM_BostonFern_A",
    "/Game/Slay/Assets/Vegetation/WildGrass/SM_WildGrass_A",
    "/Game/Slay/Assets/Vegetation/DryMeadowGrass/SM_DryMeadowGrass_A",
    "/Game/Slay/Assets/SetDec/WoodenCrate/SM_WoodenCrate",
    "/Game/Slay/Assets/SetDec/WoodenBarrel/SM_WoodenBarrel",
    "/Game/Slay/Assets/GraniteCliff/SM_GraniteCliff",
    "/Game/Slay/Assets/Terrain/PeakMoutains_A/SM_BG_PeakMountains_A",
    "/Game/Slay/Assets/MasterMaterials/MainLandscape/T_DustyGround_BC",
    "/Game/Slay/Assets/MasterMaterials/MainLandscape/T_DustyGround_N",
    "/Game/ExampleContent/Landscapes/Meshes/SM_Rock",
    "/Game/ExampleContent/Landscapes/Textures/T_GroundGrass_D",
    "/Game/ExampleContent/Landscapes/Textures/T_GroundGrass_N",
    "/Game/ExampleContent/Landscapes/Textures/T_Granite_Rock_visscjf_4K_D",
    "/Game/ExampleContent/Landscapes/Textures/T_Granite_Rock_visscjf_4K_N",
]
TERMS = {
    "Slay": {"listing": "https://www.fab.com/listings/ef920974-4e1d-4ca9-afe2-277f5adcfe53",
             "terms": "UE-Only Content; use in Unreal Engine products only",
             "official_permission": "https://www.unrealengine.com/spotlights/mold3d-studio-to-share-slay-animated-content-sample-project-with-unreal-engine-community",
             "local_acquisition_record": "Docs/Fab_Cache_Inventory.json"},
    "ExampleContent": {"official_permission": "https://dev.epicgames.com/documentation/en-us/unreal-engine/content-examples-sample-project-for-unreal-engine",
                       "terms": "Epic Content Examples; own Unreal levels permitted",
                       "local_acquisition_record": "Docs/Fab_Cache_Inventory.json"},
}


def main():
    queue, visited, records, missing = list(SEEDS), set(), [], set()
    while queue:
        package = queue.pop()
        if package in visited:
            continue
        visited.add(package)
        relative = Path(package.removeprefix("/Game/") + ".uasset")
        source_root = SOURCES.get(relative.parts[0])
        if source_root is None:
            missing.add(package)
            continue
        source = source_root / relative
        if not source.is_file():
            missing.add(package)
            continue
        data = source.read_bytes()
        for dependency in re.findall(rb"/Game/[A-Za-z0-9_/\-]+", data):
            queue.append(dependency.decode("ascii"))
        destination = ROOT / "Content" / relative
        if destination.exists() and destination.read_bytes() != data:
            raise RuntimeError(f"Refusing to replace existing project asset: {destination}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        for extension in (".uexp", ".ubulk", ".uptnl"):
            companion = source.with_suffix(extension)
            if companion.exists():
                shutil.copy2(companion, destination.with_suffix(extension))
        records.append({"package": package, "source": str(source), "sha256": hashlib.sha256(data).hexdigest(),
                        "bytes": len(data), "source_preserved": True})
    output = ROOT / "ArtSource" / "Environment"
    output.mkdir(parents=True, exist_ok=True)
    record = {"network_used": False, "purchases": False, "terms": TERMS, "seeds": SEEDS,
              "copied": records, "unresolved_package_strings": sorted(missing),
              "release_approved": False, "next": "Unreal load/export; Blender cleanup; derived import, visual and density validation"}
    (output / "source-manifest.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
    print(json.dumps({"files": len(records), "bytes": sum(r["bytes"] for r in records), "unresolved_strings": len(missing)}))


if __name__ == "__main__":
    main()
