"""Reopen the saved player defaults and verify the arms-only display boundary."""
from pathlib import Path
import hashlib
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
directory=root/"ArtSource/Characters/FirstPerson"
source=json.loads((directory/"source.json").read_text(encoding="utf-8"))
player=unreal.get_default_object(unreal.load_class(None,"/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"))
view=player.first_person_mesh.get_skeletal_mesh_asset()
assert view and view.get_path_name()=="/Game/ProjectF/Player/Meshes/SKM_PFFirstPersonArms.SKM_PFFirstPersonArms"
assert view.get_editor_property("skeleton").get_path_name()==source["skeleton"]
assert player.first_person_mesh.does_socket_exist("HandGrip_R")
body=player.get_component_by_class(unreal.SkeletalMeshComponent).get_skeletal_mesh_asset()
assert body.get_path_name()==source["source"],"Only the owner-view mesh should change"
assert hashlib.sha256(Path(source["source_file"]).read_bytes()).hexdigest()==source["source_sha256"]
record={"owner_view":view.get_path_name(),"remote_body":body.get_path_name(),"bone_count":player.first_person_mesh.get_num_bones(),
    "hand_socket_present":True,"source_mesh_preserved":True,"skeleton":source["skeleton"],"passed":True}
(directory/"saved-player-verification.json").write_text(json.dumps(record,indent=2),encoding="utf-8")
unreal.log("PF_FIRST_PERSON_SAVED_VERIFICATION_COMPLETE")
unreal.SystemLibrary.quit_editor()
