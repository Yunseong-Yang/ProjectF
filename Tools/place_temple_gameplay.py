"""Unaccepted interior placement experiment; use place_temple_exterior_gameplay.py for the playable candidate."""
from pathlib import Path
import json
import unreal

if "-PFAllowUnvalidatedTempleInterior" not in unreal.SystemLibrary.get_command_line().split():
    raise RuntimeError("Interior navigation is not accepted. Use place_temple_exterior_gameplay.py; experimental overwrite requires -PFAllowUnvalidatedTempleInterior.")

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/Temple"
path = "/Game/ProjectF/Maps/Tests/L_FrontierTemple"
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert levels.load_level(path)
assert levels.set_current_level_by_name("L_FrontierTemple")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for actor in list(editor.get_all_level_actors()):
    if "PF.TempleGenerated" in [str(t) for t in actor.tags]:
        editor.destroy_actor(actor)
outpost = next(a for a in editor.get_all_level_actors() if isinstance(a, unreal.PFOutpost) and str(a.get_editor_property("outpost_id")) == "Stronghold")
ignore = [a for a in editor.get_all_level_actors() if isinstance(a, (unreal.PFOutpost, unreal.PFInteractable))]
points = []
approach=unreal.NavigationSystemV1.project_point_to_navigation(world,unreal.Vector(12500,10000,4300),None,None,unreal.Vector(150,150,250))
assert approach is not None, "Approach is not on navigation"
counts={"hit":0,"raised":0,"projected":0,"connected":0,"clear":0}
rejections=[]
for x in (3600, 3200, 2800, 2400, 2000, 1600, 1200, 800):
    for y in (-2300, -1700, -1100, -500, 500, 1100, 1700, 2300):
        wx, wy = 18000-x, 10000-y
        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(wx, wy, 5200), unreal.Vector(wx, wy, 4400),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignore, unreal.DrawDebugTrace.NONE, True)
        if not hit:
            continue
        counts["hit"]+=1
        data = hit.to_tuple()
        if data[7].z < .72:
            continue
        center = data[5] + unreal.Vector(0, 0, 100)
        if center.z < 4500:
            continue
        counts["raised"]+=1
        projected = unreal.NavigationSystemV1.project_point_to_navigation(world, center, None, None, unreal.Vector(80,80,160))
        if projected is None:
            continue
        counts["projected"]+=1
        route = unreal.NavigationSystemV1.find_path_to_location_synchronously(world, approach, projected)
        if route is None or not route.is_valid() or route.is_partial():
            rejections.append({"point":[center.x,center.y,center.z],"nav":[projected.x,projected.y,projected.z],"partial":route.is_partial() if route else None})
            continue
        counts["connected"]+=1
        blocked = unreal.SystemLibrary.capsule_trace_single(world, center, center+unreal.Vector(0,0,1), 42, 96,
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignore, unreal.DrawDebugTrace.NONE, True)
        if blocked:
            continue
        counts["clear"]+=1
        points.append(center)
(directory/"placement-filter-diagnostic.json").write_text(json.dumps({"counts":counts,"path_rejections":rejections},indent=2),encoding="utf-8")
assert len(points) >= 20, "Not enough clear courtyard positions: " + str(len(points))
selected = [points[round(index*(len(points)-3)/17)] for index in range(18)]
terminal_point, alarm_point = points[-1], points[-2]
outpost.set_actor_location(terminal_point-unreal.Vector(0,0,50), False, False)
outpost.set_editor_property("display_name", "산악 사원 주둔지")
outpost.set_editor_property("garrison_spawns", [unreal.Transform(location=p, rotation=unreal.Rotator(pitch=0,yaw=180,roll=0)) for p in selected[4:]])
for index, point in enumerate(selected[:4]):
    anchor = editor.spawn_actor_from_class(unreal.TargetPoint, point)
    anchor.tags = ["PF.TempleGenerated", "PF.Unapproved", "PF.Stress.Stronghold."+str(index)]
    anchor.set_actor_label("Temple validated combat start "+str(index))
outpost.set_editor_property("reinforcement_spawns", [unreal.Transform(location=unreal.Vector(11000,10000,4300), rotation=unreal.Rotator(pitch=0,yaw=0,roll=0))])
alarm = outpost.get_editor_property("alarm")
assert alarm
alarm.set_actor_location(alarm_point-unreal.Vector(0,0,75), False, False)
for index, point in enumerate(points[2:14:3]):
    cover_mesh = editor.spawn_actor_from_class(unreal.StaticMeshActor, point+unreal.Vector(0,160,-100))
    cover_mesh.tags = ["PF.TempleGenerated", "PF.Unapproved"]
    cover_mesh.static_mesh_component.set_static_mesh(unreal.load_asset("/Game/ProjectF/Environment/Frontier/Meshes/SM_SandbagCover"))
    cover = editor.spawn_actor_from_class(unreal.PFCoverPoint, point)
    cover.tags = ["PF.TempleGenerated", "PF.Unapproved"]
    cover.set_editor_property("peek_offset", unreal.Vector(160,0,0))
assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
assert levels.save_all_dirty_levels()
record = {"map": path, "clear_courtyard_positions_cm": [[p.x,p.y,p.z] for p in points],
    "garrison_count": 14, "combat_start_anchors": 4, "production_approved": False,
    "required": ["reopened-map navigation and capsule validation", "ground-level visual", "four-player network and performance"]}
(directory / "gameplay-placement.json").write_text(json.dumps(record,indent=2),encoding="utf-8")
unreal.log("PF_TEMPLE_GAMEPLAY_COMPLETE clear_positions="+str(len(points)))
unreal.SystemLibrary.quit_editor()
