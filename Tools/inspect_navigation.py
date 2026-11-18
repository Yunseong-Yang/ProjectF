"""Read-only inspection of the authored test map navigation setup in a full Editor."""
import json
import unreal

subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = subsystem.get_all_level_actors()
rows = []
for actor in actors:
    if isinstance(actor, (unreal.NavMeshBoundsVolume, unreal.RecastNavMesh)) or actor.get_actor_label() == "TestGround_Unapproved":
        center, extent = actor.get_actor_bounds(False)
        rows.append({"actor": actor.get_actor_label(), "class": actor.get_class().get_name(),
                     "center": [center.x, center.y, center.z], "extent": [extent.x, extent.y, extent.z]})
unreal.log("PF_NAV_INSPECT " + json.dumps(rows))
