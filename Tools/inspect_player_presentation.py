from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
cls = unreal.load_class(None, "/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C")
player = unreal.get_default_object(cls)
camera = player.camera
record = {"camera_parent": camera.get_attach_parent().get_name(), "camera_location": str(camera.get_editor_property("relative_location")),
          "camera_rotation": str(camera.get_editor_property("relative_rotation")), "first_person_mesh_location": str(player.first_person_mesh.get_editor_property("relative_location")),
          "mesh_location": str(player.get_component_by_class(unreal.SkeletalMeshComponent).get_editor_property("relative_location")),
          "mesh_class": str(player.get_component_by_class(unreal.SkeletalMeshComponent).get_editor_property("anim_class"))}
record["rotator_positional"] = str(unreal.Rotator(0, 30, 0))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
levels.load_level("/Game/ProjectF/Maps/L_MountainFrontier")
record["starts"] = [str(a.get_actor_rotation()) for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
unreal.log("PF_PRESENTATION_CDO " + json.dumps(record))
(root / "Saved/Implementation/20261030_Continuation/presentation-cdo.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
