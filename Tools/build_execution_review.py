"""Author a frozen contact review pair, without connecting unreviewed runtime motion."""
from pathlib import Path
import json
import re
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
path = "/Game/ProjectF/Maps/Tests/L_RearExecutionReview"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    assert levels.load_level(path)
    for actor in actors.get_all_level_actors():
        if "PF.ExecutionReview" in [str(t) for t in actor.tags]: actors.destroy_actor(actor)
else: assert levels.new_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Game/ProjectF/Core/BP_PFGameMode.BP_PFGameMode_C"))

def spawn(cls, point, rotation=None):
    actor = actors.spawn_actor_from_class(cls, unreal.Vector(*point), rotation or unreal.Rotator())
    actor.tags = ["PF.ExecutionReview", "PF.Unapproved"]; return actor

floor = spawn(unreal.StaticMeshActor, (0, 0, -50))
floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube")); floor.set_actor_scale3d(unreal.Vector(20, 20, 1))
sun = spawn(unreal.DirectionalLight, (0, 0, 1000), unreal.Rotator(pitch=-40, yaw=-40, roll=0)); sun.light_component.set_editor_property("intensity", 75000)
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
match = re.search(r"PFExecutionReviewTime=([0-9.]+)", unreal.SystemLibrary.get_command_line())
time = float(match.group(1)) if match else .35
distance = 90
strike = unreal.load_asset("/Game/ProjectF/Player/Animations/AS_Rifle_RearStrike"); assert strike
idle = unreal.load_asset("/Game/ProjectF/Characters/Guard/Animations/Guard_MF_Rifle_Idle_ADS"); assert idle
meshes = [unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"), unreal.load_asset("/Game/ProjectF/Characters/Guard/SKM_Guard_Rooted")]
for index, (mesh, animation, position) in enumerate(((meshes[0], strike, time), (meshes[1], idle, 0))):
    actor = spawn(unreal.SkeletalMeshActor, (index * distance, 0, 0), unreal.Rotator(pitch=0, yaw=-90, roll=0))
    component = actor.skeletal_mesh_component; component.set_skeletal_mesh_asset(mesh)
    component.override_animation_data(animation, False, False, position, 1)
    rifle = spawn(unreal.StaticMeshActor, (0, 0, 0)); rifle.static_mesh_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    rifle.static_mesh_component.set_static_mesh(unreal.load_asset("/Game/ProjectF/Weapons/SM_FieldRifle"))
    rifle.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    assert rifle.attach_to_component(component, "HandGrip_R", unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.SNAP_TO_TARGET, unreal.AttachmentRule.KEEP_WORLD, False)
options = unreal.AnimPoseEvaluationOptions()
def point(animation, time, bone):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation, time, options)
    vector = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation
    return unreal.Vector(vector.y, -vector.x, vector.z)
hand = point(strike, time, "hand_r"); target = point(idle, 0, "spine_03") + unreal.Vector(distance, 0, 0)
gap = (hand - target).length()
record = {"map": path, "strike_time": time, "alignment_cm": distance, "hand_to_upper_back_bone_cm": gap,
          "note": "Bone proximity is diagnostic, not a complete skin/weapon contact acceptance", "production_approved": False}
(root / "ArtSource/Animations/RearExecution/contact-review.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
start = spawn(unreal.PlayerStart, (800, 800, 102)); start.tags = list(start.tags) + ["PF.SafeSpawn"]
assert levels.save_current_level()
unreal.SystemLibrary.quit_editor()
