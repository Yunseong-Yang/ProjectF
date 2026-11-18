"""Use inspected models only in the separate cached-terrain review scene."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/TerrainReview"
mapping = {r["source"]: r for r in json.loads((directory / "unreal-model-validation.json").read_text(encoding="utf-8"))}
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
path = "/Game/ProjectF/Maps/Tests/L_TerrainArtReview"; assert levels.load_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(); assert world.get_path_name().startswith(path + ".")
rows, trees = [], []
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = component.static_mesh
        if not mesh or mesh.get_path_name() not in mapping: continue
        row = mapping[mesh.get_path_name()]; replacement = unreal.load_asset(row["derived"]); assert replacement
        component.set_static_mesh(replacement)
        lower = replacement.get_name().lower()
        foliage = any(word in lower for word in ("grass", "fern", "ivy", "elm", "tree", "bush"))
        if foliage: component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        if any(word in lower for word in ("grass", "fern", "ivy")): component.set_cast_shadow(False)
        cull = 60000 if "elm" in lower or "tree" in lower else 16000 if "bush" in lower else 6500 if foliage else 0
        if cull:
            if isinstance(component, unreal.InstancedStaticMeshComponent): component.set_cull_distances(int(cull * .8), cull)
            else: component.set_cull_distance(cull)
        if ("elm" in lower or "tree" in lower) and len(trees) < 5:
            transform = component.get_instance_transform(0, True) if isinstance(component, unreal.InstancedStaticMeshComponent) and component.get_instance_count() else component.get_world_transform()
            point = transform.translation
            trees.append([point.x, point.y, point.z])
        rows.append({"actor": actor.get_actor_label(), "source": row["source"], "derived": row["derived"], "level": actor.get_level().get_path_name()})
assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
assert levels.save_all_dirty_levels()
(directory / "applied-derivatives.json").write_text(json.dumps({"map": path, "components": rows, "tree_points_cm": trees,
    "default_map_changed": False, "production_approved": False}, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
