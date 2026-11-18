"""Sample imported contact trajectories through the engine's actual animation evaluator."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve().parents[1]
directory = root / "ArtSource/Animations/RearExecution"
extension = unreal.AnimPoseExtensions
options = unreal.AnimPoseEvaluationOptions()
rows = []
for weapon in ("Rifle", "Pistol", "Shotgun"):
    animation = unreal.load_asset("/Game/ProjectF/Player/Animations/AS_" + weapon + "_RearStrike"); assert animation
    poses = []
    for step in range(21):
        time = animation.get_play_length() * step / 20
        pose = extension.get_anim_pose_at_time(animation, time, options)
        bones = {}
        for name in ("root", "pelvis", "spine_03", "head", "hand_l", "hand_r"):
            transform = extension.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD)
            point = transform.translation
            # Project body component yaw=-90; this converts component-space positions to actor-space.
            bones[name] = [point.y, -point.x, point.z - 96]
        poses.append({"time": time, "bones_actor_space_cm": bones})
    rows.append({"weapon": weapon, "duration": animation.get_play_length(), "poses": poses})
(directory / "contact-trajectories.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
target_rows = []
for path in ("/Game/ProjectF/Characters/Guard/Animations/Guard_MF_Rifle_Idle_ADS", "/Game/ProjectF/Characters/Guard/Animations/Guard_MM_Death_Front_01"):
    animation = unreal.load_asset(path); assert animation
    samples = []
    for fraction in (0, .25, .5, .75, 1):
        pose = extension.get_anim_pose_at_time(animation, animation.get_play_length() * fraction, options)
        bones = {}
        for name in ("root", "spine_03", "head"):
            point = extension.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD).translation
            bones[name] = [point.y, -point.x, point.z - 96]
        samples.append({"fraction": fraction, "bones_actor_space_cm": bones})
    target_rows.append({"animation": path, "duration": animation.get_play_length(), "samples": samples})
(directory / "victim-trajectories.json").write_text(json.dumps(target_rows, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
