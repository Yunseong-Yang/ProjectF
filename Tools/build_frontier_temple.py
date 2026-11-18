"""Adapt inspected temple architecture to the existing two-outpost gameplay region."""
from pathlib import Path
import hashlib
import json
import math
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/Temple"
records = json.loads((directory / "unreal-validation.json").read_text(encoding="utf-8"))
assert len(records) == 233, "Finish all source model validation before building the scene"
mapping = {key: row for row in records for key in (row["source"], row["derived"])}
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
source_path = "/Game/ProjectF/Maps/L_MountainFrontier"
path = "/Game/ProjectF/Maps/Tests/L_FrontierTemple"
geometry_path = "/Game/ProjectF/Maps/Tests/Temple/L_TempleGeometry"
source_file = root / "Content/ProjectF/Maps/L_MountainFrontier.umap"
source_hash = hashlib.sha256(source_file.read_bytes()).hexdigest()
if not unreal.EditorAssetLibrary.does_asset_exist(geometry_path):
    assert levels.new_level_from_template(geometry_path, "/Game/Slay/Envs/Temple/Sublevels/Temple_Geo")
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    assert levels.new_level_from_template(path, source_path)
else:
    assert levels.load_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith(path + ".")
removed, pruned_instances = [], 0
for actor in list(editor.get_all_level_actors()):
    point = actor.get_actor_location()
    distance = math.hypot(point.x-17000, point.y-10000)
    if "PF.TempleGenerated" in [str(t) for t in actor.tags]:
        editor.destroy_actor(actor)
    elif isinstance(actor, unreal.StaticMeshActor) and actor.get_level().get_path_name().startswith(path + ".") and distance < 7800:
        mesh = actor.static_mesh_component.static_mesh
        if mesh and not mesh.get_name().startswith("SM_FrontierGround"):
            removed.append(actor.get_actor_label())
            editor.destroy_actor(actor)
    elif isinstance(actor, unreal.PFCoverPoint) and distance < 7800:
        editor.destroy_actor(actor)
    elif isinstance(actor, unreal.PFEnvironmentCluster):
        component = actor.instances
        indices = []
        for index in range(component.get_instance_count()):
            position = component.get_instance_transform(index, True).translation
            if 12800 < position.x < 21500 and 6000 < position.y < 14000:
                indices.append(index)
        if indices:
            assert component.remove_instances(indices)
            pruned_instances += len(indices)
transform = unreal.Transform(location=unreal.Vector(18000, 10000, 4000), rotation=unreal.Rotator(pitch=0, yaw=180, roll=0))
streaming = unreal.GameplayStatics.get_streaming_level(world, geometry_path)
if streaming is None:
    streaming = unreal.EditorLevelUtils.add_level_to_world_with_transform(world, geometry_path, unreal.LevelStreamingAlwaysLoaded, transform)
assert streaming
applied, doors = [], []
for actor in editor.get_all_level_actors():
    if not actor.get_level().get_path_name().startswith(geometry_path + "."):
        continue
    actor.set_actor_enable_collision(True)
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = component.static_mesh
        if not mesh:
            continue
        row = mapping.get(mesh.get_path_name())
        count = component.get_instance_count() if isinstance(component, unreal.InstancedStaticMeshComponent) else 1
        if count <= 0:
            component.set_static_mesh(None)
            continue
        assert row, mesh.get_path_name()
        component.set_static_mesh(unreal.load_asset(row["derived"]))
        component.set_simulate_physics(False)
        component.set_mobility(unreal.ComponentMobility.STATIC)
        component.set_collision_profile_name("BlockAll")
        if row["background"] or (row["foliage"] and not row["trunk_collision"]):
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        lower = row["source"].lower()
        if "entrancedoor_stones" in lower:
            component.set_visibility(True)
        if "door" in lower and not any(token in lower for token in ("frame", "stone")):
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            component.set_visibility(False)
            doors.append(actor.get_actor_label())
        if any(word in lower for word in ("grass", "fern", "ivy")):
            component.set_cast_shadow(False)
        if row["foliage"]:
            cull = 30000 if row["trunk_collision"] else 10000
            if isinstance(component, unreal.InstancedStaticMeshComponent):
                component.set_cull_distances(int(cull*.8), cull)
            else:
                component.set_cull_distance(cull)
        applied.append({"actor": actor.get_actor_label(), "source": row["source"], "derived": row["derived"], "instances": count})

assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
assert levels.save_all_dirty_levels()
assert levels.set_current_level_by_name("L_TempleGeometry")
assert levels.save_current_level()
assert levels.set_current_level_by_name("L_FrontierTemple")
assert levels.save_current_level()
assert hashlib.sha256(source_file.read_bytes()).hexdigest() == source_hash, "Default gameplay map was changed"
record = {"map": path, "source_map": source_path, "source_map_sha256": source_hash,
    "geometry_level": geometry_path, "geometry_transform": "yaw180, translation(18000,10000,4000) cm",
    "removed_development_structures": removed, "pruned_overlapping_foliage_instances": pruned_instances,
    "components": applied, "opened_door_panels": doors,
    "gameplay_placement": "Reopen the scene, then run Tools/place_temple_exterior_gameplay.py; the interior placement experiment is unaccepted", "production_approved": False,
    "remaining": ["rebuild and validate navigation", "ground-level visuals", "four-player network and density performance"]}
(directory / "frontier-placement.json").write_text(json.dumps(record,indent=2),encoding="utf-8")
unreal.log("PF_FRONTIER_TEMPLE_COMPLETE components="+str(len(applied)))
unreal.SystemLibrary.quit_editor()
