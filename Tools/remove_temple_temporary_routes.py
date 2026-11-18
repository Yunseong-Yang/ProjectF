"""Remove superseded route experiments; the repaired original stairs remain walkable."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
output = Path((root / "Saved/Implementation/current-continuation-path.txt").read_text(encoding="utf-8"))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/Tests/L_FrontierTemple")
assert levels.set_current_level_by_name("L_FrontierTemple")
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tags = {"PF.TempleTraversal", "PF.TempleSideApproach", "PF.TempleLanding"}
removed = []
for actor in list(editor.get_all_level_actors()):
    if tags.intersection(str(tag) for tag in actor.tags):
        assert actor.get_outer().get_outer().get_path_name().split(".")[0] == "/Game/ProjectF/Maps/Tests/L_FrontierTemple"
        removed.append(actor.get_actor_label())
        assert editor.destroy_actor(actor)
assert len(removed) == 4, removed
assert levels.save_current_level()
(output / "removed-temporary-routes.json").write_text(json.dumps(removed, indent=2), encoding="utf-8")
unreal.log("PF_TEMPLE_TEMPORARY_ROUTES_REMOVED")
unreal.SystemLibrary.quit_editor()
