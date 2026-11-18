"""Connect one existing sentry's server patrol through the repaired original stairs."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
output = Path((root / "Saved/Implementation/current-continuation-path.txt").read_text(encoding="utf-8"))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/Tests/L_FrontierTemple")
assert levels.set_current_level_by_name("L_FrontierTemple")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
post = next(actor for actor in actors.get_all_level_actors() if isinstance(actor, unreal.PFOutpost)
            and str(actor.get_editor_property("outpost_id")) == "Stronghold")
start = post.get_editor_property("garrison_spawns")[3].translation
points = [start, unreal.Vector(14500, 10000, 4300), unreal.Vector(16200, 10000, 4700)]
route = unreal.PFGarrisonPatrolRoute()
route.set_editor_property("spawn_index", 3)
route.set_editor_property("waypoints", points)
post.set_editor_property("garrison_patrol_routes", [route])
assert levels.save_current_level()
(output / "authored-stair-patrol.json").write_text(json.dumps({"outpost": "Stronghold", "spawn_index": 3,
    "waypoints_cm": [[p.x, p.y, p.z] for p in points], "new_enemy_count": 0, "production_approved": False}, indent=2), encoding="utf-8")
unreal.log("PF_TEMPLE_STAIR_PATROL_AUTHORED")
unreal.SystemLibrary.quit_editor()
