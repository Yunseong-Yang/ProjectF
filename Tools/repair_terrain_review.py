"""Repair the isolated root map explicitly and record the actual saved world identity."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/TerrainReview"
info = json.loads((directory / "scene-inspection.json").read_text(encoding="utf-8"))
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = "/Game/ProjectF/Maps/Tests/L_TerrainArtReview"
assert levels.load_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith(path + "."), world.get_path_name()
before = [{"actor": a.get_actor_label(), "level": a.get_level().get_path_name()} for a in actors.get_all_level_actors()]
world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Game/ProjectF/Core/BP_PFGameMode.BP_PFGameMode_C"))
def spawn(cls, point, rotation=None):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point), rotation or unreal.Rotator())
    assert actor.get_level().get_path_name().startswith(path + "."), actor.get_level().get_path_name()
    actor.tags = ["PF.TerrainReview", "PF.Unapproved"]; return actor

existing = list(actors.get_all_level_actors())
start = next((a for a in existing if isinstance(a, unreal.PlayerStart) and "PF.SafeSpawn" in [str(t) for t in a.tags]), None)
if start is None: start = spawn(unreal.PlayerStart, [info["ground_cm"][0], info["ground_cm"][1], info["ground_cm"][2] + 102])
start.tags = ["PF.SafeSpawn", "PF.TerrainReview", "PF.Unapproved"]
if not any(isinstance(a, unreal.DirectionalLight) for a in existing):
    sun = spawn(unreal.DirectionalLight, (0, 0, 100000), unreal.Rotator(pitch=-40, yaw=-55, roll=0))
    sun.light_component.set_editor_property("intensity", 75000); sun.light_component.set_editor_property("atmosphere_sun_light", True)
    spawn(unreal.SkyAtmosphere, (0, 0, 0))
    sky = spawn(unreal.SkyLight, (0, 0, 0)); sky.light_component.set_editor_property("intensity", 1.8); sky.light_component.set_editor_property("real_time_capture", True)
    post = spawn(unreal.PostProcessVolume, (0, 0, 0)); post.set_editor_property("unbound", True)
    settings = post.get_editor_property("settings")
    for key, value in {"override_auto_exposure_method": True, "auto_exposure_method": unreal.AutoExposureMethod.AEM_MANUAL,
        "override_auto_exposure_apply_physical_camera_exposure": True, "auto_exposure_apply_physical_camera_exposure": True,
        "override_camera_iso": True, "camera_iso": 100, "override_camera_shutter_speed": True, "camera_shutter_speed": 125,
        "override_depth_of_field_fstop": True, "depth_of_field_fstop": 8}.items(): settings.set_editor_property(key, value)
    post.set_editor_property("settings", settings)
for package in ("/Game/Slay/Envs/Temple/Sublevels/Terrain",):
    streaming = unreal.GameplayStatics.get_streaming_level(world, package)
    if streaming is None: streaming = unreal.EditorLevelUtils.add_level_to_world(world, package, unreal.LevelStreamingAlwaysLoaded)
    assert streaming, package
assert unreal.EditorLoadingAndSavingUtils.save_map(world, path)
assert levels.save_all_dirty_levels()
(directory / "root-save-validation.json").write_text(json.dumps({"world": world.get_path_name(), "actors_before": before,
    "start_level": start.get_level().get_path_name(), "start_location": str(start.get_actor_location()),
    "landscapes_after": len([a for a in actors.get_all_level_actors() if isinstance(a, unreal.LandscapeProxy)]),
    "saved_explicit_world": True, "production_approved": False}, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
