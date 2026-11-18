"""Author a one-square-kilometre mountain combat region with local assets only."""
from pathlib import Path
import collections
import json
import math
import random
import sys
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0, str(root / "Tools"))
from frontier_layout import surface_height as height, road_weight, CENTERS, PATH

random.seed(1030)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = "/Game/ProjectF/Maps/L_MountainFrontier"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    assert levels.load_level(path)
    for actor in editor.get_all_level_actors():
        if "PF.FrontierGenerated" in [str(t) for t in actor.tags]: editor.destroy_actor(actor)
else:
    assert levels.new_level(path)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Game/ProjectF/Core/BP_PFGameMode.BP_PFGameMode_C"))
PREFIX = "/Game/ProjectF/Environment/Frontier/Meshes/"
cache = {}
counts = collections.Counter()


def load(name):
    if name not in cache:
        cache[name] = unreal.load_asset(PREFIX + name)
        assert cache[name], name
    return cache[name]


def vector(x, y, z=0):
    return unreal.Vector(x * 100, y * 100, z * 100)


def spawn(cls, x=0, y=0, z=0, yaw=0, label=None):
    actor = editor.spawn_actor_from_class(cls, vector(x, y, z), unreal.Rotator(pitch=0, yaw=yaw, roll=0))
    assert actor, (cls, x, y, z)
    actor.tags = ["PF.FrontierGenerated", "PF.Unapproved"]
    if label: actor.set_actor_label(label)
    return actor


def mesh(name, x=0, y=0, z=None, yaw=0, scale=(1, 1, 1), collision=True):
    if z is None: z = height(x, y)
    actor = spawn(unreal.StaticMeshActor, x, y, z, yaw, name)
    actor.static_mesh_component.set_static_mesh(load(name))
    actor.set_actor_scale3d(unreal.Vector(*scale))
    if not collision: actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    counts[name] += 1
    return actor


def house(cx, cy, z, angle=0, label="Station building"):
    def local(x, y):
        a = math.radians(angle)
        return cx + x * math.cos(a) - y * math.sin(a), cy + x * math.sin(a) + y * math.cos(a)
    for side in (-1, 1):
        for panel in (-1, 1):
            x, y = local(panel * 2, side * 4)
            mesh("SM_PlasterDoorway" if side == -1 and panel == 1 else "SM_PlasterWindow", x, y, z, angle)
            x, y = local(side * 4, panel * 2)
            mesh("SM_PlasterWall" if side == 1 else "SM_PlasterWindow", x, y, z, angle + 90)
    roof = mesh("SM_CorrugatedRoof", cx, cy, z + 3.28, angle, (2, 1.8, 1)); roof.set_actor_label(label + " roof")
    mesh("SM_WoodenCrate", cx - 1.8, cy + 2.4, z, angle, (1.8, 1.8, 1.8))


# Ground geometry is tiled for bounded culling/collision and smooth matching seams.
for row in range(8):
    for col in range(8): mesh(f"SM_FrontierGround_{row}_{col}", z=0)

sun = spawn(unreal.DirectionalLight, z=200)
sun.set_actor_rotation(unreal.Rotator(pitch=-43, yaw=-55, roll=0), False)
sun.light_component.set_editor_property("intensity", 75000.0)
sun.light_component.set_editor_property("light_color", unreal.Color(255, 233, 202, 255))
sun.light_component.set_editor_property("atmosphere_sun_light", True)
sun.light_component.set_editor_property("dynamic_shadow_distance_movable_light", 18000)
spawn(unreal.SkyAtmosphere)
sky = spawn(unreal.SkyLight)
sky.light_component.set_editor_property("intensity", 1.8)
sky.light_component.set_editor_property("real_time_capture", True)
fog = spawn(unreal.ExponentialHeightFog, z=8)
fog.component.set_editor_property("fog_density", .008)
fog.component.set_editor_property("fog_height_falloff", .13)
fog.component.set_editor_property("enable_volumetric_fog", True)
post = spawn(unreal.PostProcessVolume)
post.set_editor_property("unbound", True)
settings = post.get_editor_property("settings")
settings.set_editor_property("override_auto_exposure_method", True)
settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", True)
settings.set_editor_property("override_camera_iso", True)
settings.set_editor_property("camera_iso", 100)
settings.set_editor_property("override_camera_shutter_speed", True)
settings.set_editor_property("camera_shutter_speed", 125)
settings.set_editor_property("override_depth_of_field_fstop", True)
settings.set_editor_property("depth_of_field_fstop", 8)
settings.set_editor_property("override_auto_exposure_bias", True)
settings.set_editor_property("auto_exposure_bias", -.3)
settings.set_editor_property("override_vignette_intensity", True)
settings.set_editor_property("vignette_intensity", .18)
settings.set_editor_property("override_motion_blur_amount", True)
settings.set_editor_property("motion_blur_amount", 0)
post.set_editor_property("settings", settings)
ambience = spawn(unreal.AmbientSound, label="Mountain wind")
ambience.audio_component.set_sound(unreal.load_asset("/Game/ProjectF/Audio/S_MountainWind"))
ambience.audio_component.set_volume_multiplier(.35)

# Safe preparation camp and its first view down the access road.
camp = CENTERS[0]
house(camp[0] - 8, camp[1] + 5, camp[2], label="Field camp")
start = spawn(unreal.PlayerStart, camp[0], camp[1], camp[2] + 1.02, 30)
start.tags = list(start.tags) + ["PF.SafeSpawn"]
safe = spawn(unreal.PFInteractable, camp[0] + 2.5, camp[1] + .5, camp[2])
safe.set_editor_property("kind", unreal.PFInteractableKind.SAFE_POINT); safe.set_editor_property("persistent_id", "Safe.Camp")
safe.set_editor_property("display_name", "안전 캠프 · 회복·장비 준비")
safe.mesh.set_static_mesh(load("SM_WoodenCrate")); safe.set_actor_scale3d(unreal.Vector(2, 2, 2))
for dx, dy in ((-10, -6), (-4, -6), (8, 5), (10, 6)):
    mesh("SM_WoodenBarrel", camp[0] + dx, camp[1] + dy, camp[2], scale=(2, 2, 2))

enemy_class = unreal.load_class(None, "/Game/ProjectF/AI/BP_Guard.BP_Guard_C")
outposts = []
for index, (cx, cy, z, radius) in enumerate(CENTERS[1:]):
    outpost = spawn(unreal.PFOutpost, cx, cy, z + .5, label="Signal Relay" if index == 0 else "Mountain Garrison")
    outpost.set_editor_property("outpost_id", "RadioStation" if index == 0 else "Stronghold")
    outpost.set_editor_property("display_name", "산악 통신소" if index == 0 else "계곡 주둔지")
    outpost.set_editor_property("facility", unreal.PFFacilityType.COMMUNICATIONS if index == 0 else unreal.PFFacilityType.STRONGHOLD)
    if index == 0: outpost.set_editor_property("supports", ["Stronghold"])
    outpost.set_editor_property("enemy_class", enemy_class)
    outpost.set_editor_property("alarm_calling_sound", unreal.load_asset("/Game/ProjectF/Audio/S_AlarmCalling"))
    outpost.set_editor_property("alarm_sent_sound", unreal.load_asset("/Game/ProjectF/Audio/S_AlarmSent"))
    outpost.terminal.set_static_mesh(load("SM_WoodenCrate")); outpost.set_actor_scale3d(unreal.Vector(1.7, 1.7, 1.7))
    spawns = []
    for dx, dy, yaw in [(-25, -26, 90), (18, -28, 0), (0, -10, 180), (-28, 20, 270), (26, 26, 135), (-5, 30, 270), (22, 2, 180), (5, 10, 0)]:
        spawns.append(unreal.Transform(location=vector(cx + dx, cy + dy, z + 1.0), rotation=unreal.Rotator(pitch=0, yaw=yaw, roll=0)))
    outpost.set_editor_property("garrison_spawns", spawns)
    outpost.set_editor_property("reinforcement_spawns", [unreal.Transform(location=vector(cx + 78 + i * 1.2, cy + 54, height(cx + 78 + i * 1.2, cy + 54) + 1), rotation=unreal.Rotator(pitch=0, yaw=210, roll=0)) for i in range(8)])
    alarm = spawn(unreal.PFInteractable, cx + 4, cy + 1, z)
    alarm.set_editor_property("kind", unreal.PFInteractableKind.ALARM)
    alarm.set_editor_property("persistent_id", ("RadioStation" if index == 0 else "Stronghold") + ".Alarm")
    alarm.set_editor_property("display_name", "경보 장치 해제"); alarm.set_editor_property("outpost", outpost)
    alarm.mesh.set_static_mesh(unreal.load_asset("/Game/ProjectF/Environment/Devices/SM_Alarm_Industrial_01"))
    outpost.set_editor_property("alarm", alarm); outposts.append(outpost)
    house(cx - 17, cy - 7, z, 0, "Communications office")
    house(cx + 17, cy + 14, z, 90, "Garrison stores")
    if index: house(cx - 18, cy + 25, z, 0, "Barracks")
    mesh("SM_Watchtower", cx - 33, cy - 25, z, 0)
    for side in (-1, 1):
        for j in range(20):
            # Deliberate open front gate and two unguarded rear gaps.
            if j in ((8, 9, 10) if side < 0 else (3, 4, 15)): continue
            mesh("SM_FencePanel", cx - 28 + j * 2.8, cy + side * 36, z)
    for dx, dy, yaw in [(-10, -21, 0), (12, -18, 0), (-5, 17, 180), (24, 0, 90), (-25, 8, -90), (7, 25, 180)]:
        mesh("SM_SandbagCover", cx + dx, cy + dy, z, yaw)
        cover = spawn(unreal.PFCoverPoint, cx + dx, cy + dy + (1.15 if yaw == 0 else -1.15), z + .96, yaw)
        cover.set_editor_property("peek_offset", unreal.Vector(160, 0, 0))
    for j in range(7):
        mesh("SM_WoodenCrate", cx - 9 + j * 2.7, cy + 5, z, j * 33, (2, 2, 2))
    for j in range(7):
        mesh("SM_ConcreteBarrier", cx + 72, cy + 48 + j * 2.4, height(cx + 72, cy + 48 + j * 2.4), 90, (1, 1, 2.7))
    # Landmark radio mast, from low triangle structural members.
    mast = mesh("SM_RadioMast", cx + 9, cy - 2, z, 0)
    mast.set_actor_label("Relay antenna lattice")

for index, (x, y) in enumerate([(-256, -89), (-38, -57), (106, 18)]):
    z = height(x, y)
    cache_actor = spawn(unreal.PFInteractable, x, y, z, label=f"Exploration cache {index + 1}")
    cache_actor.set_editor_property("kind", unreal.PFInteractableKind.EXPLORATION)
    cache_actor.set_editor_property("persistent_id", f"Exploration.{index}")
    cache_actor.set_editor_property("display_name", ["연구 보급품 · 소음기 설계도", "무기 보급품 · 산탄총 설계도", "정찰 보급품 · 조준경 설계도"][index])
    cache_actor.mesh.set_static_mesh(load("SM_WoodenCrate")); cache_actor.set_actor_scale3d(unreal.Vector(1.8, 1.8, 1.8))
    mesh("SM_Rock", x - 7, y + 10, height(x - 7, y + 10) + .3, 40, (14, 8, 9))

# Spatially bounded HISM clusters. Grass has no collision/shadows and a short cull distance.
batches = {}
def instance(name, x, y, z, yaw, scale, cull):
    key = (name, int((x + 500) // 100), int((y + 500) // 100))
    if key not in batches:
        actor = spawn(unreal.PFEnvironmentCluster, label=f"{name} cluster {key[1]} {key[2]}")
        component = actor.instances; component.set_static_mesh(load(name))
        if name == "SM_Elm08": component.set_collision_profile_name("BlockAll")
        else: component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_cull_distances(int(cull * .8), cull)
        if "Grass" in name or "Fern" in name: component.set_cast_shadow(False)
        batches[key] = {"component": component, "transforms": []}
    batches[key]["transforms"].append(unreal.Transform(location=vector(x, y, z), rotation=unreal.Rotator(pitch=0, yaw=yaw, roll=0), scale=unreal.Vector(scale, scale, scale)))
    counts[name] += 1

for _ in range(60000):
    x, y = random.uniform(-470, 470), random.uniform(-470, 470)
    if road_weight(x, y) > .2 or any(math.hypot(x - cx, y - cy) < r + 3 for cx, cy, _, r in CENTERS): continue
    z = height(x, y)
    if z > 205 or random.random() < max(0, (z - 150) / 55): continue
    chance = random.random()
    if chance < .07:
        instance("SM_Elm08", x, y, z, random.uniform(0, 360), random.uniform(1.1, 1.8), 60000)
    elif chance < .09:
        instance("SM_JapaneseMaple", x, y, z, random.uniform(0, 360), random.uniform(1.6, 2.3), 40000)
    elif chance < .12:
        instance("SM_BostonFern_A", x, y, z, random.uniform(0, 360), random.uniform(8, 13), 6500)
    else:
        instance("SM_WildGrass_A" if chance < .65 else "SM_DryMeadowGrass_A", x, y, z, random.uniform(0, 360), random.uniform(.55, .95), 6500)
for _ in range(120000):
    x, y = random.uniform(-470, 470), random.uniform(-470, 470)
    if road_weight(x, y) > .2 or any(math.hypot(x - cx, y - cy) < r + 3 for cx, cy, _, r in CENTERS): continue
    z = height(x, y)
    if z > 120: continue
    instance("SM_WildGrass_A" if random.random() < .7 else "SM_DryMeadowGrass_A", x, y, z, random.uniform(0, 360), random.uniform(.55, .95), 6500)
for _ in range(130):
    x, y = random.uniform(-430, 430), random.uniform(-430, 430)
    if road_weight(x, y) > .25 or any(math.hypot(x - cx, y - cy) < r + 8 for cx, cy, _, r in CENTERS): continue
    gx, gy = (height(x + 1, y) - height(x - 1, y)) * .5, (height(x, y + 1) - height(x, y - 1)) * .5
    if math.hypot(gx, gy) > .7:
        # An open scanned cliff is embedded in a steep hillside, never used as a free boulder.
        rock_scale = random.uniform(.5, .9)
        bounds = load("SM_GraniteCliff").get_bounds()
        top = (bounds.origin.z + bounds.box_extent.z) * rock_scale / 100
        mesh("SM_GraniteCliff", x, y, height(x, y) - top * .72, math.degrees(math.atan2(gy, gx)) - 90, (rock_scale,) * 3)
    else:
        rock_scale = random.uniform(6, 14)
        bounds = load("SM_Rock").get_bounds()
        base_z = (bounds.origin.z - bounds.box_extent.z) * rock_scale / 100
        radius = max(bounds.box_extent.x, bounds.box_extent.y) * rock_scale / 100
        low = min(height(x + dx * radius, y + dy * radius) for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)))
        mesh("SM_Rock", x, y, low - base_z - .6, random.uniform(0, 360), (rock_scale,) * 3)
for index in range(12):
    a = index * math.tau / 12
    x, y = math.cos(a) * 1500, math.sin(a) * 1500
    mesh("SM_BG_PeakMountains_A", x, y, -120, index * 30, (300, 300, 300), False)

for batch in batches.values():
    batch["component"].add_instances(batch["transforms"], False, False, True)
assert levels.save_current_level()
record = {"map": path, "area_m2": 1000000, "actors": len(editor.get_all_level_actors()), "instances_and_meshes": dict(counts),
          "hism_clusters": len(batches), "seed": 1030, "production_approved": False,
          "required_validation": ["navigation", "spawn and ingress", "close/distant visual", "4 players dense and split", "frame RAM VRAM"]}
(root / "ArtSource/Environment/frontier-layout.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
unreal.log("PF_FRONTIER_LEVEL_COMPLETE " + json.dumps(record))
unreal.SystemLibrary.quit_editor()
