"""Import Blender-validated derivatives and build shared PBR materials locally."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource" / "Environment"
exports = directory / "OptimizedExports"
tools = unreal.AssetToolsHelpers.get_asset_tools()
meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
library = unreal.MaterialEditingLibrary
destination = "/Game/ProjectF/Environment/Frontier"


def material(name):
    path = destination + "/Materials/" + name
    asset = unreal.load_asset(path)
    if not asset:
        asset = tools.create_asset(name, destination + "/Materials", unreal.Material, unreal.MaterialFactoryNew())
    library.delete_all_material_expressions(asset)
    return asset


def node(mat, kind, **properties):
    result = library.create_material_expression(mat, kind)
    for key, value in properties.items(): result.set_editor_property(key, value)
    return result


def connect(a, output, b, input_name):
    assert library.connect_material_expressions(a, output, b, input_name), (a, output, b, input_name)


def texture(mat, path, normal=False):
    sample = node(mat, unreal.MaterialExpressionTextureSample, texture=unreal.load_asset(path))
    assert sample.texture, path
    virtual = sample.texture.get_editor_property("virtual_texture_streaming")
    sample.set_editor_property("sampler_type", (unreal.MaterialSamplerType.SAMPLERTYPE_VIRTUAL_NORMAL if virtual else unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL) if normal else (unreal.MaterialSamplerType.SAMPLERTYPE_VIRTUAL_COLOR if virtual else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR))
    return sample


def finish(mat):
    library.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat


def surface(name, color, diffuse=None, normal=None, roughness=.8, metallic=0):
    mat = material(name)
    base = texture(mat, diffuse) if diffuse else node(mat, unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(*color, 1))
    library.connect_material_property(base, "RGB" if diffuse else "", unreal.MaterialProperty.MP_BASE_COLOR)
    if normal: library.connect_material_property(texture(mat, normal, True), "RGB", unreal.MaterialProperty.MP_NORMAL)
    library.connect_material_property(node(mat, unreal.MaterialExpressionConstant, r=roughness), "", unreal.MaterialProperty.MP_ROUGHNESS)
    library.connect_material_property(node(mat, unreal.MaterialExpressionConstant, r=metallic), "", unreal.MaterialProperty.MP_METALLIC)
    return finish(mat)


def terrain_material():
    mat = material("M_FrontierGround")
    weights = node(mat, unreal.MaterialExpressionVertexColor)
    for normal in (False, True):
        suffix = "N" if normal else "D"
        grass = texture(mat, "/Game/ExampleContent/Landscapes/Textures/T_Wild_Grass_xbreagf_4K_" + suffix, normal)
        dirt = texture(mat, "/Game/Slay/Assets/MasterMaterials/MainLandscape/T_DustyGround_" + ("N" if normal else "BC"), normal)
        rock = texture(mat, "/Game/ExampleContent/Landscapes/Textures/T_Granite_Rock_visscjf_4K_" + suffix, normal)
        road = node(mat, unreal.MaterialExpressionLinearInterpolate)
        connect(grass, "RGB", road, "A"); connect(dirt, "RGB", road, "B"); connect(weights, "R", road, "Alpha")
        slope = node(mat, unreal.MaterialExpressionLinearInterpolate)
        connect(road, "", slope, "A"); connect(rock, "RGB", slope, "B"); connect(weights, "G", slope, "Alpha")
        library.connect_material_property(slope, "", unreal.MaterialProperty.MP_NORMAL if normal else unreal.MaterialProperty.MP_BASE_COLOR)
    library.connect_material_property(node(mat, unreal.MaterialExpressionConstant, r=.88), "", unreal.MaterialProperty.MP_ROUGHNESS)
    return finish(mat)


ground = terrain_material()
plaster = surface("M_Plaster", (.6, .56, .45), "/Game/Slay/Assets/Arch/SWDorm/Textures/T_WornWallPlaster_BC", "/Game/Slay/Assets/Arch/SWDorm/Textures/T_WornWallPlaster_N")
concrete = surface("M_Concrete", (.4, .4, .37), "/Game/Slay/Assets/Arch/SWDorm/Textures/T_RoughConcrete_BC", "/Game/Slay/Assets/Arch/SWDorm/Textures/T_RoughConcrete_N")
metal = surface("M_OxidizedMetal", (.12, .18, .17), roughness=.6, metallic=.65)
canvas = surface("M_Canvas", (.28, .23, .13), roughness=.92)
wood = unreal.load_asset("/Game/Slay/Assets/SetDec/WoodenCrate/MI_WoodenCrate")
bark = surface("M_ElmBark", (.2, .14, .08), "/Game/Slay/Assets/Trees/AmurCork/T_TreeBark02_BC", "/Game/Slay/Assets/Trees/AmurCork/T_TreeBark02_N")
source_records = {r["name"]: r for r in json.loads((directory / "blender-validation.json").read_text(encoding="utf-8"))}
architecture = {r["name"]: r for r in json.loads((directory / "architecture-validation.json").read_text(encoding="utf-8"))}
materials = {"Plaster": plaster, "Concrete": concrete, "OxidizedMetal": metal, "Canvas": canvas, "WeatheredWood": wood}
records = []
terrain_only = "PFImportTerrainOnly" in unreal.SystemLibrary.get_command_line()
radio_only = "PFImportRadioOnly" in unreal.SystemLibrary.get_command_line()
if terrain_only:
    records = [r for r in json.loads((directory / "unreal-derivative-validation.json").read_text(encoding="utf-8")) if not r["name"].startswith("SM_FrontierGround")]
if radio_only:
    records = [r for r in json.loads((directory / "unreal-derivative-validation.json").read_text(encoding="utf-8")) if r["name"] != "SM_RadioMast"]
for fbx in sorted(exports.glob("*.fbx")):
    name = fbx.stem
    if terrain_only and not name.startswith("SM_FrontierGround"): continue
    if radio_only and name != "SM_RadioMast": continue
    path = destination + "/Meshes/" + name
    task = unreal.AssetImportTask()
    task.filename = str(fbx); task.destination_path = destination + "/Meshes"; task.destination_name = name
    task.automated = True; task.replace_existing = True; task.save = False
    options = unreal.FbxImportUI()
    options.import_mesh = True; options.import_as_skeletal = False
    options.automated_import_should_detect_type = False; options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    options.import_materials = False; options.import_textures = False
    options.static_mesh_import_data.combine_meshes = True
    options.static_mesh_import_data.auto_generate_collision = False
    options.static_mesh_import_data.vertex_color_import_option = unreal.VertexColorImportOption.REPLACE
    task.options = options
    tools.import_asset_tasks([task])
    mesh = unreal.load_asset(path); assert isinstance(mesh, unreal.StaticMesh), (fbx, task.imported_object_paths)
    is_ground = name.startswith("SM_FrontierGround")
    is_foliage = any(word in name for word in ("Elm", "Maple", "Fern", "Grass"))
    if is_ground: mesh.set_material(0, ground)
    elif name in architecture: mesh.set_material(0, materials[architecture[name]["material"]])
    else:
        for index, material_path in enumerate(source_records[name]["material_paths"]):
            mesh.set_material(index, bark if name == "SM_Elm08" and index == 0 else unreal.load_asset(material_path))
    meshes.remove_collisions(mesh)
    if is_ground or name in ("SM_Watchtower", "SM_RadioMast", "SM_PlasterDoorway", "SM_PlasterWindow"):
        mesh.get_editor_property("body_setup").set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    elif not is_foliage and "Mountains" not in name:
        meshes.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX if name in architecture or "Crate" in name else unreal.ScriptCollisionShapeType.NDOP26)
    elif "Elm" in name:
        # Authored trunk collision is placed separately; alpha leaves never become solid walls.
        pass
    if not is_ground:
        reduction = unreal.StaticMeshReductionOptions()
        reduction.auto_compute_lod_screen_size = False
        reduction.reduction_settings = [unreal.StaticMeshReductionSettings(percent_triangles=1, screen_size=1),
                                         unreal.StaticMeshReductionSettings(percent_triangles=.5, screen_size=.35),
                                         unreal.StaticMeshReductionSettings(percent_triangles=.18, screen_size=.12)]
        meshes.set_lods(mesh, reduction)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    record = {"asset": path, "name": name, "lod_triangles": [mesh.get_num_triangles(i) for i in range(meshes.get_lod_count(mesh))],
              "vertices_lod0": meshes.get_number_verts(mesh, 0), "materials": len(mesh.static_materials),
              "simple_collision": meshes.get_simple_collision_count(mesh), "foliage": is_foliage,
              "status": "visual_and_density_validation_pending"}
    records.append(record); unreal.log("PF_FRONTIER_IMPORT " + json.dumps(record))
material_usage_saved = set()
for record in records:
    mesh = unreal.load_asset(destination + "/Meshes/" + record["name"])
    for slot in mesh.static_materials:
        if slot.material_interface:
            mat = slot.material_interface.get_base_material()
            if mat and mat.get_path_name() not in material_usage_saved:
                library.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
                library.recompile_material(mat); unreal.EditorAssetLibrary.save_loaded_asset(mat)
                material_usage_saved.add(mat.get_path_name())
            interface = slot.material_interface
            if isinstance(interface, unreal.MaterialInstanceConstant) and interface.get_path_name() not in material_usage_saved:
                library.set_material_usage_override(interface, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True, True)
                unreal.EditorAssetLibrary.save_loaded_asset(interface); material_usage_saved.add(interface.get_path_name())
for path in unreal.EditorAssetLibrary.list_assets("/Game/Slay/Assets", recursive=True):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.Texture2D):
        asset.set_editor_property("max_texture_size", 1024 if any(w in path for w in ("Grass", "Fern", "Billboard")) else 2048)
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
(directory / "unreal-derivative-validation.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
unreal.log("PF_FRONTIER_IMPORT_COMPLETE")
unreal.SystemLibrary.quit_editor()
