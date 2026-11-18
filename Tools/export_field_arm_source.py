"""Export an already-cached normal-clothing candidate from the isolated local workbench."""
from pathlib import Path
import hashlib
import json
import unreal

root = Path(__file__).resolve().parents[1]
assert Path(unreal.Paths.project_dir()).resolve() == root / "Saved/CharacterWorkbench"
out = root / "ArtSource/Characters/FirstPersonField"
out.mkdir(parents=True, exist_ok=True)
mesh = unreal.load_asset("/Game/Characters/Echo/Meshes/SKM_Echo")
assert isinstance(mesh, unreal.SkeletalMesh)
file = out / "FieldBody_Original.fbx"
task = unreal.AssetExportTask()
task.object = mesh
task.filename = str(file)
task.automated = True
task.prompt = False
task.replace_identical = False
task.exporter = unreal.SkeletalMeshExporterFBX()
options = unreal.FbxExportOption()
options.collision = False
options.level_of_detail = False
options.ascii = False
task.options = options
if not file.exists():
    assert unreal.Exporter.run_asset_export_task(task)
materials = []
for slot in mesh.get_editor_property("materials"):
    material = slot.material_interface
    row = {"slot": str(slot.get_editor_property("imported_material_slot_name")), "material": material.get_path_name() if material else None, "textures": {}}
    if material:
        row["used_textures"] = [texture.get_path_name() for texture in unreal.MaterialEditingLibrary.get_material_used_textures(material)]
        for name in unreal.MaterialEditingLibrary.get_texture_parameter_names(material):
            texture = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material, name)
            if texture:
                row["textures"][str(name)] = texture.get_path_name()
    materials.append(row)
source_file = root / "Saved/CharacterWorkbench/Content/Characters/Echo/Meshes/SKM_Echo.uasset"
record = {"source_asset": mesh.get_path_name(), "source_file": str(source_file), "source_sha256": hashlib.sha256(source_file.read_bytes()).hexdigest(),
          "fbx": str(file), "fbx_sha256": hashlib.sha256(file.read_bytes()).hexdigest(), "materials": materials,
          "origin": "Existing local GameAnimationSample cache; no download or external inference", "production_approved": False}
(out / "source.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_FIELD_ARM_SOURCE_EXPORTED")
unreal.SystemLibrary.quit_editor()
