"""Export raw action motion locally; no Lyra gameplay notifies are carried in FBX."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve().parents[1]
directory = root / "ArtSource/Animations/RearExecution"
assert directory.is_dir(), directory
records = []
for weapon in ("Rifle", "Pistol", "Shotgun"):
    path = "/Game/Characters/Heroes/Mannequin/Animations/Actions/MM_" + weapon + "_Melee"
    animation = unreal.load_asset(path); assert animation, path
    filename = directory / (weapon + "_Source.fbx")
    task = unreal.AssetExportTask(); task.object = animation; task.filename = str(filename)
    task.automated = True; task.prompt = False; task.replace_identical = True; task.exporter = unreal.AnimSequenceExporterFBX()
    task.options = unreal.FbxExportOption()
    assert unreal.Exporter.run_asset_export_task(task), path
    records.append({"animation": path, "seconds": animation.get_play_length(), "fbx": str(filename), "bytes": filename.stat().st_size})
(directory / "export-validation.json").write_text(json.dumps({"motions": records, "production_approved": False}, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
