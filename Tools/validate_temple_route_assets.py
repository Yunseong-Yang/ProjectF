"""Revalidate the changed owned mesh and the preserved locally optimized source library."""
from pathlib import Path
import hashlib
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
output = Path((root / "Saved/Implementation/current-continuation-path.txt").read_text(encoding="utf-8"))
directory = root / "ArtSource/Environment/Temple"
records = json.loads((directory / "unreal-validation.json").read_text(encoding="utf-8"))
source_errors = [row["source_file"] for row in records
                 if hashlib.sha256(Path(row["source_file"]).read_bytes()).hexdigest() != row["source_file_sha256"]]
blender = json.loads((directory / "blender-validation.json").read_text(encoding="utf-8"))
export_errors = [row["export"] for row in blender
                 if hashlib.sha256(Path(row["export"]).read_bytes()).hexdigest() != row["export_sha256"]]
row = next(row for row in records if "MossyEmbankment_Lower" in row["derived"])
mesh = unreal.load_asset(row["derived"])
editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
triangles = [mesh.get_num_triangles(i) for i in range(editor.get_lod_count(mesh))]
convex = editor.get_convex_collision_count(mesh)
complexity = str(editor.get_collision_complexity(mesh))
record = {"mesh": row["derived"], "lod_triangles": triangles, "materials": len(mesh.static_materials),
          "convex_hulls": convex, "collision": complexity, "source_models_checked": len(records),
          "source_errors": source_errors, "blender_exports_checked": len(blender), "export_errors": export_errors,
          "production_approved": False}
record["passed"] = (len(records) == len(blender) == 233 and not source_errors and not export_errors
                    and triangles == row["lod_triangles"] and len(mesh.static_materials) == row["materials"]
                    and convex == 0 and editor.get_simple_collision_count(mesh) == 0
                    and editor.get_collision_complexity(mesh) == unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
(output / "temple-route-asset-validation.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
assert record["passed"], record
unreal.log("PF_TEMPLE_ROUTE_ASSETS_VALID")
unreal.SystemLibrary.quit_editor()
