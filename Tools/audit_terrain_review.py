"""Inspect the review scene, disabling physical blocking only on background vistas."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
path = "/Game/ProjectF/Maps/Tests/L_TerrainArtReview"
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); assert levels.load_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith(path + ".")
subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
assets, backdrop = {}, []
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = component.static_mesh
        if not mesh: continue
        name = mesh.get_name().lower()
        if any(token in name for token in ("bg_", "background", "skysphere", "peakmountain")):
            actor.set_actor_enable_collision(False)
            backdrop.append({"actor": actor.get_actor_label(), "mesh": mesh.get_path_name(), "level": actor.get_level().get_path_name()})
        key = mesh.get_path_name()
        if key not in assets:
            assets[key] = {"mesh": key, "lod_triangles": [mesh.get_num_triangles(i) for i in range(subsystem.get_lod_count(mesh))],
                "vertices": subsystem.get_number_verts(mesh, 0), "materials": len(mesh.static_materials),
                "simple_collision": subsystem.get_simple_collision_count(mesh), "placements": 0,
                "blender_validation": "required before promotion", "production_approved": False}
        assets[key]["placements"] += component.get_instance_count() if isinstance(component, unreal.InstancedStaticMeshComponent) else 1
assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
assert levels.save_all_dirty_levels()
(root / "ArtSource/Environment/TerrainReview/model-audit.json").write_text(json.dumps({"map": path, "background_collision_disabled": backdrop,
    "models": list(assets.values()), "production_approved": False}, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
