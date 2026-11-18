"""Export used review geometry with original artist LOD selection, without source replacement."""
from pathlib import Path
import hashlib
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/TerrainReview"
exports = directory / "OriginalExports"; exports.mkdir(exist_ok=True)
audit = json.loads((directory / "model-audit.json").read_text(encoding="utf-8"))
tools = unreal.AssetToolsHelpers.get_asset_tools()
subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
records = []
for row in audit["models"]:
    if row["placements"] <= 0: continue
    mesh = unreal.load_asset(row["mesh"]); assert mesh
    lower = mesh.get_name().lower()
    budget = 30000 if "elm" in lower or "tree" in lower else 8000 if "bush" in lower else 30000 if "mountain" in lower else 12000 if "cliff" in lower or "rock" in lower else 3000 if "incense" in lower or "grave" in lower else 12000
    lods = row["lod_triangles"]
    chosen = next((index for index, count in enumerate(lods) if count <= budget), 0)
    name = mesh.get_name() + "_" + hashlib.sha256(row["mesh"].encode()).hexdigest()[:6]
    output_mesh = mesh
    if chosen:
        output_mesh = unreal.load_asset("/Game/ProjectF/Environment/TerrainReview/SourceLOD/" + name)
        if not output_mesh: output_mesh = tools.duplicate_asset(name, "/Game/ProjectF/Environment/TerrainReview/SourceLOD", mesh)
        assert output_mesh
        assert subsystem.set_lod_from_static_mesh(output_mesh, 0, mesh, chosen, True) == 0
        assert subsystem.remove_lods(output_mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(output_mesh)
    filename = exports / (name + ".fbx")
    task = unreal.AssetExportTask(); task.object = output_mesh; task.filename = str(filename)
    task.automated = True; task.prompt = False; task.replace_identical = True; task.exporter = unreal.StaticMeshExporterFBX()
    options = unreal.FbxExportOption(); options.collision = False; options.level_of_detail = False; task.options = options
    assert unreal.Exporter.run_asset_export_task(task)
    records.append({"name": name, "source": mesh.get_path_name(), "source_lod": chosen, "original_triangles": lods[0],
        "exported_triangles": output_mesh.get_num_triangles(0), "budget_start": budget, "placements": row["placements"],
        "material_paths": [slot.material_interface.get_path_name() if slot.material_interface else None for slot in output_mesh.static_materials],
        "fbx": str(filename), "fbx_sha256": hashlib.sha256(filename.read_bytes()).hexdigest(), "production_approved": False})
(directory / "exported-models.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
unreal.log("PF_TERRAIN_REVIEW_EXPORTED models=" + str(len(records)))
unreal.SystemLibrary.quit_editor()
