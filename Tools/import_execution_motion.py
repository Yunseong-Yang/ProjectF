"""Import clean FBX motion in the workbench against its copy of the project skeleton."""
from pathlib import Path
import json
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
root = project.parents[1]
directory = root / "ArtSource/Animations/RearExecution"
mesh = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"); assert mesh
skeleton = mesh.get_editor_property("skeleton")
records = []
for weapon in ("Rifle", "Pistol", "Shotgun"):
    task = unreal.AssetImportTask(); task.filename = str(directory / (weapon + "_Source.fbx"))
    task.destination_path = "/Game/ProjectF/Player/Animations"; task.destination_name = "AS_" + weapon + "_RearStrike"
    task.automated = True; task.replace_existing = True; task.save = False
    options = unreal.FbxImportUI(); options.import_mesh = False; options.import_animations = True
    options.automated_import_should_detect_type = False; options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    options.skeleton = skeleton; options.import_materials = False; options.import_textures = False
    options.anim_sequence_import_data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    task.options = options; unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    path = task.destination_path + "/" + task.destination_name
    animation = unreal.load_asset(path); assert isinstance(animation, unreal.AnimSequence), (path, list(task.imported_object_paths))
    assert unreal.EditorAssetLibrary.save_loaded_asset(animation)
    records.append({"asset": path, "duration": animation.get_play_length(), "skeleton": skeleton.get_path_name()})
source_mesh = mesh
target_mesh = unreal.load_asset("/Game/ProjectF/Characters/Guard/SKM_Guard_Rooted")
retargeter = unreal.load_asset("/Game/ProjectF/Characters/Guard/RTG_ProjectManny_Guard")
inputs = unreal.IKRetargetBatchOperationInputs()
inputs.assets_to_retarget = [unreal.EditorAssetLibrary.find_asset_data("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01")]
inputs.source_mesh = source_mesh; inputs.target_mesh = target_mesh; inputs.ik_retarget_asset = retargeter
inputs.prefix = "Guard_"; inputs.target_path = "/Game/ProjectF/Characters/Guard/Animations"; inputs.overwrite_existing_files = True
results = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
assert len(results) == 1
victim = results[0].get_asset(); assert victim and unreal.EditorAssetLibrary.save_loaded_asset(victim)
(directory / "workbench-import.json").write_text(json.dumps({"strikes": records, "victim": victim.get_path_name(),
    "production_approved": False, "remaining": ["Blender root and pose inspection", "pair contact and camera review", "authority presentation integration"]}, indent=2), encoding="utf-8")
(directory / "pose-api.json").write_text(json.dumps({"methods": [x for x in dir(unreal.AnimPoseExtensions) if "pose" in x or "bone" in x],
    "get_anim_pose_at_time": unreal.AnimPoseExtensions.get_anim_pose_at_time.__doc__}, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
