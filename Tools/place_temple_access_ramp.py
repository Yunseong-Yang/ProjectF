"""Author a visible side approach using the inspected stone-paving module."""
from pathlib import Path
import json
import math
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
directory=root/"ArtSource/Environment/Temple"
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/Tests/L_FrontierTemple")
assert levels.set_current_level_by_name("L_FrontierTemple")
editor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in list(editor.get_all_level_actors()):
    if "PF.TempleSideApproach" in [str(tag) for tag in actor.tags]:
        editor.destroy_actor(actor)
rows=json.loads((directory/"unreal-validation.json").read_text(encoding="utf-8"))
row=next(item for item in rows if "SM_CourtyardGround_TileStonePath." in item["source"])
mesh=unreal.load_asset(row["derived"])
assert mesh
start=unreal.Vector(14900,10550,4202)
end=unreal.Vector(15950,10550,4605)
length=math.hypot(end.x-start.x,end.z-start.z)
pitch=math.degrees(math.atan2(end.z-start.z,end.x-start.x))
bounds=mesh.get_bounds()
scale=unreal.Vector(length/(2*bounds.box_extent.x),300/(2*bounds.box_extent.y),1)
actor=editor.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(),unreal.Rotator(pitch=pitch,yaw=0,roll=0))
actor.set_actor_label("Stone side approach to occupied temple")
actor.tags=["PF.TempleSideApproach","PF.Unapproved"]
actor.static_mesh_component.set_static_mesh(mesh)
actor.set_actor_scale3d(scale)
center,extent=actor.get_actor_bounds(False)
target=(start+end)*.5
actor.set_actor_location(target-center,False,False)
actor.set_actor_enable_collision(True)
actor.static_mesh_component.set_collision_profile_name("BlockAll")
actor.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
assert levels.save_current_level()
(directory/"side-approach.json").write_text(json.dumps({"mesh":row["derived"],"pitch_degrees":pitch,
    "start_cm":[start.x,start.y,start.z],"end_cm":[end.x,end.y,end.z],"width_cm":300,
    "required":["actual physical AI traversal","entry and courtyard connection","ground-level visual acceptance"]},indent=2),encoding="utf-8")
unreal.log("PF_TEMPLE_SIDE_APPROACH_COMPLETE")
unreal.SystemLibrary.quit_editor()
