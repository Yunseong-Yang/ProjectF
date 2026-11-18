"""Replace the owned concave moss-bank's route-sealing convex hull with its reduced surface."""
from pathlib import Path
import hashlib
import json
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
out = Path((root / "Saved/Implementation/current-continuation-path.txt").read_text(encoding="utf-8"))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/Tests/L_FrontierTemple")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
targets = {part.static_mesh for actor in actors.get_all_level_actors() if actor.get_actor_label() == "SM_MossyEmbankment_Lower"
           for part in actor.get_components_by_class(unreal.StaticMeshComponent) if part.static_mesh}
assert len(targets) == 1
records = []
for mesh in targets:
    assert mesh.get_path_name().startswith("/Game/ProjectF/Environment/Temple/Meshes/")
    relative = Path("Content") / (mesh.get_path_name().removeprefix("/Game/").split(".")[0] + ".uasset")
    backup = out / "baseline" / relative
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(root / relative, backup)
    before = {"primitive": editor.get_simple_collision_count(mesh), "convex": editor.get_convex_collision_count(mesh),
              "complexity": str(editor.get_collision_complexity(mesh)), "lods": editor.get_lod_count(mesh)}
    editor.remove_collisions(mesh)
    body = mesh.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    body.set_editor_property("never_needs_cooked_collision_data", False)
    for section in range(mesh.get_num_sections(0)):
        editor.enable_section_collision(mesh, True, 0, section)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
    assert editor.get_convex_collision_count(mesh) == 0
    records.append({"mesh": mesh.get_path_name(), "before": before, "convex_after": editor.get_convex_collision_count(mesh),
                    "complexity_after": str(editor.get_collision_complexity(mesh)), "source_backup": str(backup),
                    "geometry_changed": False, "production_approved": False})
(out / "embankment-repair.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
unreal.log("PF_EMBANKMENT_SURFACE_COLLISION_COMPLETE")
unreal.SystemLibrary.quit_editor()
