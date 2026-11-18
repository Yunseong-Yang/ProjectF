"""Check actor/section collision separately and explicitly save the owned streamed level."""
from pathlib import Path
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/Tests/L_FrontierTemple")
editor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
meshes=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
mapping={r["derived"]:r for r in json.loads((root/"ArtSource/Environment/Temple/unreal-validation.json").read_text(encoding="utf-8"))}
records=[]
seen=set()
for actor in editor.get_all_level_actors():
    if "L_TempleGeometry." not in actor.get_level().get_path_name():
        continue
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh=component.static_mesh
        row=mapping.get(mesh.get_path_name()) if mesh else None
        if not row or "/Arch/" not in row["source"]:
            continue
        before=actor.get_actor_enable_collision()
        actor.set_actor_enable_collision(True)
        sections=[meshes.is_section_collision_enabled(mesh,0,i) for i in range(mesh.get_num_sections(0))]
        if mesh.get_path_name() not in seen:
            for index,enabled in enumerate(sections):
                if not enabled:
                    meshes.enable_section_collision(mesh,True,0,index)
            assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
            seen.add(mesh.get_path_name())
        records.append({"actor":actor.get_actor_label(),"object":actor.get_name(),"mesh":mesh.get_path_name(),
            "actor_collision_before":before,"sections_before":sections,"sections_after":[meshes.is_section_collision_enabled(mesh,0,i) for i in range(mesh.get_num_sections(0))]})
assert levels.set_current_level_by_name("L_TempleGeometry")
assert levels.save_current_level()
assert levels.set_current_level_by_name("L_FrontierTemple")
assert levels.save_current_level()
(root/"ArtSource/Environment/Temple/collision-repair.json").write_text(json.dumps(records,indent=2),encoding="utf-8")
unreal.log("PF_TEMPLE_COLLISION_REPAIR_COMPLETE actors="+str(len(records)))
unreal.SystemLibrary.quit_editor()
