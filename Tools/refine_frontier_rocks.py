"""Persist checked simple collision for the existing Blender-validated closed boulder."""
from pathlib import Path
import json
import unreal

mesh = unreal.load_asset("/Game/ProjectF/Environment/Frontier/Meshes/SM_Rock")
assert mesh
subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
if subsystem.get_simple_collision_count(mesh) == 0:
    subsystem.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.NDOP26)
if subsystem.get_simple_collision_count(mesh) == 0:
    subsystem.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
assert subsystem.get_simple_collision_count(mesh) > 0
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh)
record = {"mesh": mesh.get_path_name(), "simple_collision": subsystem.get_simple_collision_count(mesh),
          "lod_triangles": [mesh.get_num_triangles(i) for i in range(subsystem.get_lod_count(mesh))],
          "material": mesh.static_materials[0].material_interface.get_path_name(), "production_approved": False}
root = Path(unreal.Paths.project_dir()).resolve()
(root / "ArtSource/Environment/boulder-validation.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.SystemLibrary.quit_editor()
