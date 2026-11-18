"""Import an isolated, unapproved humanoid owner-view candidate without changing the player Blueprint."""
from pathlib import Path
import hashlib
import json
import re
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Characters/FirstPersonHuman"
record = json.loads((directory / "blender-validation.json").read_text(encoding="utf-8"))
target = json.loads((root / "ArtSource/Characters/FirstPerson/source.json").read_text(encoding="utf-8"))
assert hashlib.sha256(Path(record["export"]).read_bytes()).hexdigest() == record["export_sha256"]
player_file = root / "Content/ProjectF/Player/BP_PFPlayer.uasset"
skeleton_file = root / "Content" / (target["skeleton"].removeprefix("/Game/").split(".")[0] + ".uasset")
player_hash = hashlib.sha256(player_file.read_bytes()).hexdigest()
skeleton_hash = hashlib.sha256(skeleton_file.read_bytes()).hexdigest()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX 0")
skeleton = unreal.load_asset(target["skeleton"])
source = unreal.load_asset("/Game/ProjectF/Characters/Guard/SKM_Guard_Rooted")
by_name = {}
for slot in source.get_editor_property("materials"):
    material = slot.material_interface
    if material:
        for key in (str(slot.get_editor_property("imported_material_slot_name")), str(slot.get_editor_property("material_slot_name")), material.get_name()):
            by_name[key.lower()] = material
options = unreal.FbxImportUI()
options.import_mesh = True
options.import_as_skeletal = True
options.import_animations = False
options.automated_import_should_detect_type = False
options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
options.import_materials = False
options.import_textures = False
options.create_physics_asset = False
options.skeleton = skeleton
options.skeletal_mesh_import_data.set_editor_property("update_skeleton_reference_pose", False)
options.skeletal_mesh_import_data.set_editor_property("use_t0_as_ref_pose", False)
options.skeletal_mesh_import_data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
task = unreal.AssetImportTask()
task.filename = record["export"]
task.destination_path = "/Game/ProjectF/Player/Meshes"
task.destination_name = "SKM_PFHumanFirstPersonArms"
task.automated = True
task.replace_existing = True
task.save = False
task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh = unreal.load_asset("/Game/ProjectF/Player/Meshes/SKM_PFHumanFirstPersonArms")
assert isinstance(mesh, unreal.SkeletalMesh) and mesh.get_editor_property("skeleton") == skeleton
slots = list(mesh.get_editor_property("materials"))
expected_materials = {name.lower() for row in record["meshes"] for name in row["materials"]}
slots = [slot for slot in slots if str(slot.get_editor_property("imported_material_slot_name")).lower() in expected_materials]
for slot in slots:
    name = str(slot.get_editor_property("imported_material_slot_name")).lower()
    material = by_name.get(name) or by_name.get(re.sub(r"[_.][0-9]+$", "", name))
    assert material, (name, sorted(by_name))
    slot.material_interface = material
field_material = unreal.load_asset("/Game/ProjectF/Player/Materials/M_PFFieldGloves")
if field_material and len(slots) == 1:
    slots[0].material_interface = field_material
mesh.set_editor_property("materials", slots)
component = unreal.new_object(unreal.SkeletalMeshComponent)
component.set_skeletal_mesh_asset(mesh)
assert component.get_num_bones() == 89 and component.does_socket_exist("HandGrip_R")
expected = {row["name"]: row["parent"] for row in record["bones"]}
for index in range(component.get_num_bones()):
    name = str(component.get_bone_name(index))
    parent = str(component.get_parent_bone(name))
    assert name in expected and (parent if parent != "None" else None) == expected[name], (name, parent, expected.get(name))
assert len(slots) == len(expected_materials) and 1 <= len(slots) <= 4
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
assert hashlib.sha256(player_file.read_bytes()).hexdigest() == player_hash
assert hashlib.sha256(skeleton_file.read_bytes()).hexdigest() == skeleton_hash
output = {"mesh": mesh.get_path_name(), "skeleton": skeleton.get_path_name(), "bones": component.get_num_bones(),
          "socket": "HandGrip_R", "material_paths": [slot.material_interface.get_path_name() for slot in slots],
          "blender_triangles": sum(row["triangles"] for row in record["meshes"]),
          "source_skeleton_preserved": True, "player_blueprint_preserved": True, "default_applied": False,
          "production_approved": False, "required": ["Native owner-view grip and animation review", "Performance and co-op after any application"]}
(directory / "unreal-validation.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
unreal.log("PF_HUMAN_FIRST_PERSON_IMPORT_COMPLETE")
unreal.SystemLibrary.quit_editor()
