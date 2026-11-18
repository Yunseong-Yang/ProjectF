"""Isolated local workbench import and baked animation retarget; no game activation."""
from pathlib import Path
import json
import unreal

root = Path(__file__).resolve().parents[1]
directory = root / "ArtSource/Characters/Guard"
tools = unreal.AssetToolsHelpers.get_asset_tools()
source = unreal.load_asset("/Game/Characters/Paragon/Heroes/TwinBlast/Meshes/SKM_TwinBlast_ActionHero")
assert source
destination = "/Game/ProjectF/Characters/Guard"
task = unreal.AssetImportTask()
task.filename = str(directory / "SKM_Guard.fbx"); task.destination_path = destination; task.destination_name = "SKM_Guard_Rooted"
task.automated = True; task.replace_existing = True; task.save = False
options = unreal.FbxImportUI()
options.import_as_skeletal = True; options.import_mesh = True; options.import_animations = False
options.automated_import_should_detect_type = False; options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
options.import_materials = False; options.import_textures = False; options.create_physics_asset = False
options.skeleton = None
options.skeletal_mesh_import_data.set_editor_property("update_skeleton_reference_pose", False)
options.skeletal_mesh_import_data.set_editor_property("use_t0_as_ref_pose", False)
task.options = options
tools.import_asset_tasks([task])
mesh = unreal.load_asset(destination + "/SKM_Guard_Rooted")
assert isinstance(mesh, unreal.SkeletalMesh), task.imported_object_paths
original = json.loads((directory / "unreal-source.json").read_text(encoding="utf-8"))
by_name = {}
for path in original["materials"]:
    if path: by_name[path.split(".")[-1]] = path
slots = list(mesh.get_editor_property("materials"))
for slot in slots:
    name = str(slot.get_editor_property("imported_material_slot_name"))
    if name.endswith("_14"): name = name[:-3]
    path = by_name.get(name)
    if path: slot.material_interface = unreal.load_asset(path)
mesh.set_editor_property("materials", slots)
mesh.set_editor_property("physics_asset", source.get_editor_property("physics_asset"))
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
unreal.EditorAssetLibrary.save_loaded_asset(mesh.get_editor_property("skeleton"))
rig = unreal.load_asset(destination + "/IK_ProjectManny") or tools.duplicate_asset("IK_ProjectManny", destination, unreal.load_asset("/Game/Characters/UE5_Mannequins/Rigs/IK_UE5_Mannequin_Retarget"))
if rig is None: rig = unreal.load_asset(destination + "/IK_ProjectManny")
source_mesh = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple")
assert source_mesh
assert unreal.IKRigController.get_controller(rig).set_skeletal_mesh(source_mesh)
retargeter = unreal.load_asset(destination + "/RTG_ProjectManny_Guard") or tools.duplicate_asset("RTG_ProjectManny_Guard", destination, unreal.load_asset("/Game/Characters/Paragon/Heroes/TwinBlast/Rigs/RTG_UEFN_to_TwinBlast"))
if retargeter is None: retargeter = unreal.load_asset(destination + "/RTG_ProjectManny_Guard")
controller = unreal.IKRetargeterController.get_controller(retargeter)
controller.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, rig)
target_rig = unreal.load_asset(destination + "/IK_Guard") or tools.duplicate_asset("IK_Guard", destination, controller.get_ik_rig(unreal.RetargetSourceOrTarget.TARGET))
if target_rig is None: target_rig = unreal.load_asset(destination + "/IK_Guard")
assert unreal.IKRigController.get_controller(target_rig).set_skeletal_mesh(mesh)
controller.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
controller.set_preview_mesh(unreal.RetargetSourceOrTarget.SOURCE, source_mesh)
controller.set_preview_mesh(unreal.RetargetSourceOrTarget.TARGET, mesh)
unreal.EditorAssetLibrary.save_loaded_asset(rig); unreal.EditorAssetLibrary.save_loaded_asset(target_rig); unreal.EditorAssetLibrary.save_loaded_asset(retargeter)
names = ["/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS",
         "/Game/Characters/Mannequins/Anims/Rifle/Walk/MF_Rifle_Walk_Fwd",
         "/Game/Characters/Mannequins/Anims/Rifle/Jog/MF_Rifle_Jog_Fwd",
         "/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Fire",
         "/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload",
         "/Game/Characters/Mannequins/Anims/Rifle/Jump/MM_Rifle_Jump_Start",
         "/Game/Characters/Mannequins/Anims/Rifle/Jump/MM_Rifle_Jump_Fall_Loop",
         "/Game/Characters/Mannequins/Anims/Rifle/Jump/MM_Rifle_Jump_Fall_Land",
         "/Game/Characters/Mannequins/Anims/Death/MM_Death_Back_01"]
inputs = unreal.IKRetargetBatchOperationInputs()
inputs.assets_to_retarget = [unreal.EditorAssetLibrary.find_asset_data(path) for path in names]
inputs.source_mesh = source_mesh; inputs.target_mesh = mesh; inputs.ik_retarget_asset = retargeter
inputs.prefix = "Guard_"; inputs.target_path = destination + "/Animations"; inputs.overwrite_existing_files = True
results = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
for result in results:
    asset = result.get_asset()
    assert asset is not None
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset)
record = {"mesh": mesh.get_path_name(), "materials": [s.material_interface.get_path_name() if s.material_interface else "" for s in slots],
          "lods": unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_lod_count(mesh),
          "retargeted": [str(a.package_name) for a in results], "production_approved": False,
          "remaining": ["material-slot consolidation", "face/hand/joint deformation", "rifle grip contact", "density performance", "LOD acceptance"]}
(directory / "workbench-import.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_GUARD_RETARGET " + json.dumps(record))
unreal.SystemLibrary.quit_editor()
