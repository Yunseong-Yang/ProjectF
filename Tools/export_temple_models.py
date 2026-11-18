"""Export used temple geometry to a separate local Blender validation pipeline."""
from pathlib import Path
import hashlib
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/Temple"
exports = directory / "OriginalExports"
exports.mkdir(exist_ok=True)
audit = json.loads((directory / "source-audit.json").read_text(encoding="utf-8"))
tools = unreal.AssetToolsHelpers.get_asset_tools()
editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
records = []
for row in audit["models"]:
    mesh = unreal.load_asset(row["source"])
    assert mesh, row["source"]
    name = mesh.get_name() + "_" + hashlib.sha256(row["source"].encode()).hexdigest()[:6]
    lower = name.lower()
    budget = 30000 if any(w in lower for w in ("tree", "elm", "maple", "mountain")) else 8000 if any(w in lower for w in ("bush", "grass", "fern", "ivy")) else 60000 if "/Arch/" in row["source"] else 12000
    chosen = next((i for i, count in enumerate(row["lod_triangles"]) if count <= budget), 0)
    output_mesh = mesh
    if chosen:
        destination = "/Game/ProjectF/Environment/Temple/SourceLOD"
        output_mesh = unreal.load_asset(destination + "/" + name)
        if not output_mesh:
            output_mesh = tools.duplicate_asset(name, destination, mesh)
        assert output_mesh
        assert editor.set_lod_from_static_mesh(output_mesh, 0, mesh, chosen, True) == 0
        assert editor.remove_lods(output_mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(output_mesh)
    filename = exports / (name + ".fbx")
    task = unreal.AssetExportTask()
    task.object, task.filename = output_mesh, str(filename)
    task.automated, task.prompt, task.replace_identical = True, False, False
    task.exporter = unreal.StaticMeshExporterFBX()
    options = unreal.FbxExportOption()
    options.collision, options.level_of_detail = False, False
    task.options = options
    if not filename.exists():
        assert unreal.Exporter.run_asset_export_task(task), name
    package = row["source"].split(".")[0]
    if package.startswith("/Game/"):
        source_file = root / "Content" / (package.removeprefix("/Game/") + ".uasset")
    elif package.startswith("/Engine/"):
        source_file = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.engine_content_dir())) / (package.removeprefix("/Engine/") + ".uasset")
    else:
        raise ValueError("Unsupported source mount: " + package)
    records.append({**row, "name": name, "source_lod": chosen, "budget": budget,
        "fbx": str(filename), "fbx_sha256": hashlib.sha256(filename.read_bytes()).hexdigest(),
        "source_file": str(source_file), "source_file_sha256": hashlib.sha256(source_file.read_bytes()).hexdigest()})
    (directory / "exported-models.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
    unreal.log("PF_TEMPLE_EXPORT " + str(len(records)) + "/" + str(len(audit["models"])))
unreal.log("PF_TEMPLE_EXPORT_COMPLETE models=" + str(len(records)))
unreal.SystemLibrary.quit_editor()
