"""Persist explicit instancing support and inspect derived meshes without reimport."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
records = json.loads((root / "ArtSource/Environment/unreal-derivative-validation.json").read_text(encoding="utf-8"))
library = unreal.MaterialEditingLibrary
seen, output = set(), []
for record in records:
    mesh = unreal.load_asset(record["asset"])
    assert mesh is not None, record["asset"]
    for slot in mesh.static_materials:
        interface = slot.material_interface
        assert interface is not None, (record["asset"], "missing material")
        base = interface.get_base_material()
        assert base is not None, interface.get_path_name()
        if base.get_path_name() not in seen:
            library.set_material_usage(base, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
            library.recompile_material(base); unreal.EditorAssetLibrary.save_loaded_asset(base)
            seen.add(base.get_path_name())
        if isinstance(interface, unreal.MaterialInstanceConstant) and interface.get_path_name() not in seen:
            library.set_material_usage_override(interface, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True, True)
            unreal.EditorAssetLibrary.save_loaded_asset(interface); seen.add(interface.get_path_name())
        output.append({"mesh": record["asset"], "material": interface.get_path_name(), "base": base.get_path_name()})
(root / "ArtSource/Environment/material-reference-validation.json").write_text(json.dumps(output, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
