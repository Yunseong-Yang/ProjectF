"""Compose a separate environment review from locally cached native terrain levels."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/TerrainReview"
source = json.loads((directory / "source-manifest.json").read_text(encoding="utf-8"))
library = unreal.EditorAssetLibrary
registry = unreal.AssetRegistryHelpers.get_asset_registry(); registry.scan_paths_synchronous(["/Game/Slay"], True)
# These are newly copied review textures. Existing game textures are left at their recorded settings.
capped = []
for row in source["copied"]:
    data = library.find_asset_data(row["package"])
    if str(data.asset_class_path.asset_name) != "Texture2D": continue
    asset = data.get_asset()
    if isinstance(asset, unreal.Texture2D):
        asset.set_editor_property("max_texture_size", 1024); library.save_loaded_asset(asset); capped.append(row["package"])
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = "/Game/ProjectF/Maps/Tests/L_TerrainArtReview"
assert not library.does_asset_exist(path), "Preserve earlier review; use its scene rather than replacing it"
assert levels.new_level(path)
assert levels.load_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith(path + "."), world.get_path_name()
world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Game/ProjectF/Core/BP_PFGameMode.BP_PFGameMode_C"))

def spawn(cls, point, rotation=None):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point), rotation or unreal.Rotator())
    actor.tags = ["PF.TerrainReview", "PF.Unapproved"]; return actor

sun = spawn(unreal.DirectionalLight, (0, 0, 100000), unreal.Rotator(pitch=-40, yaw=-55, roll=0)); sun.light_component.set_editor_property("intensity", 75000)
sun.light_component.set_editor_property("atmosphere_sun_light", True)
spawn(unreal.SkyAtmosphere, (0, 0, 0))
sky = spawn(unreal.SkyLight, (0, 0, 0)); sky.light_component.set_editor_property("intensity", 1.8); sky.light_component.set_editor_property("real_time_capture", True)
post = spawn(unreal.PostProcessVolume, (0, 0, 0)); post.set_editor_property("unbound", True)
settings = post.get_editor_property("settings")
for key, value in {"override_auto_exposure_method": True, "auto_exposure_method": unreal.AutoExposureMethod.AEM_MANUAL,
    "override_auto_exposure_apply_physical_camera_exposure": True, "auto_exposure_apply_physical_camera_exposure": True,
    "override_camera_iso": True, "camera_iso": 100, "override_camera_shutter_speed": True, "camera_shutter_speed": 125,
    "override_depth_of_field_fstop": True, "depth_of_field_fstop": 8}.items(): settings.set_editor_property(key, value)
post.set_editor_property("settings", settings)
start = spawn(unreal.PlayerStart, (0, 0, 100000)); start.tags = list(start.tags) + ["PF.SafeSpawn"]
for package in ("/Game/Slay/Envs/Temple/Sublevels/Terrain",):
    streaming = unreal.EditorLevelUtils.add_level_to_world(world, package, unreal.LevelStreamingAlwaysLoaded)
    assert streaming, package
all_actors = list(actors.get_all_level_actors())
landscapes = [a for a in all_actors if isinstance(a, unreal.LandscapeProxy)]
assert landscapes, "No native landscape was found"
rows, lo, hi = [], [1e20] * 3, [-1e20] * 3
for actor in landscapes:
    center, extent = actor.get_actor_bounds(False)
    if extent.x <= 0 or extent.y <= 0: continue
    low, high = center - extent, center + extent
    values_low, values_high = [low.x, low.y, low.z], [high.x, high.y, high.z]
    lo = [min(a, b) for a, b in zip(lo, values_low)]; hi = [max(a, b) for a, b in zip(hi, values_high)]
    rows.append({"actor": actor.get_actor_label(), "min_cm": values_low, "max_cm": values_high})
assert rows
ignore = [a for a in all_actors if a not in landscapes]
ground = None
for u, v in ((.5, .5), (.35, .5), (.65, .5), (.5, .35), (.5, .65)):
    x, y = lo[0] + (hi[0] - lo[0]) * u, lo[1] + (hi[1] - lo[1]) * v
    result = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, hi[2] + 100000), unreal.Vector(x, y, lo[2] - 100000), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignore, unreal.DrawDebugTrace.NONE, True)
    if result:
        ground = result.to_tuple()[5]; break
assert ground is not None, "No valid landscape collision at review samples"
start.set_actor_location(ground + unreal.Vector(0, 0, 102), False, False)
span = min(hi[0] - lo[0], hi[1] - lo[1])
camera = [ground.x - span * .12, ground.y - span * .14, ground.z + span * .1, -20, 50]
record = {"map": path, "native_landscapes": rows, "actors": len(all_actors), "ground_cm": [ground.x, ground.y, ground.z],
    "suggested_review_camera": camera, "new_review_textures_capped_1024": len(capped),
    "default_map_changed": False, "production_approved": False,
    "required": ["source shader/VT repair review", "model optimization validation before promotion", "navigation and authored gameplay placement", "frame RAM GPU and 4-player acceptance"]}
(directory / "scene-inspection.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
assert levels.save_all_dirty_levels()
unreal.log("PF_TERRAIN_ART_REVIEW " + json.dumps(record))
unreal.SystemLibrary.quit_editor()
