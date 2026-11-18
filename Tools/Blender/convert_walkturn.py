"""Convert the locally generated SOMA BVH to a Blender Action and FBX, retaining source."""
from pathlib import Path
import hashlib
import json
import math
import bpy

root = Path(__file__).resolve().parents[2]
directory = root / "ArtSource" / "Animations" / "Kimodo" / "WalkTurn_v1"
source = directory / "WalkTurn_FromNPZ.bvh"
assert source.is_file(), source
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.unit_settings.system = "METRIC"
bpy.ops.preferences.addon_enable(module="io_anim_bvh")
bpy.ops.import_anim.bvh(filepath=str(source), global_scale=0.01, frame_start=1, use_fps_scale=False,
                        update_scene_fps=True, update_scene_duration=True,
                        axis_forward="-Z", axis_up="Y", rotate_mode="NATIVE")
rig = next(obj for obj in bpy.context.selected_objects if obj.type == "ARMATURE")
rig.name = "SKEL_SOMA_Source"
rig.animation_data.action.name = "AS_Guard_WalkTurn_Source"
scene = bpy.context.scene
scene.frame_start = math.floor(rig.animation_data.action.frame_range[0])
scene.frame_end = math.ceil(rig.animation_data.action.frame_range[1])
frames = list(range(scene.frame_start, scene.frame_end + 1))
original_action = rig.animation_data.action.copy()
original_action.name = "AS_Guard_WalkTurn_UnmodifiedSOMA"
original_action.use_fake_user = True
root_bone = rig.pose.bones.get("Root")
hips = rig.pose.bones.get("Hips")
assert root_bone and hips
pelvis_error = 0.0
first_pelvis = None
for frame in frames:
    scene.frame_set(frame)
    before = hips.matrix.copy()
    if first_pelvis is None:
        first_pelvis = before.translation.copy()
    delta = before.translation - first_pelvis
    delta.z = 0
    root_matrix = root_bone.matrix.copy()
    root_matrix.translation += delta
    root_bone.matrix = root_matrix
    bpy.context.view_layer.update()
    hips.matrix = before
    root_bone.keyframe_insert(data_path="location", frame=frame)
    hips.keyframe_insert(data_path="location", frame=frame)
    bpy.context.view_layer.update()
    pelvis_error = max(pelvis_error, (hips.matrix.translation - before.translation).length)
assert pelvis_error < 0.001, pelvis_error
positions = []
minimum = [math.inf, math.inf, math.inf]
maximum = [-math.inf, -math.inf, -math.inf]
for frame in frames:
    scene.frame_set(frame)
    root_bone = rig.pose.bones.get("Root") or rig.pose.bones[0]
    positions.append(tuple(rig.matrix_world @ root_bone.head))
    for bone in rig.pose.bones:
        point = rig.matrix_world @ bone.head
        assert all(math.isfinite(value) for value in point), (frame, bone.name)
        for axis in range(3):
            minimum[axis] = min(minimum[axis], point[axis])
            maximum[axis] = max(maximum[axis], point[axis])
distance = sum(math.dist(first, second) for first, second in zip(positions, positions[1:]))
scene.frame_set(scene.frame_start)
blend = directory / "WalkTurn_Action.blend"
bpy.ops.wm.save_as_mainfile(filepath=str(blend))
bpy.ops.object.select_all(action="DESELECT")
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
export = root / "ArtSource" / "Exports" / "AS_Guard_WalkTurn_SOMA.fbx"
bpy.ops.export_scene.fbx(filepath=str(export), use_selection=True, object_types={"ARMATURE"},
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=False,
                         bake_anim_use_nla_strips=False, bake_anim_simplify_factor=0.0,
                         apply_unit_scale=True, axis_forward="-Y", axis_up="Z")
record = {
    "source_npz": str(directory / "AS_Guard_WalkTurn_Source.npz"), "converted_bvh": str(source),
    "blender_action": str(blend), "fbx": str(export), "fbx_sha256": hashlib.sha256(export.read_bytes()).hexdigest(),
    "blender": bpy.app.version_string, "fps": scene.render.fps / scene.render.fps_base,
    "frame_start": scene.frame_start, "frame_end": scene.frame_end, "bones": len(rig.data.bones),
    "finite_transforms": True, "bounds_m": [minimum, maximum], "root_path_m": distance,
    "root_motion_extracted_from_hips": True, "maximum_pelvis_preservation_error_m": pelvis_error,
    "root_start_m": positions[0], "root_end_m": positions[-1], "status": "needs_retarget_and_contact_review",
    "remaining": ["Foot contact and motion quality", "SOMA to project skeleton retarget", "Unreal import and gameplay alignment"],
}
(directory / "conversion.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
print("PF_MOTION_CONVERSION", json.dumps(record))
