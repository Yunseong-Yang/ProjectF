"""Create arms-only display geometry, preserving the complete existing bind-pose rig."""
from pathlib import Path
import hashlib
import json
import math
import collections
import bpy
import bmesh

root=Path(__file__).resolve().parents[2]
directory=root/"ArtSource/Characters/FirstPerson"
source=json.loads((directory/"source.json").read_text(encoding="utf-8"))
assert hashlib.sha256(Path(source["fbx"]).read_bytes()).hexdigest()==source["fbx_sha256"]
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.import_scene.fbx(filepath=source["fbx"],use_anim=False)
rigs=[obj for obj in bpy.context.scene.objects if obj.type=="ARMATURE"]
assert len(rigs)==1
rig=rigs[0]
rig.name="Armature"
root_restored=False
if "root" not in rig.data.bones:
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True);bpy.context.view_layer.objects.active=rig
    bpy.ops.object.mode_set(mode="EDIT")
    roots=[bone for bone in rig.data.edit_bones if bone.parent is None]
    root_bone=rig.data.edit_bones.new("root")
    root_bone.head=(0,0,0);root_bone.tail=(0,.1,0);root_bone.use_deform=False
    for bone in roots:
        bone.parent=root_bone;bone.use_connect=False
    bpy.ops.object.mode_set(mode="OBJECT")
    root_restored=True
bones=[{"name":bone.name,"parent":bone.parent.name if bone.parent else None} for bone in rig.data.bones]
assert all(math.isfinite(c) for bone in rig.data.bones for row in bone.matrix_local for c in row)
records=[]
prefixes=("upperarm","lowerarm","hand","thumb","index","middle","ring","pinky")
for obj in [obj for obj in bpy.context.scene.objects if obj.type=="MESH"]:
    obj.data.calc_loop_triangles()
    before=len(obj.data.loop_triangles)
    groups={group.index for group in obj.vertex_groups if group.name.lower().startswith(prefixes)}
    print("PF_ARM_SOURCE",obj.name,before,len(obj.vertex_groups),len(groups),[group.name for group in obj.vertex_groups][:15],flush=True)
    weight={v.index:sum(g.weight for g in v.groups if g.group in groups) for v in obj.data.vertices}
    bm=bmesh.new();bm.from_mesh(obj.data);bm.verts.ensure_lookup_table()
    remove=[face for face in bm.faces if not all(weight[v.index]>=.5 for v in face.verts)]
    bmesh.ops.delete(bm,geom=remove,context="FACES_ONLY")
    bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context="VERTS")
    bm.to_mesh(obj.data);bm.free()
    if not obj.data.polygons:
        bpy.data.objects.remove(obj,do_unlink=True)
        continue
    obj.name="SKM_PFFirstPersonArms"
    obj.data.calc_loop_triangles()
    retained=len(obj.data.loop_triangles)
    hand_groups={group.index for group in obj.vertex_groups if group.name.lower().startswith(("hand","thumb","index","middle","ring","pinky"))}
    upper={group.index for group in obj.vertex_groups if group.name.lower().startswith("upperarm")}
    lower={group.index for group in obj.vertex_groups if group.name.lower().startswith("lowerarm")}
    protect=obj.vertex_groups.new(name="PreserveHandsAndElbows")
    positions=collections.Counter()
    for vertex in obj.data.vertices:
        hand=sum(g.weight for g in vertex.groups if g.group in hand_groups)
        upper_weight=sum(g.weight for g in vertex.groups if g.group in upper)
        lower_weight=sum(g.weight for g in vertex.groups if g.group in lower)
        if hand>.1 or (upper_weight>.15 and lower_weight>.15):
            protect.add([vertex.index],1,"REPLACE")
            positions[tuple(round(c,5) for c in vertex.co)]+=1
    original=obj.data.copy()
    bpy.context.view_layer.objects.active=obj
    modifier=obj.modifiers.new("Preserve finger and elbow deformation","DECIMATE")
    modifier.ratio=min(1,20000/retained)
    modifier.vertex_group=protect.name;modifier.invert_vertex_group=True;modifier.vertex_group_factor=1
    modifier.use_collapse_triangulate=True
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    after_positions=collections.Counter(tuple(round(c,5) for c in v.co) for v in obj.data.vertices)
    missing=sum((positions-after_positions).values())
    if missing:
        obj.data=original
    obj.vertex_groups.remove(obj.vertex_groups.get("PreserveHandsAndElbows"))
    obj.data.calc_loop_triangles()
    bm=bmesh.new();bm.from_mesh(obj.data)
    degenerate=sum(face.calc_area()<1e-12 for face in bm.faces);bm.free()
    unweighted=sum(sum(g.weight for g in v.groups)<1e-6 for v in obj.data.vertices)
    assert degenerate==0 and unweighted==0 and len(obj.data.uv_layers)>0
    assert all(math.isfinite(c) for v in obj.data.vertices for c in v.co)
    records.append({"source_triangles":before,"triangles":len(obj.data.loop_triangles),"vertices":len(obj.data.vertices),
        "uv_layers":len(obj.data.uv_layers),"unweighted_vertices":unweighted,"degenerate_faces":degenerate,
        "arms_before_reduction":retained,"protected_vertices":sum(positions.values()),"protected_vertices_lost":missing,
        "reduction_retained":missing==0})
print("PF_ARM_COUNTS",json.dumps(records),flush=True)
assert records and sum(row["triangles"] for row in records)<40000
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(directory/"FirstPersonArms.blend"))
filename=directory/"SKM_PFFirstPersonArms.fbx"
bpy.ops.export_scene.fbx(filepath=str(filename),use_selection=False,object_types={"ARMATURE","MESH"},
    add_leaf_bones=False,bake_anim=False,use_armature_deform_only=False,apply_unit_scale=True,
    axis_forward="-Y",axis_up="Z",mesh_smooth_type="FACE",use_tspace=True)
report={"source":source["source"],"source_fbx_sha256":source["fbx_sha256"],"blender":bpy.app.version_string,
    "meshes":records,"bones":bones,"bone_count":len(bones),"full_rig_preserved":True,
    "export":str(filename),"export_sha256":hashlib.sha256(filename.read_bytes()).hexdigest(),
    "production_approved":False,"required":["Unreal skeleton/socket compatibility","fire/reload/execution and remote-body verification","native first-person close-up"]}
report["restored_unreal_root_omitted_by_fbx_armature_import"]=root_restored
report["triangle_budget_note"]="Initial15k goal was set before source inspection and failed;20k reduction is retained only when hand/elbow vertices survive. A protected33k derivative may be retained unapproved rather than damaging deformation."
(directory/"blender-validation.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
print("PF_FIRST_PERSON_ARMS_COMPLETE",json.dumps(records),flush=True)
