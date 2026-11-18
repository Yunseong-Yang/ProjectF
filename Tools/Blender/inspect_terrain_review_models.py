"""Inspect selected artist geometry and preserve alpha-card normals when no cleanup is needed."""
from pathlib import Path
import hashlib
import json
import bpy
import bmesh

root = Path(__file__).resolve().parents[2]
directory = root / "ArtSource/Environment/TerrainReview"
exports = directory / "ValidatedExports"; exports.mkdir(exist_ok=True)
records = json.loads((directory / "exported-models.json").read_text(encoding="utf-8"))
results = []
for row in records:
    bpy.ops.object.select_all(action="SELECT"); bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.fbx(filepath=row["fbx"], use_anim=False)
    meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]; assert meshes
    stats = []
    for obj in meshes:
        obj.data.calc_loop_triangles()
        bm = bmesh.new(); bm.from_mesh(obj.data)
        degenerate = sum(face.calc_area() < 1e-12 for face in bm.faces)
        open_edges = sum(not edge.is_manifold for edge in bm.edges)
        if degenerate:
            bmesh.ops.dissolve_degenerate(bm, dist=1e-7, edges=list(bm.edges)); bm.to_mesh(obj.data)
        bm.free()
        stats.append({"vertices": len(obj.data.vertices), "triangles": len(obj.data.loop_triangles), "materials": len(obj.data.materials),
            "uv_layers": len(obj.data.uv_layers), "custom_normals": obj.data.has_custom_normals, "degenerate_faces_before": degenerate,
            "nonmanifold_edges": open_edges, "normals_preserved_without_topology_rewrite": degenerate == 0})
        assert len(obj.data.uv_layers) >= 1
    bpy.ops.wm.save_as_mainfile(filepath=str(directory / (row["name"] + "_Validated.blend")))
    filename = exports / (row["name"] + ".fbx")
    bpy.ops.export_scene.fbx(filepath=str(filename), use_selection=False, object_types={"MESH"}, add_leaf_bones=False, bake_anim=False,
        apply_unit_scale=True, axis_forward="-Y", axis_up="Z", use_tspace=True)
    results.append({**row, "blender": bpy.app.version_string, "models": stats, "export": str(filename),
        "export_sha256": hashlib.sha256(filename.read_bytes()).hexdigest(), "production_approved": False,
        "required": ["Unreal normals/material/LOD/collision comparison", "representative density", "visual acceptance"]})
    (directory / "blender-model-validation.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
print("PF_TERRAIN_MODELS_INSPECTED", len(results))
