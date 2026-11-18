from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/ProjectF/Maps/Tests/L_GuardRigReview")
records = []
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor, unreal.SkeletalMeshActor):
        component = actor.skeletal_mesh_component
        mesh = component.get_editor_property("skeletal_mesh_asset")
        records.append({"actor": actor.get_name(), "mesh": mesh.get_path_name() if mesh else None,
                        "mode": str(component.get_editor_property("animation_mode")), "data": str(component.get_editor_property("animation_data")),
                        "materials": [component.get_material(i).get_path_name() if component.get_material(i) else "" for i in range(component.get_num_materials())]})
(root / "ArtSource/Characters/Guard/review-inspection.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
unreal.log("PF_GUARD_REVIEW_INSPECTION " + json.dumps(records))
unreal.SystemLibrary.quit_editor()
