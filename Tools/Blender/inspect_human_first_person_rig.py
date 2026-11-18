"""Inspect local owned humanoid/first-person bind poses; no assets are overwritten."""
from pathlib import Path
import hashlib
import json
import sys
import bpy

ROOT = Path(__file__).resolve().parents[2]
field = "--field" in sys.argv
OUT = ROOT / ("ArtSource/Characters/FirstPersonField" if field else "ArtSource/Characters/FirstPersonHuman")
OUT.mkdir(parents=True, exist_ok=True)
FILES = {
    "human": ROOT / "ArtSource/Characters/Guard/SKM_Guard.fbx",
    "target": ROOT / "ArtSource/Characters/FirstPerson/FirstPerson_Original.fbx",
}
if field:
    FILES["human"] = Path(json.loads((OUT / "source.json").read_text(encoding="utf-8"))["fbx"])
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
rigs, meshes, records = {}, {}, {}
for label, file in FILES.items():
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(file), use_anim=False)
    imported = set(bpy.data.objects) - before
    rigs[label] = next(obj for obj in imported if obj.type == "ARMATURE")
    meshes[label] = [obj for obj in imported if obj.type == "MESH"]
    rigs[label].name = "Inspect_" + label
    records[label] = {
        "source": str(file), "sha256": hashlib.sha256(file.read_bytes()).hexdigest(),
        "bone_count": len(rigs[label].data.bones),
        "bones": {bone.name: {"parent": bone.parent.name if bone.parent else None,
                   "world_head": list(rigs[label].matrix_world @ bone.head_local),
                   "world_matrix": [list(row) for row in rigs[label].matrix_world @ bone.matrix_local]}
                  for bone in rigs[label].data.bones},
        "meshes": [{"name": obj.name, "vertices": len(obj.data.vertices),
                    "materials": [slot.material.name if slot.material else None for slot in obj.material_slots]}
                   for obj in meshes[label]],
    }
common = set(records["human"]["bones"]) & set(records["target"]["bones"])
arms = [name for name in sorted(common) if any(word in name.lower() for word in ("clavicle", "upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky"))]
records["common_bones"] = sorted(common)
records["arm_bones_shared"] = arms
records["production_approved"] = False
records["scope"] = "Bind-pose inspection only; no rig swap, animation compatibility or visual acceptance"
(OUT / "rig-inspection.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
print(json.dumps({"human_bones": records["human"]["bone_count"], "target_bones": records["target"]["bone_count"],
                  "shared": len(common), "shared_arm_bones": len(arms)}))
