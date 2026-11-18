"""Preserve a licensed character source; remove unused weapons and inspect skinning.

Protected face/hand vertices must survive any near-model reduction. If they do
not, retain the cleaned full geometry instead of accepting a damaging reduction.
"""
from pathlib import Path
import collections
import hashlib
import json
import bpy
import bmesh

ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT / "ArtSource/Characters/Guard"
bpy.ops.object.select_all(action="SELECT"); bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(DIRECTORY / "Guard_Original.fbx"), use_anim=False)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
rigs = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
assert meshes and len(rigs) == 1, (len(meshes), len(rigs))
rig = rigs[0]
bpy.ops.wm.save_as_mainfile(filepath=str(DIRECTORY / "Guard_Source.blend"))
bpy.ops.object.select_all(action="DESELECT")
for obj in meshes: obj.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1: bpy.ops.object.join()
obj = bpy.context.object


def stats(mesh):
    mesh.data.calc_loop_triangles()
    return {"vertices": len(mesh.data.vertices), "triangles": len(mesh.data.loop_triangles), "materials": len(mesh.data.materials),
            "uv_layers": len(mesh.data.uv_layers), "vertex_groups": len(mesh.vertex_groups)}


before = stats(obj)
material_names = [m.name if m else "" for m in obj.data.materials]
unused = {i for i, name in enumerate(material_names) if "Pistol" in name}
bm = bmesh.new(); bm.from_mesh(obj.data)
discard = [face for face in bm.faces if face.material_index in unused]
removed_faces = len(discard)
bmesh.ops.delete(bm, geom=discard, context="FACES")
bmesh.ops.dissolve_degenerate(bm, dist=1e-7, edges=list(bm.edges))
bm.to_mesh(obj.data); bm.free()
bone_names = {b.name for b in rig.data.bones}
groups = {g.index: g.name for g in obj.vertex_groups}
critical = {i for i, name in groups.items() if any(key in name.lower() for key in ("head", "neck", "hand", "thumb", "index", "middle", "pinky", "ring"))}
protected = obj.vertex_groups.new(name="PreserveFaceAndHands")
source_positions = collections.Counter()
for vertex in obj.data.vertices:
    weight = sum(g.weight for g in vertex.groups if g.group in critical)
    if weight > .9:
        protected.add([vertex.index], 1.0, "REPLACE")
        source_positions[tuple(round(v, 5) for v in vertex.co)] += 1
clean_data = obj.data.copy()
reduction_accepted = False
if stats(obj)["triangles"] > 60000:
    modifier = obj.modifiers.new("Protect hands and facial topology", "DECIMATE")
    modifier.ratio = 60000 / stats(obj)["triangles"]
    modifier.vertex_group = protected.name; modifier.invert_vertex_group = True; modifier.vertex_group_factor = 1.0
    modifier.use_collapse_triangulate = True
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    after_positions = collections.Counter(tuple(round(v, 5) for v in vertex.co) for vertex in obj.data.vertices)
    missing = sum((source_positions - after_positions).values())
    if missing:
        obj.data = clean_data
    else:
        reduction_accepted = True
obj.vertex_groups.remove(obj.vertex_groups.get("PreserveFaceAndHands"))
bad_weights = 0
for vertex in obj.data.vertices:
    weights = [(g.group, g.weight) for g in vertex.groups if groups.get(g.group) in bone_names]
    total = sum(w for _, w in weights)
    if total <= 1e-8: bad_weights += 1; continue
    for index, weight in weights: obj.vertex_groups[index].add([vertex.index], weight / total, "REPLACE")
assert bad_weights == 0, bad_weights
# UE's Blender importer recognizes this object name and does not add a new root bone.
rig.name = "Armature"
root_added = False
if "root" not in rig.data.bones:
    bpy.ops.object.select_all(action="DESELECT"); rig.select_set(True); bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    existing_roots = [bone for bone in rig.data.edit_bones if bone.parent is None]
    root_bone = rig.data.edit_bones.new("root"); root_bone.head = (0, 0, 0); root_bone.tail = (0, .1, 0); root_bone.use_deform = False
    for bone in existing_roots: bone.parent = root_bone; bone.use_connect = False
    bpy.ops.object.mode_set(mode="OBJECT"); root_added = True
bpy.ops.wm.save_as_mainfile(filepath=str(DIRECTORY / "Guard_Optimized.blend"))
bpy.ops.object.select_all(action="DESELECT"); obj.select_set(True); rig.select_set(True)
bpy.context.view_layer.objects.active = obj
destination = DIRECTORY / "SKM_Guard.fbx"
bpy.ops.export_scene.fbx(filepath=str(destination), use_selection=True, object_types={"MESH", "ARMATURE"},
                         apply_unit_scale=True, axis_forward="-Y", axis_up="Z", add_leaf_bones=False,
                         use_armature_deform_only=False, bake_anim=False, mesh_smooth_type="FACE")
record = {"blender": bpy.app.version_string, "source_sha256": hashlib.sha256((DIRECTORY / "Guard_Original.fbx").read_bytes()).hexdigest(),
          "export_sha256": hashlib.sha256(destination.read_bytes()).hexdigest(), "before": before, "after": stats(obj),
          "unused_weapon_faces_removed": removed_faces, "protected_face_hand_vertices": sum(source_positions.values()),
          "reduction_accepted": reduction_accepted, "bone_count": len(rig.data.bones), "explicit_root_restored": root_added, "unweighted_vertices": bad_weights,
          "materials": material_names, "status": "requires material consolidation, rig round-trip, joint/contact and density validation", "production_approved": False}
(DIRECTORY / "blender-validation.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
print("PF_GUARD_BLENDER", json.dumps(record))
