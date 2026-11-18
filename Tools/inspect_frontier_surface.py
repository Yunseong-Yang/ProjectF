"""Compare deterministic triangle interpolation against actual editor collision."""
from pathlib import Path
import json
import random
import sys
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(root / "Tools"))
from frontier_layout import surface_height

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/ProjectF/Maps/L_MountainFrontier")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
ignored = [a for a in actors if not (isinstance(a, unreal.StaticMeshActor) and a.static_mesh_component.static_mesh and a.static_mesh_component.static_mesh.get_name().startswith("SM_FrontierGround"))]
random.seed(1030)
rows = []
if "PFInspectSurfaceAPI" in unreal.SystemLibrary.get_command_line():
    (root / "Saved/Implementation/20261030_Continuation/surface-api.json").write_text(json.dumps({
        "hit_result": unreal.HitResult.__doc__, "methods": dir(unreal.HitResult),
        "trace": unreal.SystemLibrary.line_trace_single.__doc__}, indent=2), encoding="utf-8")
    unreal.SystemLibrary.quit_editor()
    raise SystemExit(0)
for _ in range(128):
    x, y = random.uniform(-470, 470), random.uniform(-470, 470)
    result = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x * 100, y * 100, 40000), unreal.Vector(x * 100, y * 100, -5000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, ignored, unreal.DrawDebugTrace.NONE, True)
    if isinstance(result, tuple):
        success, hit = result
    else:
        hit, success = result, result is not None
    assert success, (x, y, str(result))
    # NativeBreakFunc is exposed as the struct's tuple conversion in this engine.
    actual = hit.to_tuple()[5].z
    expected = surface_height(x, y) * 100
    rows.append({"x_cm": x * 100, "y_cm": y * 100, "expected_z_cm": expected, "actual_z_cm": actual, "error_cm": abs(actual - expected)})
record = {"map": "/Game/ProjectF/Maps/L_MountainFrontier", "samples": len(rows), "max_error_cm": max(r["error_cm"] for r in rows),
          "tolerance_cm": 2, "rows": rows}
record["passed"] = record["max_error_cm"] <= record["tolerance_cm"]
(root / "ArtSource/Environment/terrain-collision-validation.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
assert record["passed"], record["max_error_cm"]
unreal.log("PF_TERRAIN_COLLISION samples=128 max_error_cm=" + str(record["max_error_cm"]))
unreal.SystemLibrary.quit_editor()
