"""Run inside local Unreal Python; import only the locally authored alarm asset."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
source = root / "ArtSource" / "Exports" / "SM_Alarm_Industrial_01.fbx"
assert source.is_file(), source
task = unreal.AssetImportTask()
task.filename = str(source)
task.destination_path = "/Game/ProjectF/Environment/Devices"
task.destination_name = "SM_Alarm_Industrial_01"
task.automated = True
task.replace_existing = True
task.save = True
options = unreal.FbxImportUI()
options.import_mesh = True
options.import_as_skeletal = False
options.automated_import_should_detect_type = False
options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
options.import_materials = True
options.import_textures = False
options.static_mesh_import_data.combine_meshes = True
options.static_mesh_import_data.auto_generate_collision = True
task.options = options
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh = unreal.load_asset("/Game/ProjectF/Environment/Devices/SM_Alarm_Industrial_01")
assert isinstance(mesh, unreal.StaticMesh), task.imported_object_paths
subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
assert subsystem is not None, "Run with -ExecutePythonScript after full Editor initialization"
if subsystem.get_simple_collision_count(mesh) == 0:
    subsystem.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
assert subsystem.get_simple_collision_count(mesh) > 0
reductions = unreal.StaticMeshReductionOptions()
reductions.auto_compute_lod_screen_size = False
reductions.reduction_settings = [
    unreal.StaticMeshReductionSettings(percent_triangles=1.0, screen_size=1.0),
    unreal.StaticMeshReductionSettings(percent_triangles=0.5, screen_size=0.3),
    unreal.StaticMeshReductionSettings(percent_triangles=0.2, screen_size=0.1),
]
subsystem.set_lods(mesh, reductions)
assert unreal.EditorAssetLibrary.save_loaded_asset(mesh)
record = {
    "asset": mesh.get_path_name(),
    "imported_objects": list(task.imported_object_paths),
    "material_slots": len(mesh.get_editor_property("static_materials")),
    "lod_count": subsystem.get_lod_count(mesh),
    "vertices_lod0": subsystem.get_number_verts(mesh, 0),
    "triangles_per_lod": [mesh.get_num_triangles(lod) for lod in range(subsystem.get_lod_count(mesh))],
    "simple_collision_count": subsystem.get_simple_collision_count(mesh),
    "status": "needs_density_performance_and_visual_review",
}
world_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert world_subsystem.load_level("/Game/ProjectF/Maps/Tests/L_CoopSystems")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
replaced = 0
for actor in actors:
    if isinstance(actor, unreal.PFInteractable) and actor.get_editor_property("kind") == unreal.PFInteractableKind.ALARM:
        actor.get_editor_property("mesh").set_static_mesh(mesh)
        actor.set_actor_scale3d(unreal.Vector(1, 1, 1))
        point = actor.get_actor_location()
        actor.set_actor_location(unreal.Vector(point.x, point.y, 0), False, False)
        replaced += 1
assert replaced == 2, replaced
assert world_subsystem.save_current_level()
record["test_alarms_replaced"] = replaced
output = root / "ArtSource" / "Models" / "Alarm" / "unreal-import.json"
output.write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_ALARM_IMPORT " + json.dumps(record))
