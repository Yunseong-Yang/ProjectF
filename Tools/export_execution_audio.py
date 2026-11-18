"""Read already downloaded impact/cloth sounds; export PCM locally in the workbench."""
from pathlib import Path
import hashlib
import json
import shutil
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
root = project.parents[1]
cache = Path("D:/EpicGamesCache/VaultCache/Lyra_5.8/data/Content")
directory = root / "ArtSource/Audio/Execution"; directory.mkdir(parents=True, exist_ok=True)
records = []
for name, original in (("S_ExecutionWhoosh", "Lyra_MeleeWhoosh_01"), ("S_ExecutionImpact", "Lyra_MeleeImpact_01")):
    relative = Path("Audio/Sounds/SFX") / (original + ".uasset")
    source, target = cache / relative, project / "Content" / relative
    assert source.is_file()
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists(): assert target.read_bytes() == source.read_bytes()
    shutil.copy2(source, target)
    for suffix in (".uexp", ".ubulk", ".uptnl"):
        if source.with_suffix(suffix).exists(): shutil.copy2(source.with_suffix(suffix), target.with_suffix(suffix))
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/Audio/Sounds/SFX"], True)
    sound = unreal.load_asset("/Game/Audio/Sounds/SFX/" + original); assert isinstance(sound, unreal.SoundWave)
    filename = directory / (name + ".wav")
    export = unreal.AssetExportTask(); export.object = sound; export.filename = str(filename)
    export.automated = True; export.prompt = False; export.replace_identical = True; export.exporter = unreal.SoundExporterWAV()
    assert unreal.Exporter.run_asset_export_task(export)
    records.append({"source": str(source), "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "wave": str(filename), "wave_sha256": hashlib.sha256(filename.read_bytes()).hexdigest()})
(directory / "source-manifest.json").write_text(json.dumps({"files": records, "network_downloads": False, "purchases": False,
    "permission": "https://dev.epicgames.com/documentation/unreal-engine/sample-game-projects-for-unreal-engine?lang=en-US",
    "current_account_entitlement_checked": False, "public_sample_permission_checked": True}, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
