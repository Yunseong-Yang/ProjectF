"""Use exact static collision on paving; convex hulls must not seal ground-level routes."""
from pathlib import Path
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
directory=root/"ArtSource/Environment/Temple"
rows=json.loads((directory/"unreal-validation.json").read_text(encoding="utf-8"))
editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
changes=[]
for row in rows:
    lower=row["source"].lower()
    if not any(token in lower for token in ("cobble","pavement","paving","ground","floor","stair","riser")):
        continue
    mesh=unreal.load_asset(row["derived"])
    assert mesh
    before=str(editor.get_collision_complexity(mesh))
    editor.remove_collisions(mesh)
    body=mesh.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag",unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    body.set_editor_property("never_needs_cooked_collision_data",False)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh,False)
    row["collision_role"]="Static walkable paving/stairs: exact reduced surface; no route-sealing convex hull"
    row["simple_collision"]=editor.get_simple_collision_count(mesh)
    row["convex_collision"]=editor.get_convex_collision_count(mesh)
    changes.append({"mesh":row["derived"],"before":before,"after":str(editor.get_collision_complexity(mesh))})
(directory/"unreal-validation.json").write_text(json.dumps(rows,indent=2),encoding="utf-8")
(directory/"walk-surface-repair.json").write_text(json.dumps(changes,indent=2),encoding="utf-8")
unreal.log("PF_TEMPLE_WALK_SURFACES_COMPLETE models="+str(len(changes)))
unreal.SystemLibrary.quit_editor()
