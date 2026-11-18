"""Import inspected temple derivatives with measured bounds, LODs and collision roles."""
from pathlib import Path
import hashlib
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/Temple"
records = json.loads((directory / "blender-validation.json").read_text(encoding="utf-8"))
assert len(records) == len(json.loads((directory / "source-audit.json").read_text(encoding="utf-8"))["models"])
tools = unreal.AssetToolsHelpers.get_asset_tools()
editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
destination = "/Game/ProjectF/Environment/Temple/Meshes"
report = directory / "unreal-validation.json"
previous = {item["source"]: item for item in json.loads(report.read_text(encoding="utf-8"))} if report.exists() else {}
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
# Use the legacy FBX path for explicit UCX collision geometry.
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX 0")
results = []
for row in records:
    assert hashlib.sha256(Path(row["export"]).read_bytes()).hexdigest() == row["export_sha256"]
    assert hashlib.sha256(Path(row["source_file"]).read_bytes()).hexdigest() == row["source_file_sha256"]
    prior = previous.get(row["source"])
    if prior:
        cached = unreal.load_asset(prior["derived"])
        assert cached, "Missing completed derivative: " + prior["derived"]
        assert [cached.get_num_triangles(i) for i in range(editor.get_lod_count(cached))] == prior["lod_triangles"]
        assert len(cached.static_materials) == prior["materials"]
        assert editor.get_simple_collision_count(cached) == prior["simple_collision"]
        convex_count = editor.get_convex_collision_count(cached)
        if prior["collision_role"] == "Static prop convex hull":
            assert convex_count > 0, "Missing prop hull: " + row["name"]
        results.append({**prior, "convex_collision": convex_count, "export_sha256": row["export_sha256"], "completed_checkpoint_revalidated": True})
        continue
    task = unreal.AssetImportTask()
    task.filename, task.destination_path, task.destination_name = row["export"], destination, row["name"]
    task.automated, task.replace_existing, task.save = True, True, False
    options = unreal.FbxImportUI()
    options.import_mesh, options.import_as_skeletal = True, False
    options.automated_import_should_detect_type = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    options.import_materials, options.import_textures = False, False
    data = options.static_mesh_import_data
    data.combine_meshes, data.auto_generate_collision, data.one_convex_hull_per_ucx = True, False, True
    data.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
    data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
    task.options = options
    tools.import_asset_tasks([task])
    mesh = unreal.load_asset(destination + "/" + row["name"])
    source = unreal.load_asset(row["source"])
    assert mesh and source, row["name"]
    repairs = []
    for index, path in enumerate(row["materials"]):
        if not path:
            path = "/Game/ProjectF/Environment/Frontier/Materials/M_ElmBark" if "bush05" in row["name"].lower() else "/Game/Slay/Assets/MasterMaterials/CourtyardGround/M_CourtyardGround"
            repairs.append({"slot": index, "replacement": path, "reason": "Missing source material; corresponding bark/courtyard material"})
        material = unreal.load_asset(path)
        assert material and material.get_base_material(), path
        mesh.set_material(index, material)
    lower = row["source"].lower()
    foliage = any(token in lower for token in ("grass", "fern", "ivy", "bush", "elm", "maple", "/trees/"))
    background = any(token in lower for token in ("bg_", "background", "mountain", "skysphere"))
    architecture = "/arch/" in lower
    # Its concave opening contains the main stair route; a convex hull seals it.
    walkable = architecture or any(token in lower for token in ("cobble", "pavement", "paving", "ground", "floor", "stair", "riser", "mossyembankment_lower"))
    body = mesh.get_editor_property("body_setup")
    assert body
    if row["trunk_collision"] or walkable or not (background or foliage):
        body.set_editor_property("never_needs_cooked_collision_data", False)
    if row["trunk_collision"]:
        assert editor.get_convex_collision_count(mesh) == 1, "Trunk hull was not imported: " + row["name"]
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_SIMPLE_AS_COMPLEX)
        collision_role = "Single 12-sided trunk hull; canopy does not block"
    elif background or foliage:
        editor.remove_collisions(mesh)
        collision_role = "Visual-only background/low foliage"
    elif walkable:
        editor.remove_collisions(mesh)
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        collision_role = "Static architecture: exact reduced triangles preserve doors, stairs and openings"
    else:
        editor.remove_collisions(mesh)
        editor.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.NDOP26)
        collision_role = "Static prop convex hull"
    reduction = unreal.StaticMeshReductionOptions()
    reduction.auto_compute_lod_screen_size = False
    reduction.reduction_settings = [unreal.StaticMeshReductionSettings(percent_triangles=1, screen_size=1),
        unreal.StaticMeshReductionSettings(percent_triangles=.5, screen_size=.4),
        unreal.StaticMeshReductionSettings(percent_triangles=.2, screen_size=.15)]
    assert editor.set_lods(mesh, reduction) == 3
    a, b = source.get_bounds(), mesh.get_bounds()
    deltas = [abs(getattr(a.origin, axis)-getattr(b.origin, axis)) + abs(getattr(a.box_extent, axis)-getattr(b.box_extent, axis)) for axis in ("x", "y", "z")]
    tolerance = max(5, max(a.box_extent.x, a.box_extent.y, a.box_extent.z) * .03)
    assert max(deltas) <= tolerance, (row["name"], deltas, tolerance)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    results.append({"source": row["source"], "derived": mesh.get_path_name(), "source_file": row["source_file"],
        "source_file_sha256": row["source_file_sha256"], "export_sha256": row["export_sha256"], "bounds_error_cm": deltas, "bounds_tolerance_cm": tolerance,
        "source_triangles": row["lod_triangles"][0], "lod_triangles": [mesh.get_num_triangles(i) for i in range(editor.get_lod_count(mesh))],
        "vertices": editor.get_number_verts(mesh, 0), "placements": row["placements"], "materials": len(mesh.static_materials),
        "material_repairs": repairs, "simple_collision": editor.get_simple_collision_count(mesh), "convex_collision": editor.get_convex_collision_count(mesh), "collision_role": collision_role,
        "foliage": foliage, "background": background, "trunk_collision": row["trunk_collision"], "production_approved": False})
    (directory / "unreal-validation.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    unreal.log("PF_TEMPLE_IMPORT " + str(len(results)) + "/" + str(len(records)))
unreal.log("PF_TEMPLE_IMPORT_COMPLETE models=" + str(len(results)))
unreal.SystemLibrary.quit_editor()
