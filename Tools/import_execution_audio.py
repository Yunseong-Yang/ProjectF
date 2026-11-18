"""Import licensed PCM into separate game sound assets with short melee attenuation."""
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
for name in ("S_ExecutionWhoosh", "S_ExecutionImpact"):
    task = unreal.AssetImportTask(); task.filename = str(root / "ArtSource/Audio/Execution" / (name + ".wav"))
    task.destination_path = "/Game/ProjectF/Audio"; task.destination_name = name
    task.automated = True; task.replace_existing = True; task.save = False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    sound = unreal.load_asset(task.destination_path + "/" + name); assert isinstance(sound, unreal.SoundWave)
    attenuation = unreal.load_asset("/Game/ProjectF/Audio/SA_Footstep"); assert attenuation
    sound.set_editor_property("attenuation_settings", attenuation)
    unreal.EditorAssetLibrary.save_loaded_asset(sound)
unreal.SystemLibrary.quit_editor()
