"""Distance-aware terrain albedo and normals; preserve gameplay geometry."""
from pathlib import Path
import json
import unreal

library = unreal.MaterialEditingLibrary
mat = unreal.load_asset("/Game/ProjectF/Environment/Frontier/Materials/M_FrontierGround")
assert mat
library.delete_all_material_expressions(mat)

def node(kind, **properties):
    result = library.create_material_expression(mat, kind)
    for key, value in properties.items(): result.set_editor_property(key, value)
    return result

def wire(a, output, b, input_name):
    connected = library.connect_material_expressions(a, output, b, input_name)
    if not connected and input_name == "Input":
        connected = library.connect_material_expressions(a, output, b, "")
    assert connected, (a.get_class().get_name(), b.get_class().get_name(), input_name)

def blend(a, b, weight, channel="", a_output="", b_output=""):
    result = node(unreal.MaterialExpressionLinearInterpolate)
    wire(a, a_output, result, "A"); wire(b, b_output, result, "B"); wire(weight, channel, result, "Alpha")
    return result

def sample(path, normal=False, coordinates=None):
    texture = unreal.load_asset(path); assert texture, path
    virtual = texture.get_editor_property("virtual_texture_streaming")
    kind = (unreal.MaterialSamplerType.SAMPLERTYPE_VIRTUAL_NORMAL if virtual else unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL) if normal else (unreal.MaterialSamplerType.SAMPLERTYPE_VIRTUAL_COLOR if virtual else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    result = node(unreal.MaterialExpressionTextureSample, texture=texture, sampler_type=kind)
    if coordinates: wire(coordinates, "", result, "UVs")
    return result

weights = node(unreal.MaterialExpressionVertexColor)
depth = node(unreal.MaterialExpressionPixelDepth)
fade_scale = node(unreal.MaterialExpressionMultiply, const_b=1 / 18000)
wire(depth, "", fade_scale, "A")
fade_clamp = node(unreal.MaterialExpressionSaturate); wire(fade_scale, "", fade_clamp, "Input")
fade = node(unreal.MaterialExpressionMultiply, const_b=.75); wire(fade_clamp, "", fade, "A")
world = node(unreal.MaterialExpressionWorldPosition)
xy = node(unreal.MaterialExpressionComponentMask, r=True, g=True); wire(world, "", xy, "Input")
macro_uv = node(unreal.MaterialExpressionMultiply, const_b=1 / 7500); wire(xy, "", macro_uv, "A")
grass_path = "/Game/ExampleContent/Landscapes/Textures/T_Wild_Grass_xbreagf_4K_"
macro_texture = sample(grass_path + "D", coordinates=macro_uv)
variation_scale = node(unreal.MaterialExpressionMultiply, const_b=.65); wire(macro_texture, "R", variation_scale, "A")
variation = node(unreal.MaterialExpressionAdd, const_b=.7); wire(variation_scale, "", variation, "A")
colors = [node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(*color, 1)) for color in ((.105, .14, .055), (.17, .135, .08), (.16, .155, .12))]
macro_road = blend(colors[0], colors[1], weights, "R")
macro = blend(macro_road, colors[2], weights, "G")
macro_color = node(unreal.MaterialExpressionMultiply); wire(macro, "", macro_color, "A"); wire(variation, "", macro_color, "B")
for normal in (False, True):
    grass = sample(grass_path + ("N" if normal else "D"), normal)
    dirt = sample("/Game/Slay/Assets/MasterMaterials/MainLandscape/T_DustyGround_" + ("N" if normal else "BC"), normal)
    rock = sample("/Game/ExampleContent/Landscapes/Textures/T_Granite_Rock_visscjf_4K_" + ("N" if normal else "D"), normal)
    road = blend(grass, dirt, weights, "R", "RGB", "RGB")
    detail = blend(road, rock, weights, "G", "", "RGB")
    far = node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0, 0, 1, 1)) if normal else macro_color
    final = blend(detail, far, fade)
    library.connect_material_property(final, "", unreal.MaterialProperty.MP_NORMAL if normal else unreal.MaterialProperty.MP_BASE_COLOR)
library.connect_material_property(node(unreal.MaterialExpressionConstant, r=.9), "", unreal.MaterialProperty.MP_ROUGHNESS)
library.recompile_material(mat)
assert unreal.EditorAssetLibrary.save_loaded_asset(mat)
root = Path(unreal.Paths.project_dir()).resolve()
(root / "ArtSource/Environment/terrain-material-validation.json").write_text(json.dumps({
    "material": mat.get_path_name(), "distance_transition_cm": 18000,
    "world_macro_texture_cm": 7500, "texture_samples": 7, "geometry_changed": False,
    "production_approved": False, "required": ["near/far visual", "GPU and memory representative scenes"]
}, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
