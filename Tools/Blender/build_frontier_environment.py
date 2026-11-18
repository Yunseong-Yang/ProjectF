"""Local deterministic mesh cleanup and mountain/architecture authoring, Blender 5.x.

No addons, network, remote inference or purchases. Source exports are untouched.
"""
import bpy
import bmesh
from pathlib import Path
import hashlib
import json
import math
import random
import sys
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
DIRECTORY = ROOT / "ArtSource" / "Environment"
EXPORTS = DIRECTORY / "OptimizedExports"
EXPORTS.mkdir(parents=True, exist_ok=True)
random.seed(1030)
sys.path.insert(0, str(ROOT / "Tools"))
from frontier_layout import height, road_weight, smooth, CENTERS, PATH


def clear():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)


def stats(obj):
    mesh = obj.data
    mesh.calc_loop_triangles()
    bm = bmesh.new(); bm.from_mesh(mesh)
    result = {"vertices": len(mesh.vertices), "triangles": len(mesh.loop_triangles),
              "materials": len(mesh.materials), "uv_layers": len(mesh.uv_layers),
              "nonmanifold_edges": sum(not e.is_manifold for e in bm.edges),
              "degenerate_faces": sum(f.calc_area() < 1e-12 for f in bm.faces)}
    bm.free(); return result


def export(obj, name):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True); bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.fbx(filepath=str(EXPORTS / (name + ".fbx")), use_selection=True, object_types={"MESH"},
                             apply_unit_scale=True, axis_forward="-Y", axis_up="Z", add_leaf_bones=False,
                             bake_anim=False, mesh_smooth_type="FACE")


def clean_sources():
    records = json.loads((DIRECTORY / "unreal-source-meshes.json").read_text(encoding="utf-8"))
    results = []
    for record in records:
        clear()
        bpy.ops.import_scene.fbx(filepath=record["fbx"], use_anim=False)
        objects = [o for o in bpy.context.scene.objects if o.type == "MESH"]
        assert objects, record
        bpy.ops.object.select_all(action="DESELECT")
        for obj in objects: obj.select_set(True)
        bpy.context.view_layer.objects.active = objects[0]
        if len(objects) > 1: bpy.ops.object.join()
        obj = bpy.context.object
        original = stats(obj)
        name = record["name"]
        # Preserve alpha-card foliage islands and their vertex normals; no blanket welding.
        bm = bmesh.new(); bm.from_mesh(obj.data)
        bmesh.ops.dissolve_degenerate(bm, dist=1e-7, edges=list(bm.edges))
        bm.to_mesh(obj.data); bm.free()
        target = 24000 if "Maple" in name else 18000 if "Elm" in name else 18000 if "Cliff" in name else 12000 if "Mountains" in name else 3000 if "Crate" in name or "Barrel" in name else 4000
        if original["triangles"] > target:
            modifier = obj.modifiers.new("Measured reduction", "DECIMATE")
            modifier.ratio = target / original["triangles"]
            modifier.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        bpy.ops.wm.save_as_mainfile(filepath=str(DIRECTORY / (name + "_Optimized.blend")))
        export(obj, name)
        result = {**record, "before": original, "after": stats(obj), "blender": bpy.app.version_string,
                  "source_sha256": hashlib.sha256(Path(record["fbx"]).read_bytes()).hexdigest(),
                  "export_sha256": hashlib.sha256((EXPORTS / (name + ".fbx")).read_bytes()).hexdigest(),
                  "approval": "pending Unreal collision, material, visual and representative density validation"}
        results.append(result)
    (DIRECTORY / "blender-validation.json").write_text(json.dumps(results, indent=2), encoding="utf-8")


def terrain():
    clear()
    records = []
    for row in range(8):
        for col in range(8):
            n, size = 40, 125
            verts, faces, colors = [], [], []
            for j in range(n + 1):
                for i in range(n + 1):
                    x, y = -500 + col * size + i * size / n, -500 + row * size + j * size / n
                    z = height(x, y)
                    slope = math.hypot(height(x + 1, y) - height(x - 1, y), height(x, y + 1) - height(x, y - 1)) * .5
                    # Blender's FBX -> Unreal handedness conversion reflects Y for
                    # newly authored geometry. Source exports already encode this.
                    verts.append((x, -y, z))
                    colors.append((road_weight(x, y), smooth(.35, 1.0, slope), 0, 1))
            for j in range(n):
                for i in range(n):
                    a = j * (n + 1) + i
                    faces.extend([(a, a + n + 2, a + 1), (a, a + n + 1, a + n + 2)])
            name = f"SM_FrontierGround_{row}_{col}"
            mesh = bpy.data.meshes.new(name); mesh.from_pydata(verts, [], faces); mesh.update()
            obj = bpy.data.objects.new(name, mesh); bpy.context.collection.objects.link(obj)
            uv = mesh.uv_layers.new(name="UVMap")
            color = mesh.color_attributes.new(name="TerrainWeights", type="FLOAT_COLOR", domain="POINT")
            for index, rgba in enumerate(colors): color.data[index].color = rgba
            for loop in mesh.loops:
                v = mesh.vertices[loop.vertex_index].co
                uv.data[loop.index].uv = (v.x / 4, v.y / 4)
            for polygon in mesh.polygons: polygon.use_smooth = True
            export(obj, name)
            records.append({"name": name, **stats(obj), "open_boundary_intended": True, "collision": "static terrain complex-as-simple"})
    bpy.ops.wm.save_as_mainfile(filepath=str(DIRECTORY / "FrontierTerrain_Source.blend"))
    (DIRECTORY / "terrain-validation.json").write_text(json.dumps(records, indent=2), encoding="utf-8")


def box(name, location, scale, bevel=.025):
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    obj = bpy.context.object; obj.name = name; obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        modifier = obj.modifiers.new("Soft manufactured edges", "BEVEL"); modifier.width = bevel; modifier.segments = 2
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    return obj


def kit():
    clear()
    records = []
    def finish(name, objects, material):
        bpy.ops.object.select_all(action="DESELECT")
        for obj in objects: obj.select_set(True)
        bpy.context.view_layer.objects.active = objects[0]
        if len(objects) > 1: bpy.ops.object.join()
        obj = bpy.context.object; obj.name = name
        bpy.context.scene.cursor.location = (0, 0, 0); bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
        bpy.ops.object.mode_set(mode="EDIT"); bpy.ops.mesh.select_all(action="SELECT"); bpy.ops.uv.smart_project(); bpy.ops.object.mode_set(mode="OBJECT")
        mat = bpy.data.materials.get(material) or bpy.data.materials.new(material); obj.data.materials.clear(); obj.data.materials.append(mat)
        export(obj, name); records.append({"name": name, "material": material, **stats(obj)})
    finish("SM_PlasterWall", [box("Wall", (0, 0, 1.6), (4, .28, 3.2))], "Plaster")
    finish("SM_PlasterWindow", [box("Sill", (0, 0, .5), (4, .28, 1)), box("Lintel", (0, 0, 2.8), (4, .28, .8)),
                                box("Left", (-1.4, 0, 1.8), (1.2, .28, 1.6)), box("Right", (1.4, 0, 1.8), (1.2, .28, 1.6))], "Plaster")
    finish("SM_PlasterDoorway", [box("Left", (-1.35, 0, 1.6), (1.3, .28, 3.2)), box("Right", (1.35, 0, 1.6), (1.3, .28, 3.2)), box("Lintel", (0, 0, 2.85), (1.4, .28, .7))], "Plaster")
    roof = [box("Roof sheet", (0, 0, .07), (4.5, 5, .14), .015)]
    for i in range(21): roof.append(box("Standing seam", (-2.2 + i * .22, 0, .15), (.035, 5, .04), .005))
    finish("SM_CorrugatedRoof", roof, "OxidizedMetal")
    finish("SM_FencePanel", [box("Post", (x, 0, 1.0), (.14, .14, 2.0)) for x in (-1.4, 1.4)] + [box("Rail", (0, 0, z), (2.8, .10, .12)) for z in (.65, 1.5)] + [box("Paling", (-1.35 + i * .18, 0, 1), (.13, .08, 1.8), .008) for i in range(16)], "WeatheredWood")
    bags = []
    for row in range(3):
        for col in range(4):
            obj = box("Sandbag", ((col - 1.5) * .56 + (row % 2) * .12, 0, .18 + row * .30), (.60, .65, .32), .12)
            bags.append(obj)
    finish("SM_SandbagCover", bags, "Canvas")
    finish("SM_ConcreteBarrier", [box("Barrier", (0, 0, .65), (2.4, .65, 1.3), .12), box("Base", (0, 0, .08), (2.65, .9, .16), .04)], "Concrete")
    tower = [box("Leg", (x, y, 2.3), (.25, .25, 4.6)) for x in (-1.5, 1.5) for y in (-1.5, 1.5)]
    tower += [box("Deck", (0, 0, 4.5), (3.6, 3.6, .25)), box("Canopy", (0, 0, 6.9), (4.1, 4.1, .12))]
    tower += [box("Post", (x, y, 5.8), (.14, .14, 2.4)) for x in (-1.6, 1.6) for y in (-1.6, 1.6)]
    tower += [box("Balustrade", (0, y, 5.1), (3.5, .12, .9)) for y in (-1.6, 1.6)]
    tower += [box("Balustrade", (x, 0, 5.1), (.12, 3.5, .9)) for x in (-1.6, 1.6)]
    # A 1.3m wide open stair leads to the deck; no convex hull sealing the tower.
    tower += [box("Stair", (2.5, -1.5 + i * .32, .125 + i * .25), (1.3, .34, .25)) for i in range(18)]
    finish("SM_Watchtower", tower, "WeatheredWood")
    def beam(a, b, radius=.045):
        a, b = Vector(a), Vector(b)
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=radius, depth=(b-a).length, location=(a+b)*.5)
        obj = bpy.context.object; obj.rotation_euler = (b-a).to_track_quat("Z", "Y").to_euler(); return obj
    lattice = []
    for x in (-.7, .7):
        for y in (-.7, .7): lattice.append(beam((x, y, 0), (x*.45, y*.45, 20), .075))
    for tier in range(5):
        z = tier * 4
        for side in (-1, 1):
            lattice.append(beam((-.7, side*.7, z), (.7, side*.7, z+4)))
            lattice.append(beam((.7, side*.7, z), (-.7, side*.7, z+4)))
            lattice.append(beam((side*.7, -.7, z), (side*.7, .7, z+4)))
            lattice.append(beam((side*.7, .7, z), (side*.7, -.7, z+4)))
    lattice += [beam((0, 0, 19), (0, 0, 23), .045), beam((-.7, 0, 14), (1.4, 0, 14), .07)]
    # A shallow concave microwave dish gives the tower its functional silhouette.
    vertices, faces = [(1.2, 0, 14)], []
    for ring in range(1, 7):
        r = ring / 6
        for segment in range(24):
            a = segment * math.tau / 24
            vertices.append((1.2 + .28*r*r, math.cos(a)*r, 14+math.sin(a)*r))
    for segment in range(24): faces.append((0, 1+segment, 1+(segment+1)%24))
    for ring in range(5):
        for segment in range(24):
            a = 1+ring*24+segment; b = 1+ring*24+(segment+1)%24
            faces.append((a, a+24, b+24, b))
    dish_mesh = bpy.data.meshes.new("Dish"); dish_mesh.from_pydata(vertices, [], faces); dish_mesh.update()
    dish = bpy.data.objects.new("Dish", dish_mesh); bpy.context.collection.objects.link(dish); lattice.append(dish)
    finish("SM_RadioMast", lattice, "OxidizedMetal")
    bpy.ops.wm.save_as_mainfile(filepath=str(DIRECTORY / "FrontierArchitecture_Source.blend"))
    (DIRECTORY / "architecture-validation.json").write_text(json.dumps(records, indent=2), encoding="utf-8")


if __name__ == "__main__":
    mode = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else ["all"]
    if "sources" in mode or "all" in mode: clean_sources()
    if "terrain" in mode or "all" in mode: terrain()
    if "kit" in mode or "all" in mode: kit()
    print("PF_FRONTIER_BLENDER_COMPLETE", mode)
