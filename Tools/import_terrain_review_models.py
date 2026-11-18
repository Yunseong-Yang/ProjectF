"""Import separate inspected geometry, preserving artist normals and source packages."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/TerrainReview"
records = json.loads((directory / "blender-model-validation.json").read_text(encoding="utf-8"))
tools = unreal.AssetToolsHelpers.get_asset_tools()
subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
library = unreal.MaterialEditingLibrary
destination = "/Game/ProjectF/Environment/TerrainReview/Meshes"
output, seen = [], set()
for row in records:
    lower = row["name"].lower()
    reused = "/Game/ProjectF/Environment/Frontier/Meshes/SM_JapaneseMaple" if "japanesemaple" in lower else None
    if reused:
        mesh = unreal.load_asset(reused); assert mesh
    else:
        task = unreal.AssetImportTask(); task.filename = row["export"]; task.destination_path = destination; task.destination_name = row["name"]
        task.automated = True; task.replace_existing = True; task.save = False
        options = unreal.FbxImportUI(); options.import_mesh = True; options.import_as_skeletal = False
        options.automated_import_should_detect_type = False; options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
        options.import_materials = False; options.import_textures = False
        data = options.static_mesh_import_data; data.combine_meshes = True; data.auto_generate_collision = False
        data.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
        data.normal_import_method = unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS
        task.options = options; tools.import_asset_tasks([task])
        mesh = unreal.load_asset(destination + "/" + row["name"]); assert mesh
        repairs = []
        for index, path in enumerate(row["material_paths"]):
            material = unreal.load_asset(path) if path else None
            if (material is None or material.get_base_material() is None) and index == 0 and any(word in lower for word in ("bush", "elm", "tree")):
                material = unreal.load_asset("/Game/ProjectF/Environment/Frontier/Materials/M_ElmBark")
                repairs.append({"slot": index, "missing_source": path, "replacement": material.get_path_name() if material else None})
            assert material, (row["source"], path)
            mesh.set_material(index, material)
        subsystem.remove_collisions(mesh)
        if any(word in lower for word in ("mountain", "cliff", "terrain", "path", "stair")) and "bg_" not in lower:
            mesh.get_editor_property("body_setup").set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        elif not any(word in lower for word in ("grass", "fern", "ivy", "elm", "tree", "bush", "bg_")):
            subsystem.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.NDOP26)
            if subsystem.get_simple_collision_count(mesh) == 0: subsystem.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
        reduction = unreal.StaticMeshReductionOptions(); reduction.auto_compute_lod_screen_size = False
        reduction.reduction_settings = [unreal.StaticMeshReductionSettings(percent_triangles=1, screen_size=1), unreal.StaticMeshReductionSettings(percent_triangles=.5, screen_size=.4), unreal.StaticMeshReductionSettings(percent_triangles=.2, screen_size=.15)]
        subsystem.set_lods(mesh, reduction)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    for slot in mesh.static_materials:
        interface = slot.material_interface; assert interface
        base = interface.get_base_material()
        if base and base.get_path_name() not in seen:
            library.set_material_usage(base, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
            library.recompile_material(base); unreal.EditorAssetLibrary.save_loaded_asset(base); seen.add(base.get_path_name())
    output.append({"source": row["source"], "derived": mesh.get_path_name(), "reused_existing_inspected_model": bool(reused),
        "material_repairs": repairs if not reused else [],
        "original_triangles": row["original_triangles"], "selected_source_lod": row["source_lod"],
        "lod_triangles": [mesh.get_num_triangles(i) for i in range(subsystem.get_lod_count(mesh))],
        "vertices": subsystem.get_number_verts(mesh, 0), "materials": len(mesh.static_materials),
        "simple_collision": subsystem.get_simple_collision_count(mesh), "production_approved": False,
        "remaining": ["tree trunk collision where needed", "scene comparison", "density memory/frame acceptance"]})
    (directory / "unreal-model-validation.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
unreal.log("PF_TERRAIN_DERIVATIVES_IMPORTED models=" + str(len(output)))
unreal.SystemLibrary.quit_editor()
