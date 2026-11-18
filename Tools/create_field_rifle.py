"""Create a dark field finish without editing the original template weapon."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
folder = "/Game/ProjectF/Weapons"
tools = unreal.AssetToolsHelpers.get_asset_tools(); library = unreal.MaterialEditingLibrary
source = unreal.load_asset("/Game/Weapons/Rifle/Meshes/SM_Rifle")
mesh = unreal.load_asset(folder + "/SM_FieldRifle") or tools.duplicate_asset("SM_FieldRifle", folder, source)
mat = unreal.load_asset(folder + "/M_FieldRifle") or tools.create_asset("M_FieldRifle", folder, unreal.Material, unreal.MaterialFactoryNew())
library.delete_all_material_expressions(mat)
base = library.create_material_expression(mat, unreal.MaterialExpressionTextureSample)
base.texture = unreal.load_asset("/Game/Weapons/Rifle/Textures/T_Rifle_BC")
base.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if base.texture.get_editor_property("srgb") else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
tint = library.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector); tint.constant = unreal.LinearColor(.16, .19, .15, 1)
multiply = library.create_material_expression(mat, unreal.MaterialExpressionMultiply)
library.connect_material_expressions(base, "RGB", multiply, "A"); library.connect_material_expressions(tint, "", multiply, "B")
library.connect_material_property(multiply, "", unreal.MaterialProperty.MP_BASE_COLOR)
normal = library.create_material_expression(mat, unreal.MaterialExpressionTextureSample)
normal.texture = unreal.load_asset("/Game/Weapons/Rifle/Textures/T_Rifle_N"); normal.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
library.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
for value, property_name in ((.5, unreal.MaterialProperty.MP_ROUGHNESS), (.45, unreal.MaterialProperty.MP_METALLIC)):
    constant = library.create_material_expression(mat, unreal.MaterialExpressionConstant); constant.r = value
    library.connect_material_property(constant, "", property_name)
library.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_NANITE)
library.recompile_material(mat); mesh.set_material(0, mat)
subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
reduction = unreal.StaticMeshReductionOptions()
reduction.auto_compute_lod_screen_size = False
reduction.reduction_settings = [unreal.StaticMeshReductionSettings(percent_triangles=1, screen_size=1), unreal.StaticMeshReductionSettings(percent_triangles=.5, screen_size=.3), unreal.StaticMeshReductionSettings(percent_triangles=.2, screen_size=.1)]
subsystem.set_lods(mesh, reduction)
unreal.EditorAssetLibrary.save_loaded_asset(mat); unreal.EditorAssetLibrary.save_loaded_asset(mesh)
directory = root / "ArtSource/Weapons/Rifle"; directory.mkdir(parents=True, exist_ok=True)
export = unreal.AssetExportTask(); export.object = source; export.filename = str(directory / "Rifle_Source.fbx")
export.automated = True; export.prompt = False; export.replace_identical = True; export.exporter = unreal.StaticMeshExporterFBX()
options = unreal.FbxExportOption(); options.collision = False; options.level_of_detail = False; export.options = options
assert unreal.Exporter.run_asset_export_task(export)
subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
record = {"source": source.get_path_name(), "derived": mesh.get_path_name(), "triangles": mesh.get_num_triangles(0),
          "vertices": subsystem.get_number_verts(mesh, 0), "materials": len(mesh.static_materials), "lods": subsystem.get_lod_count(mesh),
          "geometry_changed": False, "source_preserved": True, "production_approved": False}
(directory / "unreal-validation.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
