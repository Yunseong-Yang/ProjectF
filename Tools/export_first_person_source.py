"""Preserve and export the current first-person rig for a separate arms-only derivative."""
from pathlib import Path
import hashlib
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
directory=root/"ArtSource/Characters/FirstPerson"
directory.mkdir(parents=True,exist_ok=True)
player=unreal.get_default_object(unreal.load_class(None,"/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"))
source=player.first_person_mesh.get_skeletal_mesh_asset()
assert isinstance(source,unreal.SkeletalMesh)
filename=directory/"FirstPerson_Original.fbx"
task=unreal.AssetExportTask()
task.object=source;task.filename=str(filename);task.automated=True;task.prompt=False;task.replace_identical=False
task.exporter=unreal.SkeletalMeshExporterFBX()
options=unreal.FbxExportOption();options.collision=False;options.level_of_detail=False;options.ascii=False
task.options=options
if not filename.exists():
    assert unreal.Exporter.run_asset_export_task(task)
source_file=root/"Content"/(source.get_path_name().split(".")[0].removeprefix("/Game/")+".uasset")
record={"source":source.get_path_name(),"skeleton":source.get_editor_property("skeleton").get_path_name(),
    "source_file":str(source_file),"source_sha256":hashlib.sha256(source_file.read_bytes()).hexdigest(),
    "fbx":str(filename),"fbx_sha256":hashlib.sha256(filename.read_bytes()).hexdigest(),
    "materials":[s.material_interface.get_path_name() if s.material_interface else None for s in source.get_editor_property("materials")],
    "hand_socket_present":player.first_person_mesh.does_socket_exist("HandGrip_R"),"production_approved":False}
(directory/"source.json").write_text(json.dumps(record,indent=2),encoding="utf-8")
unreal.log("PF_FIRST_PERSON_EXPORT_COMPLETE")
unreal.SystemLibrary.quit_editor()
