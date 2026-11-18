"""Distinguish missing walkable surfaces from disconnected courtyard access."""
from pathlib import Path
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/ProjectF/Maps/Tests/L_FrontierTemple")
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
start=unreal.Vector(12500,10000,4300)
records=[]
ramp=[]
for x in (14900,15100,15300,15500,15700,15900,16000):
    hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,10550,5200),unreal.Vector(x,10550,4000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE,True)
    if hit:
        data=hit.to_tuple()
        p=data[5]+unreal.Vector(0,0,100)
        nav=unreal.NavigationSystemV1.project_point_to_navigation(world,p,None,None,unreal.Vector(75,75,180))
        route=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,unreal.Vector(14400,10000,4210),nav) if nav is not None else None
        ramp.append({"x":x,"height":data[5].z,"actor":data[9].get_actor_label(),"nav":None if nav is None else [nav.x,nav.y,nav.z],"partial":route.is_partial() if route else None})
collisions=[]
capsules=[]
for radius in (34,42):
    hit=unreal.SystemLibrary.capsule_trace_single(world,unreal.Vector(12500,10000,4300),unreal.Vector(14500,10000,4300),radius,96,
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE,True)
    if hit:
        data=hit.to_tuple()
        capsules.append({"radius":radius,"actor":data[9].get_actor_label(),"point":[data[5].x,data[5].y,data[5].z]})
    else:
        capsules.append({"radius":radius,"clear":True})
height_profile=[]
stair_detail={}
mesh_editor=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
all_actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
links=[]
for actor in all_actors:
    if isinstance(actor,unreal.NavLinkProxy):
        p=actor.get_actor_location()
        links.append({"actor":actor.get_actor_label(),"location":[p.x,p.y,p.z],"points":[{"left":[n.left.x,n.left.y,n.left.z],"right":[n.right.x,n.right.y,n.right.z],"direction":str(n.direction)} for n in actor.get_editor_property("point_links")]})
stairs=next(a for a in all_actors if a.get_actor_label()=="SM_CastleStairs")
component=stairs.static_mesh_component
scale=stairs.get_actor_scale3d()
stair_detail={"scale":[scale.x,scale.y,scale.z],"actor_collision":stairs.get_actor_enable_collision(),
    "enabled":str(component.get_collision_enabled()),"profile":str(component.get_collision_profile_name()),
    "sections":[mesh_editor.is_section_collision_enabled(component.static_mesh,0,i) for i in range(component.static_mesh.get_num_sections(0))],
    "two_sided_collision":component.static_mesh.get_editor_property("body_setup").get_editor_property("double_sided_geometry"),"rays":[]}
others=[a for a in all_actors if a!=stairs]
for x in (14000,14500,15000,15500):
    for up in (False,True):
        hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,10000,3500 if up else 5200),unreal.Vector(x,10000,5200 if up else 3500),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,others,unreal.DrawDebugTrace.NONE,True)
        stair_detail["rays"].append({"x":x,"up":up,"hit":hit.to_tuple()[5].z if hit else None})
ignore=[]
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    role=(actor.get_actor_label()+" "+actor.get_name()).lower()
    if not any(word in role for word in ("stair","ground","floor","platform")):
        ignore.append(actor)
for x in range(12000,16201,25):
    hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,10000,5200),unreal.Vector(x,10000,3500),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,ignore,unreal.DrawDebugTrace.NONE,True)
    if hit:
        data=hit.to_tuple()
        sample={"x":x,"z":data[5].z,"actor":data[9].get_actor_label()}
        if x%100==0:
            nav=unreal.NavigationSystemV1.project_point_to_navigation(world,unreal.Vector(x,10000,data[5].z+100),None,None,unreal.Vector(75,75,160))
            sample["nav"]=None if nav is None else [nav.x,nav.y,nav.z]
            if nav is not None:
                route=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,unreal.Vector(12500,10000,4210),nav)
                sample["connected"]=route is not None and route.is_valid() and not route.is_partial()
        height_profile.append(sample)
for z in (4250,4350,4450,4550):
    hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(12500,10000,z),unreal.Vector(15000,10000,z),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE,True)
    if hit:
        data=hit.to_tuple()
        component=data[10]
        collisions.append({"z":z,"point":[data[5].x,data[5].y,data[5].z],"actor":data[9].get_actor_label(),
            "mesh":component.static_mesh.get_path_name() if isinstance(component,unreal.StaticMeshComponent) and component.static_mesh else None})
for point in ((12500,10000,4300),(14000,10000,4400),(14800,10000,4650),(15900,10000,4605),(16000,10000,4703),(16200,10000,4605),(16000,10000,4300),(17000,7700,5100)):
    projected=unreal.NavigationSystemV1.project_point_to_navigation(world,unreal.Vector(*point),None,None,unreal.Vector(150,150,180))
    row={"point":point,"projection":None}
    if projected is not None:
        row["projection"]=[projected.x,projected.y,projected.z]
        path=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,start,projected)
        row["valid"]=path is not None and path.is_valid()
        row["partial"]=path.is_partial() if path else None
        inside=unreal.NavigationSystemV1.find_path_to_location_synchronously(world,unreal.Vector(14400,10000,4205),projected)
        row["inside_partial"]=inside.is_partial() if inside else None
        row["inside_points"]=[[p.x,p.y,p.z] for p in inside.path_points] if inside else []
    records.append(row)
(root/"ArtSource/Environment/Temple/nav-diagnostic.json").write_text(json.dumps({"navigation":records,"entry_collisions":collisions,"entry_capsules":capsules,"height_profile":height_profile,"stair_detail":stair_detail,"links":links,"ramp":ramp},indent=2),encoding="utf-8")
unreal.log("PF_TEMPLE_NAV_DIAGNOSTIC_COMPLETE")
unreal.SystemLibrary.quit_editor()
