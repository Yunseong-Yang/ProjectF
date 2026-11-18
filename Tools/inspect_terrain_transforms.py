"""Separate geometry round-trip changes from original cinematic level placement."""
from pathlib import Path
import json
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/TerrainReview"
rows = []
for row in json.loads((directory / "unreal-model-validation.json").read_text(encoding="utf-8")):
    source, derived = unreal.load_asset(row["source"]), unreal.load_asset(row["derived"])
    a, b = source.get_bounds(), derived.get_bounds()
    def bounds(value): return {"origin": [value.origin.x, value.origin.y, value.origin.z], "extent": [value.box_extent.x, value.box_extent.y, value.box_extent.z]}
    rows.append({"source": row["source"], "derived": row["derived"], "before": bounds(a), "after": bounds(b)})
relative = Path("Slay/Envs/Temple/temple_P.umap")
source = Path("D:/EpicGamesCache/VaultCache/Slay_5.7/data/Content") / relative
destination = root / "Content" / relative
if not destination.exists(): destination.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, destination)
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(["/Game/Slay/Envs/Temple"], True)
world = unreal.load_asset("/Game/Slay/Envs/Temple/temple_P"); assert world
streaming_rows = []
try:
    for streaming in world.get_editor_property("streaming_levels"):
        entry = {"class": streaming.get_class().get_name()}
        for name in ("world_asset", "level_transform", "should_be_visible_in_editor"):
            try: entry[name] = str(streaming.get_editor_property(name))
            except Exception as error: entry[name] = str(error)
        streaming_rows.append(entry)
except Exception as error:
    streaming_rows.append({"reflection_error": str(error), "world_methods": [m for m in dir(world) if "stream" in m or "level" in m]})
(directory / "transform-comparison.json").write_text(json.dumps({"models": rows, "original_world_streaming": streaming_rows,
    "production_approved": False}, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
