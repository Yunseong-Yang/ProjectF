"""Remove empty cinematic foliage references from the owned streamed level."""
from pathlib import Path
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/Tests/L_FrontierTemple")
removed=[]
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if "L_TempleGeometry." not in actor.get_level().get_path_name():
        continue
    for component in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
        if component.static_mesh and component.get_instance_count()==0:
            removed.append(component.static_mesh.get_path_name())
            component.set_static_mesh(None)
assert levels.set_current_level_by_name("L_TempleGeometry")
assert levels.save_current_level()
(root/"ArtSource/Environment/Temple/pruned-empty-instance-references.json").write_text(json.dumps(removed,indent=2),encoding="utf-8")
unreal.log("PF_TEMPLE_PRUNE_COMPLETE references="+str(len(removed)))
unreal.SystemLibrary.quit_editor()
