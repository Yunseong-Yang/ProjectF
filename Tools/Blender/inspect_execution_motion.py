"""Preserve and inspect exported local source motion without inference or addons."""
from pathlib import Path
import hashlib
import json
import math
import bpy

root = Path(__file__).resolve().parents[2]
directory = root / "ArtSource/Animations/RearExecution"
records = []
for weapon in ("Rifle", "Pistol", "Shotgun"):
    bpy.ops.object.select_all(action="SELECT"); bpy.ops.object.delete(use_global=False)
    before = set(bpy.data.actions)
    source = directory / (weapon + "_Source.fbx")
    bpy.ops.import_scene.fbx(filepath=str(source), use_anim=True)
    rigs = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
    assert len(rigs) == 1
    actions = list(set(bpy.data.actions) - before); assert actions
    rig, action = rigs[0], actions[0]
    lo, hi = action.frame_range
    samples = []
    for time in (0, .2, .4, .6, .8, 1):
        frame = lo + (hi - lo) * time
        bpy.context.scene.frame_set(int(frame), subframe=frame % 1)
        points = {}
        for name in ("root", "pelvis", "head", "hand_r", "hand_l", "foot_r", "foot_l"):
            bone = rig.pose.bones.get(name)
            if name == "root" and bone is None:
                point = rig.matrix_world.translation
            else:
                assert bone, name
                point = rig.matrix_world @ bone.matrix.translation
            assert all(math.isfinite(v) for v in point)
            points[name] = list(point)
        samples.append({"normalized_time": time, "points_m": points})
    bpy.ops.wm.save_as_mainfile(filepath=str(directory / (weapon + "_MotionSource.blend")))
    records.append({"weapon": weapon, "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "blender": bpy.app.version_string, "bones": len(rig.data.bones), "root_representation": "bone" if "root" in rig.pose.bones else "armature_object", "frame_range": [lo, hi], "samples": samples,
        "production_approved": False})
(directory / "blender-validation.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
