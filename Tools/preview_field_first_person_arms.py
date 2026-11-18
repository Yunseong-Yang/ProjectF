"""Preview only the validated field arms; retain the prior owner mesh and remote body record."""
from pathlib import Path
import hashlib
import json
import shutil
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Characters/FirstPersonField"
validation = json.loads((directory / "unreal-validation.json").read_text(encoding="utf-8"))
assert validation["bones"] == 89 and validation["sections"] == 2
file = root / "Content/ProjectF/Player/BP_PFPlayer.uasset"
backup = directory / "BeforeFieldPlayer.uasset"
if not backup.exists():
    shutil.copy2(file, backup)
player = unreal.get_default_object(unreal.load_class(None, "/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"))
before_mesh = player.first_person_mesh.get_skeletal_mesh_asset().get_path_name()
assert before_mesh.startswith(("/Game/ProjectF/Player/Meshes/SKM_PFHumanFirstPersonArms.", "/Game/ProjectF/Player/Meshes/SKM_PFFieldArms.")), "Preserve an unrelated owner-view asset"
before = {part.get_name(): part.get_skeletal_mesh_asset().get_path_name() if part.get_skeletal_mesh_asset() else None
          for part in player.get_components_by_class(unreal.SkeletalMeshComponent)}
mesh = unreal.load_asset(validation["mesh"])
player.first_person_mesh.set_skeletal_mesh_asset(mesh)
player.first_person_mesh.set_editor_property("relative_location", unreal.Vector(0, 30, -10))
assert player.first_person_mesh.does_socket_exist("HandGrip_R")
for part in player.get_components_by_class(unreal.SkeletalMeshComponent):
    if part != player.first_person_mesh:
        assert before[part.get_name()] == (part.get_skeletal_mesh_asset().get_path_name() if part.get_skeletal_mesh_asset() else None)
blueprint = unreal.load_asset("/Game/ProjectF/Player/BP_PFPlayer")
assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False)
record = {"before_mesh": before_mesh, "view_mesh": mesh.get_path_name(), "remote_components": before,
          "blueprint_sha256": hashlib.sha256(file.read_bytes()).hexdigest(), "production_approved": False,
          "status": "Native visual preview; no final art acceptance"}
(directory / "preview-application.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_FIELD_FIRST_PERSON_PREVIEW_APPLIED")
unreal.SystemLibrary.quit_editor()
