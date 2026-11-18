"""Close the measured stair/courtyard gap with visible, already-inspected stone paving."""
from pathlib import Path
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
path="/Game/ProjectF/Maps/Tests/L_FrontierTemple"
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(path)
assert levels.set_current_level_by_name("L_FrontierTemple")
editor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in list(editor.get_all_level_actors()):
    if "PF.TempleLanding" in [str(tag) for tag in actor.tags]:
        editor.destroy_actor(actor)
records=json.loads((root/"ArtSource/Environment/Temple/unreal-validation.json").read_text(encoding="utf-8"))
row=next(item for item in records if "SM_CourtyardGround_TileStonePath." in item["source"])
mesh=unreal.load_asset(row["derived"])
assert mesh
bounds=mesh.get_bounds()
scale=unreal.Vector(350/bounds.box_extent.x,60/bounds.box_extent.y,1)
offset=unreal.Vector(-bounds.origin.y*scale.y,bounds.origin.x*scale.x,bounds.origin.z)
center=unreal.Vector(15800,10000,4600)
actor=editor.spawn_actor_from_class(unreal.StaticMeshActor,center-offset,unreal.Rotator(pitch=0,yaw=90,roll=0))
assert actor
actor.set_actor_label("Stone threshold at courtyard stair landing")
actor.tags=["PF.TempleLanding","PF.Unapproved"]
actor.static_mesh_component.set_static_mesh(mesh)
actor.set_actor_scale3d(scale)
actor.set_actor_enable_collision(True)
actor.static_mesh_component.set_collision_profile_name("BlockAll")
assert levels.save_current_level()
record={"map":path,"mesh":row["derived"],"center_cm":[center.x,center.y,center.z],
    "scale":[scale.x,scale.y,scale.z],"purpose":"Visible stone threshold spans the measured 0.5m geometry gap", "production_approved":False}
(root/"ArtSource/Environment/Temple/landing-placement.json").write_text(json.dumps(record,indent=2),encoding="utf-8")
unreal.log("PF_TEMPLE_LANDING_COMPLETE")
unreal.SystemLibrary.quit_editor()
