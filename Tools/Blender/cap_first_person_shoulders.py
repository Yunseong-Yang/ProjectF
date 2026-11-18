"""Close exposed shoulder/elbow shell boundaries, retaining skin weights and source inputs."""
from pathlib import Path
import collections
import hashlib
import json
import math
import shutil
import bpy
import bmesh
from mathutils import Vector
from mathutils.kdtree import KDTree

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "ArtSource/Characters/FirstPersonHuman"
record = json.loads((OUT / "blender-validation.json").read_text(encoding="utf-8"))
file = Path(record["export"])
assert hashlib.sha256(file.read_bytes()).hexdigest() == record["export_sha256"]
backup = OUT / "Uncapped_Arms.fbx"
if not backup.exists():
    shutil.copy2(file, backup)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(backup), use_anim=False)
rig = next(obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE")
rig.name = "Armature"
if "root" not in rig.data.bones:
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    roots = [bone for bone in rig.data.edit_bones if bone.parent is None]
    root_bone = rig.data.edit_bones.new("root")
    root_bone.head, root_bone.tail, root_bone.use_deform = (0, 0, 0), (0, .1, 0), False
    for child in roots:
        child.parent, child.use_connect = root_bone, False
    bpy.ops.object.mode_set(mode="OBJECT")
assert len(rig.data.bones) == 89
shoulders = [rig.matrix_world @ rig.data.bones[name].head_local for name in ("upperarm_l", "upperarm_r", "lowerarm_l", "lowerarm_r")]
reports = []
for obj in [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]:
    before = [vertex.co.copy() for vertex in obj.data.vertices]
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    scale = max(abs(value) for value in obj.matrix_world.to_scale())
    near_shoulder = [vertex for vertex in bm.verts if min((obj.matrix_world @ vertex.co - shoulder).length for shoulder in shoulders) < .25]
    # FBX splits vertices along UV/normal seams. Weld only coincident shoulder
    # geometry so those seams do not form branching boundary graphs; UVs stay on loops.
    bmesh.ops.remove_doubles(bm, verts=near_shoulder, dist=.00001 / scale)
    boundary = {edge for edge in bm.edges if edge.is_boundary}
    components = []
    while boundary:
        edges = {boundary.pop()}
        pending = list(edges)
        while pending:
            edge = pending.pop()
            for vertex in edge.verts:
                adjacent = set(vertex.link_edges) & boundary
                boundary -= adjacent
                edges |= adjacent
                pending.extend(adjacent)
        verts = {vertex for edge in edges for vertex in edge.verts}
        if len(verts) < 4 or any(sum(edge in edges for edge in vertex.link_edges) != 2 for vertex in verts):
            continue
        center = sum((obj.matrix_world @ vertex.co for vertex in verts), Vector()) / len(verts)
        signature = tuple(sorted(tuple(round(value, 5) for value in obj.matrix_world @ vertex.co) for vertex in verts))
        components.append({"edges": edges, "verts": verts, "center": center, "signature": signature})
    signatures = collections.Counter(component["signature"] for component in components)
    caps = []
    capped_signatures = set()
    for component in components:
        distance = min((component["center"] - shoulder).length for shoulder in shoulders)
        if component["signature"] in capped_signatures or distance > .16 or len(component["verts"]) < 16:
            continue
        result = bmesh.ops.holes_fill(bm, edges=list(component["edges"]), sides=0)
        faces = result.get("faces", [])
        if not faces:
            start = next(iter(component["verts"]))
            ordered, current, previous = [start], start, None
            for _ in range(len(component["verts"]) + 1):
                choices = [edge for edge in current.link_edges if edge in component["edges"] and edge != previous]
                if not choices:
                    break
                edge = choices[0]
                following = edge.other_vert(current)
                if following == start:
                    break
                ordered.append(following)
                current, previous = following, edge
            if len(set(ordered)) == len(component["verts"]):
                try:
                    face = bm.faces.new(ordered)
                    bm.normal_update()
                    nearest = min(shoulders, key=lambda point: (component["center"] - point).length)
                    direction = obj.matrix_world.inverted().to_3x3() @ (nearest - component["center"])
                    if face.normal.dot(direction) < 0:
                        face.normal_flip()
                    faces = [face]
                except ValueError:
                    pass
        if not faces:
            continue
        for face in faces:
            face.material_index = 0
            face.smooth = False
        uv = bm.loops.layers.uv.active
        if uv:
            for face in faces:
                spans = [max(vertex.co[axis] for vertex in face.verts) - min(vertex.co[axis] for vertex in face.verts) for axis in range(3)]
                axes = sorted(range(3), key=lambda axis: spans[axis], reverse=True)[:2]
                center = sum((vertex.co for vertex in face.verts), Vector()) / len(face.verts)
                for loop in face.loops:
                    loop[uv].uv = tuple(.5 + (loop.vert.co[axis] - center[axis]) / max(spans[axis], 1e-5) * .1 for axis in axes)
        bmesh.ops.triangulate(bm, faces=faces)
        capped_signatures.add(component["signature"])
        caps.append({"boundary_vertices": len(component["verts"]), "world_center": list(component["center"]), "shoulder_distance_m": distance})
    bm.to_mesh(obj.data)
    bm.free()
    positions = KDTree(len(obj.data.vertices))
    for vertex in obj.data.vertices:
        positions.insert(vertex.co, vertex.index)
    positions.balance()
    maximum_error = max(positions.find(original)[2] * scale for original in before)
    assert maximum_error < .00002
    obj.data.calc_loop_triangles()
    degenerate = sum(triangle.area < 1e-12 for triangle in obj.data.loop_triangles)
    assert degenerate == 0 and obj.data.uv_layers
    assert all(math.isfinite(value) for vertex in obj.data.vertices for value in vertex.co)
    reports.append({"triangles": len(obj.data.loop_triangles), "vertices": len(obj.data.vertices), "caps": caps,
                    "original_vertices": len(before), "maximum_vertex_surface_error_m": maximum_error,
                    "degenerate_faces": degenerate,
                    "boundary_components": [{"vertices": len(row["verts"]), "center": list(row["center"]),
                                             "shoulder_distance_m": min((row["center"] - shoulder).length for shoulder in shoulders)} for row in components]})
(OUT / "shoulder-boundary-inspection.json").write_text(json.dumps(reports, indent=2), encoding="utf-8")
assert sum(len(row["caps"]) for row in reports) >= 2, reports
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "HumanFirstPersonArms_Capped.blend"))
bpy.ops.export_scene.fbx(filepath=str(file), use_selection=False, object_types={"ARMATURE", "MESH"},
                        add_leaf_bones=False, bake_anim=False, use_armature_deform_only=False, apply_unit_scale=True,
                        axis_forward="-Y", axis_up="Z", mesh_smooth_type="FACE", use_tspace=True)
record["uncapped_export_sha256"] = hashlib.sha256(backup.read_bytes()).hexdigest()
record["shoulder_cap_validation"] = reports
for mesh, result in zip(record["meshes"], reports):
    mesh["triangles"] = result["triangles"]
    mesh["vertices"] = result["vertices"]
record["export_sha256"] = hashlib.sha256(file.read_bytes()).hexdigest()
(OUT / "blender-validation.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
print("PF_SHOULDER_CAPS_COMPLETE", json.dumps(reports), flush=True)
