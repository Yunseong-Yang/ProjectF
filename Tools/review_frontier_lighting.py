from pathlib import Path
import json
import math
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/L_MountainFrontier")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
for actor in actors:
    if isinstance(actor, unreal.StaticMeshActor) and actor.static_mesh_component.static_mesh:
        name = actor.static_mesh_component.static_mesh.get_name()
        if name == "SM_BG_PeakMountains_A":
            point = actor.get_actor_location(); length = max(1, math.hypot(point.x, point.y))
            actor.set_actor_location(unreal.Vector(point.x / length * 150000, point.y / length * 150000, -12000), False, False)
            actor.set_actor_scale3d(unreal.Vector(300, 300, 300))
        if actor.get_actor_label() == "Relay antenna lattice":
            mast = unreal.load_asset("/Game/ProjectF/Environment/Frontier/Meshes/SM_RadioMast")
            if mast: actor.static_mesh_component.set_static_mesh(mast); actor.set_actor_scale3d(unreal.Vector(1, 1, 1))
    if isinstance(actor, unreal.PFEnvironmentCluster) and actor.instances.static_mesh and actor.instances.static_mesh.get_name() == "SM_Elm08":
        actor.instances.set_collision_profile_name("BlockAll")
    if isinstance(actor, unreal.DirectionalLight):
        actor.light_component.set_editor_property("intensity", 75000)
        actor.set_actor_rotation(unreal.Rotator(pitch=-43, yaw=-55, roll=0), False)
    if isinstance(actor, unreal.SkyLight): actor.light_component.set_editor_property("intensity", 1.8)
    if isinstance(actor, unreal.PFOutpost):
        actor.set_editor_property("alarm_calling_sound", unreal.load_asset("/Game/ProjectF/Audio/S_AlarmCalling"))
        actor.set_editor_property("alarm_sent_sound", unreal.load_asset("/Game/ProjectF/Audio/S_AlarmSent"))
    if isinstance(actor, unreal.PostProcessVolume):
        settings = actor.get_editor_property("settings")
        settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
        settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", True)
        settings.set_editor_property("override_camera_iso", True); settings.set_editor_property("camera_iso", 100)
        settings.set_editor_property("override_camera_shutter_speed", True); settings.set_editor_property("camera_shutter_speed", 125)
        settings.set_editor_property("override_depth_of_field_fstop", True); settings.set_editor_property("depth_of_field_fstop", 8)
        settings.set_editor_property("auto_exposure_bias", -.3)
        actor.set_editor_property("settings", settings)
elm = unreal.load_asset("/Game/ProjectF/Environment/Frontier/Meshes/SM_Elm08")
bark = unreal.load_asset("/Game/ProjectF/Environment/Frontier/Materials/M_ElmBark")
if bark is None:
    bark = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_ElmBark", "/Game/ProjectF/Environment/Frontier/Materials", unreal.Material, unreal.MaterialFactoryNew())
    library = unreal.MaterialEditingLibrary
    diffuse = library.create_material_expression(bark, unreal.MaterialExpressionTextureSample)
    diffuse.texture = unreal.load_asset("/Game/Slay/Assets/Trees/AmurCork/T_TreeBark02_BC")
    normal = library.create_material_expression(bark, unreal.MaterialExpressionTextureSample)
    normal.texture = unreal.load_asset("/Game/Slay/Assets/Trees/AmurCork/T_TreeBark02_N"); normal.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
    library.connect_material_property(diffuse, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    roughness = library.create_material_expression(bark, unreal.MaterialExpressionConstant); roughness.r = .9
    library.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    library.recompile_material(bark); unreal.EditorAssetLibrary.save_loaded_asset(bark)
if bark:
    unreal.MaterialEditingLibrary.set_material_usage(bark, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    unreal.EditorAssetLibrary.save_loaded_asset(bark)
    elm.set_material(0, bark); unreal.EditorAssetLibrary.save_loaded_asset(elm)
ambience = next((a for a in actors if a.get_actor_label() == "Mountain wind"), None)
if ambience is None:
    ambience = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.AmbientSound, unreal.Vector())
    ambience.set_actor_label("Mountain wind"); ambience.tags = ["PF.FrontierGenerated"]
ambience.audio_component.set_sound(unreal.load_asset("/Game/ProjectF/Audio/S_MountainWind")); ambience.audio_component.set_volume_multiplier(.35)
assert levels.save_current_level()
stats = []
for name in ("SM_FrontierGround_0_1", "SM_FrontierGround_4_2", "SM_GraniteCliff", "SM_Elm08"):
    mesh = unreal.load_asset("/Game/ProjectF/Environment/Frontier/Meshes/" + name)
    bounds = mesh.get_bounding_box()
    stats.append({"name": name, "min": [bounds.min.x, bounds.min.y, bounds.min.z], "max": [bounds.max.x, bounds.max.y, bounds.max.z]})
(root / "ArtSource/Environment/spatial-inspection.json").write_text(json.dumps(stats, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
