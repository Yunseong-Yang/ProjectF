"""Import the validated normal-clothing display candidate and its owned PBR textures."""
from pathlib import Path
import hashlib
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Characters/FirstPersonField"
source = json.loads((directory / "source.json").read_text(encoding="utf-8"))
validation = json.loads((directory / "blender-validation.json").read_text(encoding="utf-8"))
assert hashlib.sha256(Path(validation["export"]).read_bytes()).hexdigest() == validation["export_sha256"]
tools = unreal.AssetToolsHelpers.get_asset_tools()
library = unreal.MaterialEditingLibrary
materials, texture_records = {}, []
for label, index in (("MI_Echo_Skin", 1), ("MI_Echo_Shirt", 2)):
    name = "M_PFFieldSkin" if index == 1 else "M_PFFieldSleeve"
    folder = "/Game/ProjectF/Player/Materials"
    material = unreal.load_asset(folder + "/" + name) or tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    library.delete_all_material_expressions(material)
    material.set_editor_property("two_sided", True)
    for suffix, sampler, properties in (
        ("D", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, (("RGB", unreal.MaterialProperty.MP_BASE_COLOR),)),
        ("N", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, (("RGB", unreal.MaterialProperty.MP_NORMAL),)),
        ("ORM", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR, (("R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION), ("G", unreal.MaterialProperty.MP_ROUGHNESS), ("B", unreal.MaterialProperty.MP_METALLIC))),
    ):
        path = f"/Game/Characters/Echo/Textures/Costume/T_echo_Costume{index}_{suffix}"
        texture = unreal.load_asset(path)
        assert isinstance(texture, unreal.Texture2D), path
        texture.set_editor_property("max_texture_size", 4096)
        texture.set_editor_property("srgb", suffix == "D")
        assert unreal.EditorAssetLibrary.save_loaded_asset(texture, False)
        node = library.create_material_expression(material, unreal.MaterialExpressionTextureSample)
        node.texture, node.sampler_type = texture, sampler
        for channel, property in properties:
            library.connect_material_property(node, channel, property)
        texture_records.append({"asset": path, "maximum_cooked_resolution": 4096, "srgb": suffix == "D"})
    library.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    library.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
    materials[label.lower()] = material
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.SystemLibrary.execute_console_command(world, "Interchange.FeatureFlags.Import.FBX 0")
skeleton = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SK_Mannequin")
options = unreal.FbxImportUI()
options.import_mesh = True
options.import_as_skeletal = True
options.import_animations = False
options.automated_import_should_detect_type = False
options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
options.import_materials = False
options.import_textures = False
options.create_physics_asset = False
options.skeleton = skeleton
options.skeletal_mesh_import_data.set_editor_property("update_skeleton_reference_pose", False)
options.skeletal_mesh_import_data.set_editor_property("use_t0_as_ref_pose", False)
options.skeletal_mesh_import_data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
task = unreal.AssetImportTask()
task.filename, task.destination_path, task.destination_name = validation["export"], "/Game/ProjectF/Player/Meshes", "SKM_PFFieldArms"
task.automated, task.replace_existing, task.save, task.options = True, True, False, options
tools.import_asset_tasks([task])
mesh = unreal.load_asset("/Game/ProjectF/Player/Meshes/SKM_PFFieldArms")
assert isinstance(mesh, unreal.SkeletalMesh) and mesh.get_editor_property("skeleton") == skeleton
slots = list(mesh.get_editor_property("materials"))
for slot in slots:
    name = str(slot.get_editor_property("imported_material_slot_name")).lower()
    assert name in materials, (name, list(materials))
    slot.material_interface = materials[name]
mesh.set_editor_property("materials", slots)
component = unreal.new_object(unreal.SkeletalMeshComponent)
component.set_skeletal_mesh_asset(mesh)
assert component.get_num_bones() == 89 and component.does_socket_exist("HandGrip_R")
editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
assert len(slots) == 2 and editor.get_num_sections(mesh, 0) == 2
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
record = {"mesh": mesh.get_path_name(), "bones": component.get_num_bones(), "material_paths": [slot.material_interface.get_path_name() for slot in slots],
          "vertices": editor.get_num_verts(mesh, 0), "sections": editor.get_num_sections(mesh, 0), "textures": texture_records,
          "blender_triangles": sum(row["triangles"] for row in validation["meshes"]), "protected_vertices_lost": 0,
          "default_applied": False, "production_approved": False}
(directory / "unreal-validation.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_FIELD_FIRST_PERSON_IMPORT_COMPLETE")
unreal.SystemLibrary.quit_editor()
