"""Reopen the candidate and inspect saved world-space geometry and physical floors."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/Tests/L_FrontierTemple")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
rows = []
for actor in editor.get_all_level_actors():
    if "L_TempleGeometry." not in actor.get_level().get_path_name():
        continue
    if not isinstance(actor, unreal.StaticMeshActor):
        continue
    component = actor.static_mesh_component
    point, extent = actor.get_actor_bounds(False)
    if any(word in (actor.get_actor_label()+" "+actor.get_name()).lower() for word in ("entrancewall", "castlestairs", "courtyardground_tilestonepath", "maintowermiddle_wood", "cobble")):
        rows.append({"actor": actor.get_actor_label(), "center": [point.x,point.y,point.z], "extent": [extent.x,extent.y,extent.z],
            "profile": str(component.get_collision_profile_name()), "enabled": str(component.get_collision_enabled()),
            "mesh": component.static_mesh.get_path_name(), "complexity": str(unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_collision_complexity(component.static_mesh)),
            "has_navigation_data": component.static_mesh.get_editor_property("has_navigation_data"),
            "affects_navigation": component.get_editor_property("can_ever_affect_navigation"),
            "step_up": str(component.get_editor_property("can_character_step_up_on"))})
samples = []
for x,y in ((16000,10000),(14400,12300),(17000,7700),(2000,0),(4000,0)):
    hit = unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,7000),unreal.Vector(x,y,-1000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
    if hit:
        data=hit.to_tuple()
        samples.append({"xy":[x,y],"impact":[data[5].x,data[5].y,data[5].z],"hit_fields":str(data[9:])})
    simple = unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,7000),unreal.Vector(x,y,-1000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE,True)
    if simple:
        data=simple.to_tuple()
        samples.append({"simple_xy":[x,y],"impact":[data[5].x,data[5].y,data[5].z],"hit_fields":str(data[9:])})
(root/"ArtSource/Environment/Temple/saved-scene-inspection.json").write_text(json.dumps({"geometry":rows,"samples":samples},indent=2),encoding="utf-8")
unreal.log("PF_TEMPLE_SAVED_INSPECTION_COMPLETE")
unreal.SystemLibrary.quit_editor()
