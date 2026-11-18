"""Read the cached temple scene without changing its source assets."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
directory = root / "ArtSource/Environment/Temple"
directory.mkdir(parents=True, exist_ok=True)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/Tests/L_TempleArtReview")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
models, placements, landscapes = {}, [], []


def xyz(value):
    return [value.x, value.y, value.z]


for actor in editor.get_all_level_actors():
    center, extent = actor.get_actor_bounds(False)
    if isinstance(actor, unreal.LandscapeProxy):
        landscapes.append({"actor": actor.get_actor_label(), "center": xyz(center), "extent": xyz(extent)})
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = component.static_mesh
        count = component.get_instance_count() if isinstance(component, unreal.InstancedStaticMeshComponent) else 1
        if not mesh or count <= 0:
            continue
        key = mesh.get_path_name()
        if key not in models:
            models[key] = {"source": key, "lod_triangles": [mesh.get_num_triangles(i) for i in range(meshes.get_lod_count(mesh))],
                "materials": [s.material_interface.get_path_name() if s.material_interface else None for s in mesh.static_materials],
                "simple_collision": meshes.get_simple_collision_count(mesh), "placements": 0}
        models[key]["placements"] += count
        placements.append({"actor": actor.get_actor_label(), "mesh": key, "instances": count,
            "level": actor.get_level().get_path_name(), "center": xyz(center), "extent": xyz(extent)})

samples = []
for x in range(-6000, 6001, 1000):
    for y in range(-6000, 6001, 1000):
        hit = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, 4500), unreal.Vector(x, y, -5000),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
        if hit:
            data = hit.to_tuple()
            samples.append({"xy": [x, y], "point": xyz(data[5]), "normal": xyz(data[7])})
record = {"map": world.get_path_name(), "models": list(models.values()), "placements": placements,
    "landscapes": landscapes, "ground_samples": samples, "production_approved": False}
(directory / "source-audit.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_TEMPLE_AUDIT models=" + str(len(models)) + " placements=" + str(len(placements)) + " ground_samples=" + str(len(samples)))
unreal.SystemLibrary.quit_editor()
