import bpy
import bmesh
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
directory = root / "ArtSource/Weapons/Rifle"
bpy.ops.object.select_all(action="SELECT"); bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=str(directory / "Rifle_Source.fbx"), use_anim=False)
records = []
for obj in bpy.context.scene.objects:
    if obj.type != "MESH": continue
    obj.data.calc_loop_triangles()
    bm = bmesh.new(); bm.from_mesh(obj.data)
    degenerates = sum(f.calc_area() < 1e-12 for f in bm.faces)
    nonmanifold = sum(not e.is_manifold for e in bm.edges)
    bm.free()
    records.append({"name": obj.name, "vertices": len(obj.data.vertices), "triangles": len(obj.data.loop_triangles),
                    "uv_layers": len(obj.data.uv_layers), "materials": len(obj.data.materials),
                    "degenerate_faces": degenerates, "nonmanifold_edges": nonmanifold})
assert records and all(r["degenerate_faces"] == 0 and r["uv_layers"] >= 1 for r in records)
bpy.ops.wm.save_as_mainfile(filepath=str(directory / "Rifle_Inspection.blend"))
(directory / "blender-validation.json").write_text(json.dumps({"blender": bpy.app.version_string,
    "source_sha256": hashlib.sha256((directory / "Rifle_Source.fbx").read_bytes()).hexdigest(),
    "models": records, "geometry_reduction_required": False, "production_approved": False}, indent=2), encoding="utf-8")
