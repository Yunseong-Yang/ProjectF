"""Author the playable exterior siege while the cinematic interior remains unaccepted."""
from pathlib import Path
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
directory=root/"ArtSource/Environment/Temple"
path="/Game/ProjectF/Maps/Tests/L_FrontierTemple"
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(path)
assert levels.set_current_level_by_name("L_FrontierTemple")
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
editor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in list(editor.get_all_level_actors()):
    if "PF.TempleGenerated" in [str(t) for t in actor.tags]:
        editor.destroy_actor(actor)
posts=[a for a in editor.get_all_level_actors() if isinstance(a,unreal.PFOutpost)]
post=next(a for a in posts if str(a.get_editor_property("outpost_id"))=="Stronghold")
ignore=[a for a in editor.get_all_level_actors() if isinstance(a,(unreal.PFOutpost,unreal.PFInteractable))]
approach=unreal.NavigationSystemV1.project_point_to_navigation(world,unreal.Vector(12000,10000,4300),None,None,unreal.Vector(200,200,300))
assert approach is not None


def ground(x,y):
    hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,7500),unreal.Vector(x,y,2500),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,ignore,unreal.DrawDebugTrace.NONE,True)
    assert hit, (x,y)
    p=hit.to_tuple()[5]+unreal.Vector(0,0,100)
    projected=unreal.NavigationSystemV1.project_point_to_navigation(world,p,None,None,unreal.Vector(100,100,200))
    assert projected is not None,(x,y,"off navigation")
    route=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,approach,projected)
    assert route is not None and route.is_valid() and not route.is_partial(),(x,y,"disconnected")
    return p


spawns=[ground(x,y) for x in (12600,13050) for y in (7800,8600,9400,10200,11000,11800,12600)]
spawns=spawns[:14]
terminal=ground(12800,10000)
alarm_point=ground(13300,9500)
post.set_actor_location(terminal-unreal.Vector(0,0,50),False,False)
post.set_editor_property("display_name","사원 외곽 주둔지")
post.set_editor_property("garrison_spawns",[unreal.Transform(location=p,rotation=unreal.Rotator(pitch=0,yaw=180,roll=0)) for p in spawns])
post.get_editor_property("alarm").set_actor_location(alarm_point-unreal.Vector(0,0,75),False,False)
entries=[ground(x,y) for x,y in ((23500,8000),(23500,8800),(23500,9600),(23500,10400),(23500,11200),(23500,12000),(24000,8400),(24000,11600))]
post.set_editor_property("reinforcement_spawns",[unreal.Transform(location=p,rotation=unreal.Rotator(pitch=0,yaw=180,roll=0)) for p in entries])
anchors=[ground(x,y) for x,y in ((11700,9500),(11700,10500),(12100,9300),(12100,10700))]
for index,p in enumerate(anchors):
    actor=editor.spawn_actor_from_class(unreal.TargetPoint,p)
    actor.tags=["PF.TempleGenerated","PF.Unapproved","PF.Stress.Stronghold."+str(index)]
    actor.set_actor_label("Temple exterior combat start "+str(index))
for x,y in ((12100,8000),(12100,8800),(12100,11600),(12100,12400)):
    p=ground(x,y)
    mesh=editor.spawn_actor_from_class(unreal.StaticMeshActor,p+unreal.Vector(0,160,-100))
    mesh.tags=["PF.TempleGenerated","PF.Unapproved"]
    mesh.static_mesh_component.set_static_mesh(unreal.load_asset("/Game/ProjectF/Environment/Frontier/Meshes/SM_SandbagCover"))
    cover=editor.spawn_actor_from_class(unreal.PFCoverPoint,p)
    cover.tags=["PF.TempleGenerated","PF.Unapproved"]
    cover.set_editor_property("peek_offset",unreal.Vector(160,0,0))
assert levels.save_current_level()
(directory/"exterior-gameplay.json").write_text(json.dumps({"map":path,"garrison_positions_cm":[[p.x,p.y,p.z] for p in spawns],
    "combat_start_positions_cm":[[p.x,p.y,p.z] for p in anchors],"reinforcement_entries_cm":[[p.x,p.y,p.z] for p in entries],"scope":"Exterior temple siege; interior AI navigation is not accepted",
    "production_approved":False,"required":["native capsule and full path validation","4-player functional test","dense and split performance","visual acceptance"]},indent=2),encoding="utf-8")
unreal.log("PF_TEMPLE_EXTERIOR_COMPLETE garrison=14 starts=4")
unreal.SystemLibrary.quit_editor()
