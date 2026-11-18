from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
manifest = json.loads((root / "ArtSource/Audio/provenance.json").read_text(encoding="utf-8"))
tools = unreal.AssetToolsHelpers.get_asset_tools()
attenuations = {}
for name, distance in (("SA_Combat", 10000), ("SA_Footstep", 1800)):
    attenuation = unreal.load_asset("/Game/ProjectF/Audio/" + name)
    if not attenuation: attenuation = tools.create_asset(name, "/Game/ProjectF/Audio", unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = attenuation.get_editor_property("attenuation")
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(150, 0, 0))
    settings.set_editor_property("falloff_distance", distance)
    attenuation.set_editor_property("attenuation", settings)
    unreal.EditorAssetLibrary.save_loaded_asset(attenuation)
    attenuations[name] = attenuation
for record in manifest:
    task = unreal.AssetImportTask()
    task.filename = str(root / "ArtSource/Audio" / (record["name"] + ".wav"))
    task.destination_path = "/Game/ProjectF/Audio"; task.destination_name = record["name"]
    task.automated = True; task.replace_existing = True; task.save = False
    tools.import_asset_tasks([task])
    sound = unreal.load_asset("/Game/ProjectF/Audio/" + record["name"])
    assert isinstance(sound, unreal.SoundWave), record
    sound.set_editor_property("looping", record["loop"])
    if record["name"] in ("S_RifleReport", "S_SuppressedReport", "S_AlarmCalling", "S_AlarmSent"): sound.set_editor_property("attenuation_settings", attenuations["SA_Combat"])
    if record["name"] == "S_DirtFootstep": sound.set_editor_property("attenuation_settings", attenuations["SA_Footstep"])
    unreal.EditorAssetLibrary.save_loaded_asset(sound)
    record["imported_asset"] = sound.get_path_name()
(root / "ArtSource/Audio/unreal-import.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
