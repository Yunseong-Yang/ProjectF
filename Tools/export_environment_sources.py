"""Executed by the local Unreal Editor after full initialization."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource" / "Environment"
manifest = json.loads((directory / "source-manifest.json").read_text(encoding="utf-8"))
output = directory / "OriginalExports"
output.mkdir(parents=True, exist_ok=True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Slay", "/Game/ExampleContent"], True)
meshes = []
for path in manifest["seeds"]:
    asset = unreal.load_asset(path)
    assert asset is not None, path
    if not isinstance(asset, unreal.StaticMesh):
        continue
    destination = output / (asset.get_name() + ".fbx")
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(destination)
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = unreal.StaticMeshExporterFBX()
    options = unreal.FbxExportOption()
    options.collision = False
    options.level_of_detail = False
    options.ascii = False
    task.options = options
    assert unreal.Exporter.run_asset_export_task(task), path
    bounds = asset.get_bounding_box()
    record = {"source": path, "name": asset.get_name(), "fbx": str(destination),
              "triangles": asset.get_num_triangles(0), "material_paths": [m.material_interface.get_path_name() if m.material_interface else "" for m in asset.static_materials],
              "bounds_min": [bounds.min.x, bounds.min.y, bounds.min.z], "bounds_max": [bounds.max.x, bounds.max.y, bounds.max.z]}
    meshes.append(record)
    unreal.log("PF_ENV_EXPORT " + json.dumps(record))
(directory / "unreal-source-meshes.json").write_text(json.dumps(meshes, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
