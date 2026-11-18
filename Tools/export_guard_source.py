"""Local Unreal workbench: export reference geometry and inspect retarget APIs."""
from pathlib import Path
import json
import unreal

root = Path(__file__).resolve().parents[1]
directory = root / "ArtSource/Characters/Guard"
mesh = unreal.load_asset("/Game/Characters/Paragon/Heroes/TwinBlast/Meshes/SKM_TwinBlast_ActionHero")
assert isinstance(mesh, unreal.SkeletalMesh)
task = unreal.AssetExportTask()
task.object = mesh; task.filename = str(directory / "Guard_Original.fbx")
task.automated = True; task.prompt = False; task.replace_identical = True
task.exporter = unreal.SkeletalMeshExporterFBX()
options = unreal.FbxExportOption(); options.collision = False; options.level_of_detail = False; options.ascii = False
task.options = options
assert unreal.Exporter.run_asset_export_task(task)
record = {"mesh": mesh.get_path_name(), "skeleton": mesh.get_editor_property("skeleton").get_path_name(),
          "materials": [m.material_interface.get_path_name() if m.material_interface else "" for m in mesh.get_editor_property("materials")],
          "export": task.filename, "source_preserved": True, "production_approved": False}
(directory / "unreal-source.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
api = {}
for cls_name in ("IKRetargetBatchOperation", "IKRetargeterController", "IKRigController", "SkeletalMeshEditorSubsystem"):
    cls = getattr(unreal, cls_name, None)
    if cls:
        api[cls_name] = {name: str(getattr(cls, name).__doc__) for name in dir(cls)
                         if not name.startswith("_") and any(key in name for key in ("duplicate_and_retarget", "ik_rig", "skeletal_mesh", "preview_mesh", "retarget_pose", "lod_count", "lod_num", "material", "skeleton"))}
(directory / "local-api.json").write_text(json.dumps(api, indent=2), encoding="utf-8")
unreal.log("PF_GUARD_EXPORT " + json.dumps(record))
unreal.SystemLibrary.quit_editor()
