"""Create a dedicated field-glove material; preserve the existing guard materials."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Characters/FirstPersonHuman"
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.MaterialEditingLibrary
folder, name = "/Game/ProjectF/Player/Materials", "M_PFFieldGloves"
material = unreal.load_asset(folder + "/" + name) or tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
library.delete_all_material_expressions(material)
material.set_editor_property("two_sided", True)
texture_root = "/Game/Characters/Paragon/Heroes/TwinBlast/Textures/"
mask = library.create_material_expression(material, unreal.MaterialExpressionTextureSample)
mask.texture = unreal.load_asset(texture_root + "T_TwinBlast_ActionMovieArms_RGB1")
assert mask.texture
mask.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if mask.texture.get_editor_property("srgb") else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
wear = library.create_material_expression(material, unreal.MaterialExpressionMultiply)
wear.set_editor_property("const_b", .25)
library.connect_material_expressions(mask, "R", wear, "A")
bias = library.create_material_expression(material, unreal.MaterialExpressionAdd)
bias.set_editor_property("const_b", .75)
library.connect_material_expressions(wear, "", bias, "A")
tint = library.create_material_expression(material, unreal.MaterialExpressionVectorParameter)
tint.set_editor_property("parameter_name", "FieldGloveTint")
tint.set_editor_property("default_value", unreal.LinearColor(.025, .032, .024, 1))
base = library.create_material_expression(material, unreal.MaterialExpressionMultiply)
library.connect_material_expressions(bias, "", base, "A")
library.connect_material_expressions(tint, "", base, "B")
library.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
normal = library.create_material_expression(material, unreal.MaterialExpressionTextureSample)
normal.texture = unreal.load_asset(texture_root + "T_TwinBlast_Arms_N")
assert normal.texture
normal.sampler_type = unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL
library.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
rough = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
rough.set_editor_property("parameter_name", "FieldGloveRoughness")
rough.set_editor_property("default_value", .72)
library.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
specular = library.create_material_expression(material, unreal.MaterialExpressionConstant)
specular.r = .3
library.connect_material_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
library.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
library.recompile_material(material)
assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
mesh = unreal.load_asset("/Game/ProjectF/Player/Meshes/SKM_PFHumanFirstPersonArms")
slots = list(mesh.get_editor_property("materials"))
assert len(slots) == 1
slots[0].material_interface = material
mesh.set_editor_property("materials", slots)
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
record = {"material": material.get_path_name(), "mask": mask.texture.get_path_name(), "normal": normal.texture.get_path_name(),
          "tint_linear_rgb": [.025, .032, .024], "roughness": .72, "specular": .3,
          "slots": 1, "guard_materials_modified": False, "production_approved": False}
record["two_sided_owner_geometry"] = True
(directory / "view-material.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_FIRST_PERSON_FIELD_MATERIAL_COMPLETE")
unreal.SystemLibrary.quit_editor()
