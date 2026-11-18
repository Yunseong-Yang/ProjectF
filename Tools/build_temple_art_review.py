"""Compose complete source context for art review, still isolated from gameplay maps."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/TerrainReview"
record = json.loads((directory / "additional-scene-source.json").read_text(encoding="utf-8"))
registry = unreal.AssetRegistryHelpers.get_asset_registry(); registry.scan_paths_synchronous(["/Game/Slay"], True)
capped = []
for row in record["files"]:
    data = unreal.EditorAssetLibrary.find_asset_data(row["package"])
    if str(data.asset_class_path.asset_name) != "Texture2D": continue
    texture = data.get_asset(); texture.set_editor_property("max_texture_size", 1024)
    unreal.EditorAssetLibrary.save_loaded_asset(texture); capped.append(row["package"])
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = "/Game/ProjectF/Maps/Tests/L_TempleArtReview"
if unreal.EditorAssetLibrary.does_asset_exist(path): assert levels.load_level(path)
else:
    assert levels.new_level(path); assert levels.load_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(); assert world.get_path_name().startswith(path + ".")
world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Game/ProjectF/Core/BP_PFGameMode.BP_PFGameMode_C"))
start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 500))
start.tags = ["PF.SafeSpawn", "PF.Unapproved", "PF.ArtReview"]
for name in ("Terrain", "Temple_Geo", "Temple_Lighting"):
    package = "/Game/Slay/Envs/Temple/Sublevels/" + name
    existing = unreal.GameplayStatics.get_streaming_level(world, package)
    if existing is None: assert unreal.EditorLevelUtils.add_level_to_world(world, package, unreal.LevelStreamingAlwaysLoaded)
all_actors = list(actors.get_all_level_actors())
cameras, bounds = [], []
for actor in all_actors:
    if isinstance(actor, unreal.CameraActor):
        point, rotation = actor.get_actor_location(), actor.get_actor_rotation()
        cameras.append({"actor": actor.get_actor_label(), "camera": [point.x, point.y, point.z, rotation.pitch, rotation.yaw]})
    if isinstance(actor, unreal.StaticMeshActor):
        mesh = actor.static_mesh_component.static_mesh
        if mesh and any(word in mesh.get_name().lower() for word in ("temple", "dorm", "floor", "wall")):
            center, extent = actor.get_actor_bounds(False)
            bounds.append({"actor": actor.get_actor_label(), "center_cm": [center.x, center.y, center.z], "extent_cm": [extent.x, extent.y, extent.z]})
assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
assert levels.save_all_dirty_levels()
(directory / "complete-review-scene.json").write_text(json.dumps({"map": path, "actors": len(all_actors), "source_cameras": cameras,
    "architecture_bounds": bounds[:40], "new_review_textures_capped_1024": len(capped), "production_approved": False,
    "default_map_changed": False, "required": ["source composition/collision validation", "all used model Blender validation", "gameplay adaptation and performance"]}, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
