"""Import the arms derivative onto the existing rig; change only the owner-view display."""
from pathlib import Path
import hashlib
import json
import shutil
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
directory=root/"ArtSource/Characters/FirstPerson"
source=json.loads((directory/"source.json").read_text(encoding="utf-8"))
validation=json.loads((directory/"blender-validation.json").read_text(encoding="utf-8"))
assert hashlib.sha256(Path(validation["export"]).read_bytes()).hexdigest()==validation["export_sha256"]
assert hashlib.sha256(Path(source["source_file"]).read_bytes()).hexdigest()==source["source_sha256"]
blueprint_file=root/"Content/ProjectF/Player/BP_PFPlayer.uasset"
backup=root/"Saved/Implementation/20261030_Completion/baseline/Content/ProjectF/Player/BP_PFPlayer.uasset"
if not backup.exists():
    backup.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(blueprint_file,backup)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world,"Interchange.FeatureFlags.Import.FBX 0")
skeleton=unreal.load_asset(source["skeleton"])
assert skeleton
task=unreal.AssetImportTask()
task.filename=validation["export"];task.destination_path="/Game/ProjectF/Player/Meshes";task.destination_name="SKM_PFFirstPersonArms"
task.automated=True;task.replace_existing=True;task.save=False
options=unreal.FbxImportUI()
options.import_mesh=True;options.import_as_skeletal=True;options.import_animations=False
options.automated_import_should_detect_type=False;options.mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH
options.import_materials=False;options.import_textures=False;options.create_physics_asset=False;options.skeleton=skeleton
data=options.skeletal_mesh_import_data
data.set_editor_property("update_skeleton_reference_pose",False)
data.set_editor_property("use_t0_as_ref_pose",False)
data.set_editor_property("normal_import_method",unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
task.options=options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh=unreal.load_asset("/Game/ProjectF/Player/Meshes/SKM_PFFirstPersonArms")
assert isinstance(mesh,unreal.SkeletalMesh)
assert mesh.get_editor_property("skeleton")==skeleton,"Do not replace the established skeleton"
slots=list(mesh.get_editor_property("materials"))
by_name={path.split(".")[-1]:path for path in source["materials"] if path}
for slot in slots:
    name=str(slot.get_editor_property("imported_material_slot_name"))
    path=next((path for key,path in by_name.items() if name==key or name.startswith(key+"_")),None)
    assert path,(name,list(by_name))
    slot.material_interface=unreal.load_asset(path)
mesh.set_editor_property("materials",slots)
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
blueprint=unreal.load_asset("/Game/ProjectF/Player/BP_PFPlayer")
player=unreal.get_default_object(unreal.load_class(None,"/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"))
body_before=player.get_component_by_class(unreal.SkeletalMeshComponent).get_skeletal_mesh_asset().get_path_name()
player.first_person_mesh.set_editor_property("skeletal_mesh_asset",mesh)
assert player.first_person_mesh.does_socket_exist("HandGrip_R"),"Displayed weapon socket must survive"
assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint,False)
assert hashlib.sha256(Path(source["source_file"]).read_bytes()).hexdigest()==source["source_sha256"]
record={"display_mesh":mesh.get_path_name(),"skeleton":source["skeleton"],"hand_socket_present":True,
    "materials":[s.material_interface.get_path_name() for s in mesh.get_editor_property("materials")],
    "lod_count":unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).get_lod_count(mesh),
    "source_mesh_preserved":True,"owner_view_only":True,"body_before":body_before,"production_approved":False,
    "required":["reopened CDO persistence","first-person fire/reload/execution pose","native camera close-up","performance and co-op regression"]}
(directory/"unreal-validation.json").write_text(json.dumps(record,indent=2),encoding="utf-8")
unreal.log("PF_FIRST_PERSON_ARMS_IMPORT_COMPLETE")
unreal.SystemLibrary.quit_editor()
