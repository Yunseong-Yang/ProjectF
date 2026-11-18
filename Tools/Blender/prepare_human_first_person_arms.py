"""Fit owned humanoid arm geometry to the preserved first-person bind pose locally."""
from pathlib import Path
import collections
import hashlib
import json
import math
import sys
import bpy
import bmesh

ROOT = Path(__file__).resolve().parents[2]
field = "--field" in sys.argv
OUT = ROOT / ("ArtSource/Characters/FirstPersonField" if field else "ArtSource/Characters/FirstPersonHuman")
inspection = json.loads((OUT / "rig-inspection.json").read_text(encoding="utf-8"))
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
rigs, meshes = {}, {}
for label in ("human", "target"):
    file = Path(inspection[label]["source"])
    assert hashlib.sha256(file.read_bytes()).hexdigest() == inspection[label]["sha256"]
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(file), use_anim=False)
    imported = set(bpy.data.objects) - before
    rigs[label] = next(obj for obj in imported if obj.type == "ARMATURE")
    meshes[label] = [obj for obj in imported if obj.type == "MESH"]
source, target = rigs["human"], rigs["target"]
source.name = "SourceHumanRig"
target.name = "Armature"
if "root" not in target.data.bones:
    bpy.ops.object.select_all(action="DESELECT")
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.mode_set(mode="EDIT")
    roots = [bone for bone in target.data.edit_bones if bone.parent is None]
    bone = target.data.edit_bones.new("root")
    bone.head, bone.tail, bone.use_deform = (0, 0, 0), (0, .1, 0), False
    for child in roots:
        child.parent, child.use_connect = bone, False
    bpy.ops.object.mode_set(mode="OBJECT")
assert len(target.data.bones) == 89
common = set(source.data.bones.keys()) & set(target.data.bones.keys())
inverse = source.matrix_world.inverted()
for bone in sorted(source.data.bones, key=lambda value: len(value.parent_recursive)):
    if bone.name in common:
        source.pose.bones[bone.name].matrix = inverse @ target.matrix_world @ target.data.bones[bone.name].matrix_local
        bpy.context.view_layer.update()
errors = {name: (source.matrix_world @ source.pose.bones[name].head - target.matrix_world @ target.data.bones[name].head_local).length
          for name in common if name.startswith(("upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky"))}
assert errors and max(errors.values()) < .001, errors

remap = {}
for bone in source.data.bones:
    ancestor = bone
    while ancestor and ancestor.name not in target.data.bones:
        ancestor = ancestor.parent
    remap[bone.name] = ancestor.name if ancestor else "root"
records, exported = [], []
prefixes = ("upperarm", "lowerarm", "hand", "thumb", "index", "middle", "ring", "pinky")
for original in meshes["human"]:
    obj = original.copy()
    obj.data = original.data.copy()
    bpy.context.collection.objects.link(obj)
    removed_shape_keys = len(obj.data.shape_keys.key_blocks) if obj.data.shape_keys else 0
    if obj.data.shape_keys:
        obj.shape_key_clear()
    obj.data.calc_loop_triangles()
    before = len(obj.data.loop_triangles)
    bpy.context.view_layer.objects.active = obj
    for modifier in list(obj.modifiers):
        if modifier.type == "ARMATURE":
            bpy.ops.object.modifier_apply(modifier=modifier.name)
    groups = {group.index for group in obj.vertex_groups if group.name.startswith(prefixes)}
    weight = {vertex.index: sum(item.weight for item in vertex.groups if item.group in groups) for vertex in obj.data.vertices}
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bm.verts.ensure_lookup_table()
    remove = [face for face in bm.faces if not all(weight[vertex.index] >= .5 for vertex in face.verts)
              or any(word in obj.material_slots[face.material_index].name.lower() for word in ("pistol", "coat"))]
    bmesh.ops.delete(bm, geom=remove, context="FACES_ONLY")
    bmesh.ops.delete(bm, geom=[vertex for vertex in bm.verts if not vertex.link_faces], context="VERTS")
    bm.to_mesh(obj.data)
    bm.free()
    if not obj.data.polygons:
        bpy.data.objects.remove(obj, do_unlink=True)
        continue
    names = {group.index: group.name for group in obj.vertex_groups}
    weights = []
    for vertex in obj.data.vertices:
        mapped = collections.defaultdict(float)
        for item in vertex.groups:
            mapped[remap[names[item.group]]] += item.weight
        total = sum(mapped.values())
        assert total > .00001
        weights.append({name: value / total for name, value in mapped.items() if value > .000001})
    obj.vertex_groups.clear()
    new_groups = {name: obj.vertex_groups.new(name=name) for name in sorted({name for row in weights for name in row})}
    for index, row in enumerate(weights):
        for name, value in row.items():
            new_groups[name].add([index], value, "REPLACE")
    world = obj.matrix_world.copy()
    obj.parent = target
    obj.matrix_parent_inverse = target.matrix_world.inverted()
    obj.matrix_world = world
    modifier = obj.modifiers.new("Preserved first-person reference rig", "ARMATURE")
    modifier.object = target
    obj.data.calc_loop_triangles()
    protected_count, protected_lost = 0, 0
    if len(obj.data.loop_triangles) > 25000:
        protected = obj.vertex_groups.new(name="PreserveHandsAndElbows")
        name_indices = {group.index: group.name for group in obj.vertex_groups}
        positions = collections.Counter()
        for vertex in obj.data.vertices:
            hand = sum(item.weight for item in vertex.groups if name_indices[item.group].startswith(("hand", "thumb", "index", "middle", "ring", "pinky")))
            upper = sum(item.weight for item in vertex.groups if name_indices[item.group].startswith("upperarm"))
            lower = sum(item.weight for item in vertex.groups if name_indices[item.group].startswith("lowerarm"))
            if hand > .1 or (upper > .15 and lower > .15):
                protected.add([vertex.index], 1, "REPLACE")
                positions[tuple(round(value, 5) for value in vertex.co)] += 1
        protected_count = sum(positions.values())
        decimate = obj.modifiers.new("Preserve finger and elbow geometry", "DECIMATE")
        decimate.ratio = 25000 / len(obj.data.loop_triangles)
        decimate.vertex_group = protected.name
        decimate.invert_vertex_group = True
        decimate.vertex_group_factor = 1
        decimate.use_collapse_triangulate = True
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=decimate.name)
        remaining = collections.Counter(tuple(round(value, 5) for value in vertex.co) for vertex in obj.data.vertices)
        protected_lost = sum((positions - remaining).values())
        assert protected_lost == 0, "Do not damage finger/elbow geometry to meet the budget"
        obj.vertex_groups.remove(obj.vertex_groups.get("PreserveHandsAndElbows"))
    used = sorted({polygon.material_index for polygon in obj.data.polygons})
    materials = [obj.data.materials[index] for index in used]
    slots = {old: new for new, old in enumerate(used)}
    material_indices = [slots[polygon.material_index] for polygon in obj.data.polygons]
    obj.data.materials.clear()
    for material in materials:
        obj.data.materials.append(material)
    for polygon, index in zip(obj.data.polygons, material_indices):
        polygon.material_index = index
        polygon.use_smooth = True
    obj.data.calc_loop_triangles()
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    degenerate = sum(face.calc_area() < 1e-12 for face in bm.faces)
    bm.free()
    assert degenerate == 0 and obj.data.uv_layers
    assert all(math.isfinite(value) for vertex in obj.data.vertices for value in vertex.co)
    assert len(obj.data.loop_triangles) <= 25000 and len(materials) <= 4
    obj.name = "SKM_PFFieldArms" if field else "SKM_PFHumanFirstPersonArms"
    exported.append(obj)
    records.append({"source_triangles": before, "triangles": len(obj.data.loop_triangles), "vertices": len(obj.data.vertices),
                    "materials": [material.name for material in materials], "original_material_slots": used,
                    "uv_layers": len(obj.data.uv_layers), "degenerate_faces": degenerate, "unweighted_vertices": 0,
                    "protected_vertices": protected_count, "protected_vertices_lost": protected_lost,
                    "unused_source_shape_keys_removed": removed_shape_keys,
                    "deformation": "Source bind pose fitted to the exact preserved player rig; weights mapped to named bones/ancestors",
                    "reduction": "Non-arm, pistol and unsupported coat surfaces removed; all retained arm/finger vertices preserved"})
assert exported
for obj in meshes["human"] + meshes["target"]:
    obj.hide_set(True)
    obj.hide_render = True
source.hide_set(True)
source.hide_render = True
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "HumanFirstPersonArms.blend"))
bpy.ops.object.select_all(action="DESELECT")
target.select_set(True)
for obj in exported:
    obj.select_set(True)
file = OUT / ("SKM_PFFieldArms.fbx" if field else "SKM_PFHumanFirstPersonArms.fbx")
bpy.ops.export_scene.fbx(filepath=str(file), use_selection=True, object_types={"ARMATURE", "MESH"},
                        add_leaf_bones=False, bake_anim=False, use_armature_deform_only=False, apply_unit_scale=True,
                        axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=True)
record = {"sources": {label: {"source": inspection[label]["source"], "sha256": inspection[label]["sha256"]} for label in ("human", "target")},
          "bone_count": len(target.data.bones), "bones": [{"name": bone.name, "parent": bone.parent.name if bone.parent else None} for bone in target.data.bones],
          "arm_bind_pose_error_m": errors, "meshes": records, "export": str(file), "export_sha256": hashlib.sha256(file.read_bytes()).hexdigest(),
          "production_approved": False, "required": ["Unreal skeleton and material validation", "Native hands/weapon/animation visual review"]}
(OUT / "blender-validation.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
print("PF_HUMAN_ARMS_PREPARED", json.dumps(records), flush=True)
