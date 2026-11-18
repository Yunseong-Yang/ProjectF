"""Build a local industrial alarm prop, retain the source, and record mesh checks.

Run with Blender --background --factory-startup --disable-autoexec --python this_file.
No downloads, inference, plugins, credentials or external services are used.
"""
from pathlib import Path
import hashlib
import json
import math
import bpy
import bmesh
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ArtSource" / "Models" / "Alarm"
EXPORT = ROOT / "ArtSource" / "Exports"
SOURCE.mkdir(parents=True, exist_ok=True)
EXPORT.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.unit_settings.system = "METRIC"


def material(name, color, metallic=0.0, roughness=0.5):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Metallic"].default_value = metallic
    shader.inputs["Roughness"].default_value = roughness
    return mat


paint = material("Alarm_OlivePaint", (0.11, 0.16, 0.12), 0.55, 0.42)
metal = material("Alarm_BareMetal", (0.23, 0.25, 0.25), 0.8, 0.32)
warning = material("Alarm_Warning", (0.68, 0.065, 0.025), 0.2, 0.33)
parts = []


def finish(obj, name, mat, bevel=0.008):
    obj.name = name
    obj.data.materials.append(mat)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        modifier = obj.modifiers.new("ManufacturedEdges", "BEVEL")
        modifier.width = bevel
        modifier.segments = 2
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    parts.append(obj)
    return obj


def box(name, location, scale, mat, bevel=0.008):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    obj = bpy.context.object
    obj.dimensions = scale
    return finish(obj, name, mat, bevel)


def cylinder(name, location, radius, depth, mat, vertices=16, rotation=(0, 0, 0)):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=location, rotation=rotation)
    return finish(bpy.context.object, name, mat, 0.003)


box("MountingFoot", (0, 0, 0.04), (0.36, 0.28, 0.08), metal)
box("Upright", (0, 0.035, 0.72), (0.09, 0.09, 1.36), paint, 0.004)
box("ControlHousing", (0, -0.025, 1.26), (0.43, 0.23, 0.51), paint, 0.018)
box("FacePlate", (0, -0.151, 1.26), (0.36, 0.025, 0.43), metal, 0.007)
box("WeatherHood", (0, -0.025, 1.535), (0.51, 0.32, 0.035), paint, 0.005)
cylinder("EmergencyButton", (0, -0.18, 1.22), 0.058, 0.045, warning, rotation=(math.pi / 2, 0, 0))
cylinder("ButtonCollar", (0, -0.165, 1.22), 0.077, 0.015, metal, rotation=(math.pi / 2, 0, 0))
cylinder("SignalLens", (0, -0.17, 1.4), 0.028, 0.025, warning, rotation=(math.pi / 2, 0, 0))
cylinder("BeaconBase", (0, 0.01, 1.58), 0.075, 0.06, metal)
cylinder("Beacon", (0, 0.01, 1.675), 0.06, 0.13, warning, vertices=24)
for x in (-0.14, 0.14):
    for z in (1.09, 1.43):
        cylinder("FaceFastener", (x, -0.17, z), 0.012, 0.009, metal, vertices=6, rotation=(math.pi / 2, 0, 0))
for i in range(5):
    box("Vent", (0.219, -0.01, 1.14 + i * 0.04), (0.008, 0.12, 0.012), metal, 0.001)

bpy.ops.object.select_all(action="DESELECT")
for obj in parts:
    obj.select_set(True)
bpy.context.view_layer.objects.active = parts[0]
bpy.ops.object.join()
source = bpy.context.object
source.name = "SM_Alarm_Source"
bpy.context.scene.cursor.location = (0, 0, 0)
bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
original = SOURCE / "Alarm_Source.blend"
bpy.ops.wm.save_as_mainfile(filepath=str(original))

mesh = source.data
mesh.calc_loop_triangles()
before = len(mesh.loop_triangles)
bm = bmesh.new()
bm.from_mesh(mesh)
bmesh.ops.remove_doubles(bm, verts=list(bm.verts), dist=0.00001)
bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
non_manifold = sum(1 for edge in bm.edges if not edge.is_manifold)
degenerate = sum(1 for face in bm.faces if face.calc_area() < 1e-10)
bm.to_mesh(mesh)
bm.free()
mesh.update()
mesh.calc_loop_triangles()
source.name = "SM_Alarm_Industrial_01"
after = len(mesh.loop_triangles)
bpy.context.view_layer.objects.active = source
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.uv.smart_project(island_margin=0.02)
bpy.ops.object.mode_set(mode="OBJECT")
work = SOURCE / "Alarm_Optimized.blend"
bpy.ops.wm.save_as_mainfile(filepath=str(work))
fbx = EXPORT / "SM_Alarm_Industrial_01.fbx"
bpy.ops.export_scene.fbx(filepath=str(fbx), use_selection=True, object_types={"MESH"},
                         apply_unit_scale=True, global_scale=1.0, axis_forward="-Y", axis_up="Z",
                         bake_anim=False, add_leaf_bones=False, mesh_smooth_type="FACE")

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE"
scene.render.resolution_x = 640
scene.render.resolution_y = 640
scene.render.resolution_percentage = 100
scene.world = bpy.data.worlds.new("StudioWorld")
scene.world.color = (0.12, 0.12, 0.12)
bpy.ops.object.camera_add(location=(2.3, -3.0, 2.2))
camera = bpy.context.object
camera.rotation_euler = (Vector((0, 0, 0.85)) - camera.location).to_track_quat("-Z", "Y").to_euler()
camera.data.type = "ORTHO"
camera.data.ortho_scale = 2.1
scene.camera = camera
for location, energy, size in [((1, -2, 3), 400, 3), ((-2, -1, 2), 250, 3), ((0, 2, 3), 450, 2)]:
    bpy.ops.object.light_add(type="AREA", location=location)
    light = bpy.context.object
    light.data.energy = energy
    light.data.shape = "DISK"
    light.data.size = size
    light.rotation_euler = (Vector((0, 0, 1)) - light.location).to_track_quat("-Z", "Y").to_euler()
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = str(SOURCE / "Alarm_Inspection.png")
bpy.ops.render.render(write_still=True)

record = {
    "asset": source.name, "source": "Local manual Blender geometry", "external_inference": False,
    "blender": bpy.app.version_string, "source_file": str(original), "work_file": str(work),
    "export_file": str(fbx), "export_sha256": hashlib.sha256(fbx.read_bytes()).hexdigest(),
    "triangles_before": before, "triangles_after": after, "materials": len(mesh.materials),
    "non_manifold_edges": non_manifold, "degenerate_faces": degenerate,
    "dimensions_m": list(source.dimensions), "uv_layers": len(mesh.uv_layers),
    "texture_bytes": 0, "status": "needs_unreal_validation",
    "remaining": ["Unreal import statistics and collision", "Near/distant visual review", "Representative-density performance"],
}
(SOURCE / "optimization.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
print("PF_ALARM", json.dumps(record))
