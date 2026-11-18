"""Deterministic offline temple cleanup; preserve originals, UVs and source materials."""
from pathlib import Path
import hashlib
import json
import math
import statistics
import bpy
import bmesh
from mathutils import Vector

root = Path(__file__).resolve().parents[2]
directory = root / "ArtSource/Environment/Temple"
exports = directory / "ValidatedExports"
work = directory / "Work"
exports.mkdir(exist_ok=True)
work.mkdir(exist_ok=True)
records = json.loads((directory / "exported-models.json").read_text(encoding="utf-8"))
assert len(records) == len(json.loads((directory / "source-audit.json").read_text(encoding="utf-8"))["models"])
report = directory / "blender-validation.json"
previous = {r["source"]: r for r in json.loads(report.read_text(encoding="utf-8"))} if report.exists() else {}
results = []
bpy.context.preferences.filepaths.save_version = 0


def topology(mesh):
    mesh.calc_loop_triangles()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    result = {"vertices": len(mesh.vertices), "triangles": len(mesh.loop_triangles),
        "uv_layers": len(mesh.uv_layers), "materials": len(mesh.materials),
        "degenerate_faces": sum(face.calc_area() < 1e-12 for face in bm.faces),
        "nonmanifold_edges": sum(not edge.is_manifold for edge in bm.edges)}
    bm.free()
    assert all(math.isfinite(c) for v in mesh.vertices for c in v.co), "Non-finite geometry"
    return result


for row in records:
    filename = exports / (row["name"] + ".fbx")
    blend = work / (row["name"] + ".blend")
    old = previous.get(row["source"])
    if old and old["after"]["degenerate_faces"] == 0 and old["fbx_sha256"] == row["fbx_sha256"] and filename.exists() and blend.exists() and hashlib.sha256(filename.read_bytes()).hexdigest() == old["export_sha256"]:
        results.append(old)
        continue
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=row["fbx"], use_anim=False)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
    assert meshes, row["source"]
    bpy.ops.object.select_all(action="DESELECT")
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = row["name"]
    before = topology(obj.data)
    assert before["uv_layers"] > 0, row["source"]
    if before["degenerate_faces"]:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.dissolve_degenerate(bm, dist=1e-7, edges=list(bm.edges))
        bm.to_mesh(obj.data)
        bm.free()
    if before["triangles"] > row["budget"]:
        modifier = obj.modifiers.new("MeasuredLOD0Budget", "DECIMATE")
        modifier.ratio = max(0.01, row["budget"] / before["triangles"])
        modifier.use_collapse_triangulate = True
        modifier.delimit = {"UV", "MATERIAL", "SHARP"}
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    after = topology(obj.data)
    if after["degenerate_faces"]:
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.delete(bm, geom=[face for face in bm.faces if face.calc_area() < 1e-12], context="FACES_ONLY")
        bm.to_mesh(obj.data)
        bm.free()
        after = topology(obj.data)
    assert after["degenerate_faces"] == 0, row["source"]
    assert after["uv_layers"] == before["uv_layers"]
    assert after["triangles"] > 0
    points = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    low = [min(p[i] for p in points) for i in range(3)]
    high = [max(p[i] for p in points) for i in range(3)]
    trunk = None
    lower = row["source"].lower()
    if any(token in lower for token in ("/trees/", "elm", "maple")) and "bg_" not in lower:
        # FBX centimetres become Blender metres. Lower trunk samples exclude canopy cards.
        z0, height = low[2], high[2] - low[2]
        basal = [p for p in points if p.z <= z0 + min(1.5, height * .12)]
        if len(basal) >= 6 and height > 2:
            cx, cy = statistics.median(p.x for p in basal), statistics.median(p.y for p in basal)
            radii = sorted(math.hypot(p.x-cx, p.y-cy) for p in basal)
            radius = min(.60, max(.08, radii[int((len(radii)-1)*.8)] * 1.1))
            depth = min(height * .55, 3.5)
            bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=radius, depth=depth, location=(cx, cy, z0 + depth/2))
            collision = bpy.context.object
            collision.name = "UCX_" + row["name"] + "_00"
            trunk = {"radius_m": radius, "height_m": depth, "center_m": [cx, cy, z0+depth/2], "sides": 12}
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    bpy.ops.export_scene.fbx(filepath=str(filename), use_selection=True, object_types={"MESH"},
        add_leaf_bones=False, bake_anim=False, apply_unit_scale=True, axis_forward="-Y", axis_up="Z", use_tspace=True)
    results.append({**row, "blender": bpy.app.version_string, "before": before, "after": after,
        "bounds_m": {"low": low, "high": high}, "trunk_collision": trunk,
        "export": str(filename), "export_sha256": hashlib.sha256(filename.read_bytes()).hexdigest(),
        "production_approved": False, "remaining": ["Unreal round-trip bounds/material/collision", "near/distant visual and representative density acceptance"]})
    report.write_text(json.dumps(results, indent=2), encoding="utf-8")
    print("PF_TEMPLE_BLENDER", len(results), "/", len(records), row["name"], before["triangles"], after["triangles"], flush=True)
report.write_text(json.dumps(results, indent=2), encoding="utf-8")
print("PF_TEMPLE_BLENDER_COMPLETE", len(results), flush=True)
