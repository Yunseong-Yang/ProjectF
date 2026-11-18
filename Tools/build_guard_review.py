from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = "/Game/ProjectF/Maps/Tests/L_GuardRigReview"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    assert levels.load_level(path)
    for actor in actors.get_all_level_actors():
        if "PF.GuardReview" in [str(t) for t in actor.tags]: actors.destroy_actor(actor)
else: assert levels.new_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Game/ProjectF/Core/BP_PFGameMode.BP_PFGameMode_C"))
def spawn(cls, point, rotation=None):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point), rotation or unreal.Rotator())
    actor.tags = ["PF.GuardReview", "PF.Unapproved"]; return actor
floor = spawn(unreal.StaticMeshActor, (0, 0, -50))
floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube")); floor.set_actor_scale3d(unreal.Vector(25, 25, 1))
sun = spawn(unreal.DirectionalLight, (0, 0, 1000), unreal.Rotator(pitch=-35, yaw=-35, roll=0)); sun.light_component.set_editor_property("intensity", 75000)
sun.light_component.set_editor_property("atmosphere_sun_light", True)
spawn(unreal.SkyAtmosphere, (0, 0, 0))
sky = spawn(unreal.SkyLight, (0, 0, 0)); sky.light_component.set_editor_property("intensity", 1.5); sky.light_component.set_editor_property("real_time_capture", True)
post = spawn(unreal.PostProcessVolume, (0, 0, 0)); post.set_editor_property("unbound", True)
settings = post.get_editor_property("settings")
for key, value in {"override_auto_exposure_method": True, "auto_exposure_method": unreal.AutoExposureMethod.AEM_MANUAL,
                   "override_auto_exposure_apply_physical_camera_exposure": True, "auto_exposure_apply_physical_camera_exposure": True,
                   "override_camera_iso": True, "camera_iso": 100, "override_camera_shutter_speed": True, "camera_shutter_speed": 125,
                   "override_depth_of_field_fstop": True, "depth_of_field_fstop": 8}.items(): settings.set_editor_property(key, value)
post.set_editor_property("settings", settings)
mesh = unreal.load_asset("/Game/ProjectF/Characters/Guard/SKM_Guard_Rooted")
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.MaterialEditingLibrary
texture_root = "/Game/Characters/Paragon/Heroes/TwinBlast/Textures/"
def pbr(name, diffuse=None, normal=None, color=(.02, .015, .01), roughness=.6):
    folder = "/Game/ProjectF/Characters/Guard/Materials"
    mat = unreal.load_asset(folder + "/" + name) or tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    library.delete_all_material_expressions(mat)
    if diffuse:
        base = library.create_material_expression(mat, unreal.MaterialExpressionTextureSample); base.texture = unreal.load_asset(texture_root + diffuse)
        assert base.texture, diffuse
        base.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if base.texture.get_editor_property("srgb") else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
        # These source RGB textures pack costume masks rather than final albedo.
        # Use their red channel for subtle wear; keep the actual cloth palette independent.
        scale = library.create_material_expression(mat, unreal.MaterialExpressionMultiply)
        scale.set_editor_property("const_b", .25)
        library.connect_material_expressions(base, "R", scale, "A")
        bias = library.create_material_expression(mat, unreal.MaterialExpressionAdd)
        bias.set_editor_property("const_b", .75)
        library.connect_material_expressions(scale, "", bias, "A")
        tint = library.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
        tint.constant = unreal.LinearColor(*color, 1)
        result = library.create_material_expression(mat, unreal.MaterialExpressionMultiply)
        library.connect_material_expressions(bias, "", result, "A")
        library.connect_material_expressions(tint, "", result, "B")
        library.connect_material_property(result, "", unreal.MaterialProperty.MP_BASE_COLOR)
    else:
        base = library.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector); base.constant = unreal.LinearColor(*color, 1)
        library.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    if normal:
        sample = library.create_material_expression(mat, unreal.MaterialExpressionTextureSample); sample.texture = unreal.load_asset(texture_root + normal)
        assert sample.texture, normal
        sample.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
        library.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_NORMAL)
    rough = library.create_material_expression(mat, unreal.MaterialExpressionConstant); rough.r = roughness
    library.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    library.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    library.recompile_material(mat); unreal.EditorAssetLibrary.save_loaded_asset(mat); return mat
fixed = {
    "Arms": pbr("M_GuardArms", "T_TwinBlast_ActionMovieArms_RGB1", "T_TwinBlast_Arms_N", (.12, .025, .014), .72),
    "Coat": pbr("M_GuardCoat", "T_Twinblast_ActionMovie_Coat_RGBA", "T_Twinblast_ActionMovie_Coat_Pattern_N", (.1, .07, .045), .68),
    "LowerBody": pbr("M_GuardLowerBody", "T_TwinBlast_LowerBody_RGB", "T_TwinBlast_LowerBody__normals", (.055, .075, .035), .78),
    "EyeRefractive": pbr("M_GuardEyes", roughness=.18),
}
slots = list(mesh.get_editor_property("materials"))
for slot in slots:
    name = slot.material_interface.get_name() if slot.material_interface else ""
    for key, replacement in fixed.items():
        if key in name: slot.material_interface = replacement
mesh.set_editor_property("materials", slots)
subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
if subsystem.get_lod_count(mesh) < 3: assert subsystem.regenerate_lod(mesh, 3, True, False)
unreal.EditorAssetLibrary.save_loaded_asset(mesh)
character = spawn(unreal.SkeletalMeshActor, (0, 0, 0), unreal.Rotator(pitch=0, yaw=-90, roll=0))
component = character.skeletal_mesh_component
component.set_skeletal_mesh_asset(mesh)
component.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
animation = unreal.load_asset("/Game/ProjectF/Characters/Guard/Animations/Guard_MF_Rifle_Idle_ADS")
assert animation is not None, "Retargeted animations must be saved and staged before review"
component.override_animation_data(animation, True, True, .5, 1)
data = component.get_editor_property("animation_data")
data.set_editor_property("anim_to_play", animation)
data.set_editor_property("saved_looping", True); data.set_editor_property("saved_playing", True)
component.set_editor_property("animation_data", data)
rifle = spawn(unreal.StaticMeshActor, (0, 0, 0))
rifle.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
rifle.static_mesh_component.set_static_mesh(unreal.load_asset("/Game/ProjectF/Weapons/SM_FieldRifle"))
rifle.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
assert rifle.attach_to_component(component, "HandGrip_R", unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.KEEP_WORLD, False)
for asset_path in unreal.EditorAssetLibrary.list_assets("/Game/Characters/Paragon/Heroes/TwinBlast/Textures", recursive=True):
    texture_asset = unreal.load_asset(asset_path)
    if isinstance(texture_asset, unreal.Texture2D):
        texture_asset.set_editor_property("max_texture_size", 1024 if "Hair" in asset_path or "Eye" in asset_path else 2048)
        unreal.EditorAssetLibrary.save_loaded_asset(texture_asset)
start = spawn(unreal.PlayerStart, (800, 800, 102)); start.tags = list(start.tags) + ["PF.SafeSpawn"]
assert levels.save_current_level()
unreal.SystemLibrary.quit_editor()
