"""Apply only the owner-view candidate for a recorded native visual review."""
from pathlib import Path
import hashlib
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Characters/FirstPersonHuman"
validation = json.loads((directory / "unreal-validation.json").read_text(encoding="utf-8"))
assert validation["bones"] == 89 and 1 <= len(validation["material_paths"]) <= 4
file = root / "Content/ProjectF/Player/BP_PFPlayer.uasset"
baseline = root / "Saved/Implementation/20261030_Completion/baseline/Content/ProjectF/Player/BP_PFPlayer.uasset"
prior = directory / "preview-application.json"
known = json.loads(prior.read_text(encoding="utf-8")) if prior.exists() else {}
current_hash = hashlib.sha256(file.read_bytes()).hexdigest()
assert file.read_bytes() == baseline.read_bytes() or current_hash == known.get("preview_blueprint_sha256"), "Preserve unrelated player Blueprint edits"
blueprint = unreal.load_asset("/Game/ProjectF/Player/BP_PFPlayer")
player = unreal.get_default_object(unreal.load_class(None, "/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"))
before = {component.get_name(): component.get_skeletal_mesh_asset().get_path_name() if component.get_skeletal_mesh_asset() else None
          for component in player.get_components_by_class(unreal.SkeletalMeshComponent)}
mesh = unreal.load_asset(validation["mesh"])
player.first_person_mesh.set_skeletal_mesh_asset(mesh)
player.first_person_mesh.set_editor_property("relative_location", unreal.Vector(0, 30, -10))
assert player.first_person_mesh.does_socket_exist("HandGrip_R")
after = {component.get_name(): component.get_skeletal_mesh_asset().get_path_name() if component.get_skeletal_mesh_asset() else None
         for component in player.get_components_by_class(unreal.SkeletalMeshComponent)}
assert all(before[name] == value for name, value in after.items() if name != player.first_person_mesh.get_name())
assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)
record = {"before": before, "after": after, "before_blueprint_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
          "preview_blueprint_sha256": hashlib.sha256(file.read_bytes()).hexdigest(), "owner_view_only": True,
          "owner_view_translation_local_cm": [0, 30, -10],
          "production_approved": False, "status": "Preview application; retain only after native visual and animation review"}
(directory / "preview-application.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_HUMAN_FIRST_PERSON_PREVIEW_APPLIED")
unreal.SystemLibrary.quit_editor()
