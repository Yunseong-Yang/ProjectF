"""Inspect owned weapon axes and sockets before calibrating first-person sights."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
player = unreal.get_default_object(unreal.load_class(None, "/Game/ProjectF/Player/BP_PFPlayer.BP_PFPlayer_C"))
rows = []
for label in ("rifle_mesh", "pistol_mesh"):
    mesh = player.get_editor_property(label)
    assert mesh
    bounds = mesh.get_bounds()
    sockets = []
    component = unreal.new_object(unreal.StaticMeshComponent)
    component.set_static_mesh(mesh)
    for name in component.get_all_socket_names():
        location = component.get_socket_location(name)
        rotation = component.get_socket_rotation(name)
        sockets.append({"name": str(name), "location": [location.x, location.y, location.z],
                        "rotation": [rotation.pitch, rotation.yaw, rotation.roll]})
    rows.append({"role": label, "mesh": mesh.get_path_name(), "origin": [bounds.origin.x, bounds.origin.y, bounds.origin.z],
                 "extent": [bounds.box_extent.x, bounds.box_extent.y, bounds.box_extent.z], "sockets": sockets})
(root / "ArtSource/Characters/FirstPersonHuman/weapon-sight-inspection.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
unreal.log("PF_WEAPON_SIGHT_INSPECTION_COMPLETE")
unreal.SystemLibrary.quit_editor()
