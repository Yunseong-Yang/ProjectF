"""Read back the saved owner-view mesh, materials, framing and untouched remote body."""
from pathlib import Path
import hashlib
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Characters/FirstPersonHuman"
player = unreal.get_default_object(unreal.load_class(None, "/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"))
mesh = player.first_person_mesh.get_skeletal_mesh_asset()
assert mesh.get_path_name().startswith("/Game/ProjectF/Player/Meshes/SKM_PFHumanFirstPersonArms.")
assert player.first_person_mesh.get_num_bones() == 89 and player.first_person_mesh.does_socket_exist("HandGrip_R")
location = player.first_person_mesh.get_editor_property("relative_location")
assert all(abs(actual - expected) < .01 for actual, expected in zip((location.x, location.y, location.z), (0, 30, -10)))
slots = list(mesh.get_editor_property("materials"))
assert len(slots) == 1 and slots[0].material_interface.get_name() == "M_PFFieldGloves"
editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
assert editor.get_lod_count(mesh) == 1 and editor.get_num_sections(mesh, 0) == 1
assert editor.get_lod_material_slot(mesh, 0, 0) == 0
expected = json.loads((directory / "preview-application.json").read_text(encoding="utf-8"))["before"]
remote = {}
for component in player.get_components_by_class(unreal.SkeletalMeshComponent):
    if component == player.first_person_mesh:
        continue
    asset = component.get_skeletal_mesh_asset()
    path = asset.get_path_name() if asset else None
    assert expected[component.get_name()] == path
    remote[component.get_name()] = path
record = {"mesh": mesh.get_path_name(), "bones": player.first_person_mesh.get_num_bones(), "socket": "HandGrip_R",
          "lod0_vertices": editor.get_num_verts(mesh, 0), "lod0_sections": editor.get_num_sections(mesh, 0),
          "section_material_slot": 0, "material": slots[0].material_interface.get_path_name(),
          "translation_local_cm": [0, 30, -10], "remote_body_preserved": remote,
          "player_blueprint_sha256": hashlib.sha256((root / "Content/ProjectF/Player/BP_PFPlayer.uasset").read_bytes()).hexdigest(),
          "mesh_sha256": hashlib.sha256((root / "Content/ProjectF/Player/Meshes/SKM_PFHumanFirstPersonArms.uasset").read_bytes()).hexdigest(),
          "production_approved": False}
(directory / "saved-verification.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_HUMAN_FIRST_PERSON_SAVED_VERIFIED")
unreal.SystemLibrary.quit_editor()
