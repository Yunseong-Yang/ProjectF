from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
task = unreal.AssetImportTask()
task.filename = str(root / "ArtSource/Environment/T_FrontierMap.png")
task.destination_path = "/Game/ProjectF/UI"; task.destination_name = "T_FrontierMap"
task.automated = True; task.replace_existing = True; task.save = False
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture = unreal.load_asset("/Game/ProjectF/UI/T_FrontierMap")
assert isinstance(texture, unreal.Texture2D)
texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
unreal.EditorAssetLibrary.save_loaded_asset(texture)
unreal.SystemLibrary.quit_editor()
