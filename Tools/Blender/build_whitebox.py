"""Run with Blender --background --python this_file; creates editable geometry and UE FBX files.

All dimensions are metres in Blender. Four independent sectors use local FBX pivots;
the UE placement manifest converts metres to centimetres and adds the existing 100m height.
"""

import bpy
import functools
import json
import math
from pathlib import Path
from mathutils import Vector, Matrix

# Project-relative output keeps the source scene, exports, and preview renders together.
ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "Art" / "Whitebox"
# The four origins match AFPSDemoGameMode::GetAreaCenter after the UE importer adds Z=100m.
AREAS = [(-1, "00_SafeHub", "SAFE HAVEN"), (0, "01_CargoYard", "CARGO YARD"),
         (1, "02_RelayHall", "RELAY HALL"), (2, "03_BossArena", "WARDEN ARENA")]
# RGB values deliberately remain neutral; material contrast communicates traversable space.
PALETTE = {"White": (0.78, 0.80, 0.81, 1), "Floor": (0.43, 0.46, 0.48, 1),
           "Trim": (0.20, 0.23, 0.25, 1), "Mark": (0.90, 0.92, 0.92, 1)}
# Materials and gameplay markers are session-local; marker geometry never exports as collision.
MATERIALS = {}
MARKERS = []


def traced(function):
    """function is a synchronous Blender operation; wrap calls without holding scene objects."""
    print(f"[Whitebox] register {function.__name__}", flush=True)

    @functools.wraps(function)
    def wrapped(*args, **kwargs):
        """args/kwargs belong to this call; function is captured until the script exits, main thread only."""
        print(f"[Whitebox] call {function.__name__}", flush=True)
        return function(*args, **kwargs)

    return wrapped


@traced
def collection(name):
    """name is the stable outliner group/export identifier; return a scene-owned collection."""
    # A named collection keeps each sector independently editable in the .blend source.
    result = bpy.data.collections.new(name)
    bpy.context.scene.collection.children.link(result)
    return result


@traced
def move_to(obj, group):
    """obj is a scene object; group becomes its only collection owner, without changing transforms."""
    # Copy the list because unlinking mutates users_collection while iterating.
    for previous in tuple(obj.users_collection):
        previous.objects.unlink(obj)
    group.objects.link(obj)


@traced
def box(group, name, position, size, material="White", bevel=0.035, rotation=0.0):
    """group owns name; position/size are metres, bevel is edge width in metres, rotation is Z radians.
    material selects a neutral palette slot. Positive sizes produce closed collision-ready boxes.
    """
    bpy.ops.mesh.primitive_cube_add(size=1, location=position)
    # The object owns the mesh; applied scale gives predictable FBX normals and centimetre conversion.
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.rotation_euler.z = rotation
    obj.data.materials.append(MATERIALS[material])
    if bevel:
        # Bevel is retained as an editable modifier in .blend and evaluated in the export copy.
        modifier = obj.modifiers.new("Whitebox_edge_readability", "BEVEL")
        modifier.width = bevel
        modifier.segments = 1
    move_to(obj, group)
    return obj


@traced
def text(group, name, body, position, size=0.55, rotation=(0, 0, 0), material="Mark"):
    """group owns name; body is the displayed label, position/size are metres, rotation is XYZ radians.
    Text remains editable in Blender and exports as mesh; material is a palette slot.
    """
    # Text lies in its local XY plane; zero rotation is readable in plan views and floor markings.
    data = bpy.data.curves.new(name, type="FONT")
    data.body = body
    data.align_x = "CENTER"
    data.align_y = "CENTER"
    data.size = size
    data.extrude = 0.002
    # The collection and bpy.data retain the text object for the whole scene lifecycle.
    obj = bpy.data.objects.new(name, data)
    group.objects.link(obj)
    obj.location = position
    obj.rotation_euler = rotation
    data.materials.append(MATERIALS[material])
    return obj


@traced
def marker(group, name, position, kind, radius=1.0):
    """group owns an editor-only Empty; name/kind identify gameplay intent, position/radius are metres."""
    # Empties are visible in Blender edit mode only; the manifest carries them into UE as tagged Actors.
    obj = bpy.data.objects.new(name, None)
    group.objects.link(obj)
    obj.location = position
    obj.empty_display_type = "CIRCLE"
    obj.empty_display_size = radius
    obj["purpose"] = kind
    MARKERS.append({"name": name, "kind": kind, "position_m": list(position), "radius_m": radius})


@traced
def arch(group, prefix, x, y, opening=4.0, height=4.0):
    """group owns prefix objects; x/y locate a gate across local Y, opening/height are clear metres."""
    # Gate posts sit outside the walking lane; only the header spans the player's path.
    for side in (-1, 1):
        box(group, f"{prefix}_Post_{side}", (x, y + side*(opening/2+0.25), height/2), (0.7,0.5,height))
    box(group, f"{prefix}_Header", (x,y,height+0.22), (0.85,opening+1.1,0.44))
    box(group, f"{prefix}_Threshold", (x,y,0.025), (1.0,opening,0.05), "Trim", 0)


@traced
def cover(group, prefix, x, y, length=3.0, height=1.15, rotation=0.0):
    """group owns prefix; x/y and length/height are metres, rotation is Z radians; top is jumpable cover."""
    box(group, prefix, (x,y,height/2), (length,1.2,height), "White", 0.06, rotation)
    box(group, f"{prefix}_Cap", (x,y,height+0.025), (length+0.06,1.26,0.05), "Trim", 0.01, rotation)


@traced
def floor_ring(group, prefix, center, radius, segments=48):
    """group owns prefix strips; center/radius in metres, segments >= 8 controls polygon ring resolution."""
    # Thin flush strips are navigational markings, not raised obstacles under the Boss.
    for index in range(segments):
        # The tangent strip length leaves a small intentional gap between markers.
        angle = index * math.tau / segments
        position = (center[0]+radius*math.cos(angle), center[1]+radius*math.sin(angle), 0.022)
        box(group, f"{prefix}_{index:02}", position, (radius*math.tau/segments*0.86,0.12,0.035),
            "Mark", 0, angle+math.pi/2)


@traced
def build_shell(group, origin_x, index, label):
    """group owns a 34m square shell at origin_x metres; index/label provide sector wayfinding."""
    box(group, f"{index}_Foundation", (origin_x,0,-0.65), (35.5,35.5,1.25), "Trim", 0.12)
    box(group, f"{index}_Floor", (origin_x,0,-0.125), (34,34,0.25), "Floor", 0.02)
    # Low, near-side walls expose the interior from the presentation cameras; tall far walls frame it.
    # UE gets the same closed footprint; 2.4m low walls still exceed the character's jump height.
    for side in (-1, 1):
        box(group,f"{index}_WallX_{side}",(origin_x+side*17,0,2.5),(0.45,34.5,5.0),"White",0.05)
        box(group,f"{index}_WallY_{side}",(origin_x,side*17,1.2 if side == -1 else 2.5),
            (34.5,0.45,2.4 if side == -1 else 5.0),"White",0.05)
        # Slender perimeter curb is separated from floor markings for easy art replacement.
        box(group,f"{index}_CurbY_{side}",(origin_x,side*16.55,0.18),(33,0.25,0.36),"Trim",0.025)
    # Every 4m seam establishes human scale without introducing a texture dependency.
    for offset in range(-12, 13, 4):
        box(group,f"{index}_GridX_{offset}",(origin_x+offset,0,0.006),(0.022,32,0.008),"Trim",0)
        box(group,f"{index}_GridY_{offset}",(origin_x,offset,0.006),(32,0.022,0.008),"Trim",0)
    text(group,f"{index}_FloorID",index,(origin_x-12,-13,0.022),2.4)
    text(group,f"{index}_FloorTitle",label,(origin_x,-14.5,0.022),0.7)
    # A start lane keeps all known spawn/aim test coordinates unobstructed.
    for side in (-1,1):
        box(group,f"{index}_Lane_{side}",(origin_x-9,side*2.6,0.018),(8,0.1,0.025),"Mark",0)


@traced
def build_hub(group, x):
    """group is the safe collection, x is the world X origin in metres; gameplay terminals remain runtime actors."""
    build_shell(group,x,"00","SAFE HAVEN / NO HOSTILES")
    # The covered rear service bay leaves the native player (-6.5,0) and terminal (0,-3) clear.
    box(group,"Hub_ServiceBack",(x+4,-9,2.25),(0.4,10,4.5))
    box(group,"Hub_ServiceCanopy",(x+1,-9,4.45),(6.5,10.5,0.3))
    for y in (-13.3,-4.7):
        box(group,f"Hub_CanopySupport_{y}",(x-2,y,2.2),(0.3,0.3,4.4),"Trim")
    box(group,"Hub_Workbench",(x+2,-9,0.9),(1.8,6,1.8))
    for y in (-11,-9,-7):
        box(group,f"Hub_Locker_{y}",(x+3.2,y,2.6),(0.6,1.5,1.5),"Trim")
    # Terminal footprints are floor markings; do not duplicate the interactive cylinder at the same point.
    box(group,"Hub_ShopPad",(x,-3,0.025),(3.2,3.2,0.05),"Mark",0.03)
    text(group,"Hub_ShopLabel","UPGRADE",(x-2.7,-3,0.04),0.42,rotation=(0,0,math.pi/2),material="Trim")
    arch(group,"Hub_EntryPortal",x,3,opening=3.6,height=4.0)
    text(group,"Hub_EnterLabel","ENTER SECTOR",(x-2.3,3,0.025),0.45,rotation=(0,0,math.pi/2))
    # Empty markers preserve intended interaction/return locations without collision.
    marker(group,"PlayerStart_Hub",(x-6.5,0,1.0),"player_start",0.55)
    marker(group,"Interact_Upgrade",(x,-3,0.8),"upgrade_terminal",2.5)
    marker(group,"Interact_Portal",(x,3,0.8),"sector_portal",2.5)
    for y in (-10,10):
        box(group,f"Hub_Bench_{y}",(x-10,y,0.45),(4,1,0.9))


@traced
def build_cargo(group, x):
    """group owns sector one at x metres; side cover introduces flanking while preserving central visibility."""
    build_shell(group,x,"01","CARGO YARD")
    arch(group,"Cargo_Entry",x-13,0)
    # Cover is kept away from the native ring spawn positions and the X-axis firing lane.
    for y in (-10.5,10.5):
        cover(group,f"Cargo_Cover_{y}",x-8,y,4.0)
        box(group,f"Cargo_Container_{y}",(x+12.6,y,1.6),(3.0,6.0,3.2))
        for rib in range(5):
            box(group,f"Cargo_Rib_{y}_{rib}",(x+11.07,y-2.4+rib*1.2,1.6),(0.08,0.12,3.0),"Trim",0)
    cover(group,"Cargo_LeftCover",x-13,6,2.8,0.9,math.pi/2)
    cover(group,"Cargo_RightCover",x-13,-6,2.8,0.9,math.pi/2)
    text(group,"Cargo_Objective","CLEAR HOSTILES",(x+7,0,0.024),0.65,rotation=(0,0,math.pi/2))


@traced
def build_relay(group, x):
    """group owns sector two at x metres; perimeter gantries and staggered side cover imply a service hall."""
    build_shell(group,x,"02","RELAY HALL")
    arch(group,"Relay_Entry",x-13,0)
    for y in (-13.5,13.5):
        # Service banks sit outside the enemy ring to avoid impassable spawn collisions.
        for offset in (-8,-2,4,10):
            box(group,f"Relay_Bank_{y}_{offset}",(x+offset,y,2.1),(2.5,1.5,4.2))
            box(group,f"Relay_Face_{y}_{offset}",(x+offset,y-math.copysign(0.78,y),2.4),(1.9,0.08,2.3),"Trim",0.01)
        box(group,f"Relay_Overhead_{y}",(x,y,5.0),(30,0.8,0.5),"White",0.04)
    cover(group,"Relay_FlankA",x-5,8,3.2,1.1)
    cover(group,"Relay_FlankB",x+4,-8,3.2,1.1)
    cover(group,"Relay_FlankC",x-11,-8,2.8,1.1,math.pi/2)
    # Overhead crossbeams allow full movement beneath; there is no inaccessible playable upper floor.
    for offset in (-10,0,10):
        box(group,f"Relay_CrossBeam_{offset}",(x+offset,0,5.7),(0.5,29,0.5),"Trim",0.025)


@traced
def build_boss(group, x):
    """group owns final sector at x metres; a clear central ring gives room to dodge Boss ground attacks."""
    build_shell(group,x,"03","WARDEN / FINAL SECTOR")
    arch(group,"Boss_Entry",x-13,0,opening=5.0,height=5.0)
    floor_ring(group,"Boss_InnerRing",(x,0),6.0)
    floor_ring(group,"Boss_OuterRing",(x,0),12.2)
    for sx in (-1,1):
        for sy in (-1,1):
            # Corner pylons communicate scale without obstructing the 24m-diameter dodge zone.
            box(group,f"Boss_Pylon_{sx}_{sy}",(x+sx*12.8,sy*12.8,3),(1.6,1.6,6))
            box(group,f"Boss_PylonFoot_{sx}_{sy}",(x+sx*12.8,sy*12.8,0.35),(2.4,2.4,0.7),"Trim")
    box(group,"Boss_BackMonolith",(x+15,0,3.5),(1.6,8,7))
    text(group,"Boss_FloorWarning","KEEP MOVING",(x,0,0.03),0.9)
    marker(group,"Boss_Spawn",(x+8,0,1.3),"boss_spawn",1.15)


@traced
def add_combat_markers(group, x, count, number):
    """group owns editor markers; x is origin metres, count is minion number, number is sector 1..3."""
    marker(group,f"PlayerStart_Sector{number}",(x-11,0,1.0),"player_start",0.55)
    for index in range(count):
        # Same formula as the current native spawner; documented anchors can replace it in future.
        angle = math.tau*index/count
        marker(group,f"Enemy_{number}_{index:02}",(x+3.5+6.5*math.cos(angle),9.5*math.sin(angle),1.1),"enemy_spawn",0.48)


@traced
def export_area(group, name, origin):
    """group is editable source; name is FBX basename, origin is world pivot metres, never mutate originals."""
    # Temporary copies let exports apply bevels/text conversion while preserving a clean editable source.
    temporary = collection(f"EXPORT_{name}")
    bpy.ops.object.select_all(action="DESELECT")
    for original in tuple(group.objects):
        if original.type not in {"MESH","FONT"}:
            continue
        # Mesh/curve data is copied because conversion and joining mutate ownership.
        duplicate = original.copy()
        duplicate.data = original.data.copy()
        temporary.objects.link(duplicate)
        duplicate.select_set(True)
    bpy.context.view_layer.objects.active = next(iter(temporary.objects))
    bpy.ops.object.convert(target="MESH")
    bpy.ops.object.join()
    # Joined geometry pivots locally; UE places it using the manifest, never by guessed import offsets.
    joined = bpy.context.object
    joined.name = f"SM_WB_{name}"
    bpy.context.scene.cursor.location = origin
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    joined.location = (0,0,0)
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    # FBX -Y/Z to UE reverses Y; mirror export-only vertices and winding so gameplay X/Y stays exact.
    joined.data.transform(Matrix.Diagonal((1,-1,1,1)))
    joined.data.flip_normals()
    bpy.ops.export_scene.fbx(filepath=str(OUTPUT/"FBX"/f"{joined.name}.fbx"), use_selection=True,
        object_types={"MESH"}, use_mesh_modifiers=True, bake_anim=False, add_leaf_bones=False,
        axis_forward="-Y", axis_up="Z", apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        use_triangles=True, mesh_smooth_type="FACE")
    bpy.data.objects.remove(joined, do_unlink=True)
    bpy.data.collections.remove(temporary)


@traced
def camera(group, name, position, target, scale):
    """group owns camera name; position/target in metres, scale is orthographic view width in metres."""
    # Camera points local -Z at target with local Y as image up.
    data = bpy.data.cameras.new(name)
    data.type = "ORTHO"
    data.ortho_scale = scale
    data.clip_end = 1000
    obj = bpy.data.objects.new(name,data)
    group.objects.link(obj)
    obj.location = position
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat("-Z","Y").to_euler()
    return obj


@traced
def render_preview(scene, cam, filename, width=1400, height=1050):
    """scene/cam are Blender-owned; filename is a preview basename, width/height are pixel dimensions."""
    scene.camera = cam
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.filepath = str(OUTPUT/"Previews"/f"{filename}.png")
    bpy.ops.render.render(write_still=True)


@traced
def main():
    """Build from factory-empty state in an isolated background Blender process; never touches open user scenes."""
    OUTPUT.mkdir(parents=True,exist_ok=True)
    (OUTPUT/"FBX").mkdir(exist_ok=True)
    (OUTPUT/"Previews").mkdir(exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    # Scene uses true metres; FBX stores unit conversion and UE imports at 1x into centimetres.
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1.0
    for name, color in PALETTE.items():
        material = bpy.data.materials.new(f"M_WB_{name}")
        material.diffuse_color = color
        material.use_nodes = True
        material.node_tree.nodes.get("Principled BSDF").inputs["Base Color"].default_value = color
        material.node_tree.nodes.get("Principled BSDF").inputs["Roughness"].default_value = 0.82
        MATERIALS[name] = material
    # Registry drives both export placement and the per-sector preview cameras.
    registry = []
    for index, name, label in AREAS:
        group = collection(name)
        x = index*60.0
        if index == -1:
            build_hub(group,x)
        elif index == 0:
            build_cargo(group,x)
        elif index == 1:
            build_relay(group,x)
        else:
            build_boss(group,x)
        if index >= 0:
            add_combat_markers(group,x,(5,8,6)[index],index+1)
        export_area(group,name,(x,0,0))
        registry.append({"name": name,"asset": f"SM_WB_{name}","index": index,
                         "origin_ue_cm": [x*100,0,10000],"label":label})
    # JSON has no comments: schema, units, ownership and import rules are documented alongside the scene.
    (OUTPUT/"whitebox_manifest.json").write_text(json.dumps({"version":1,"blender_unit":"metres",
        "ue_unit":"centimetres","ue_height_offset_cm":10000,"areas":registry,"markers":MARKERS},indent=2),encoding="utf-8")
    # Workbench studio rendering is a fast geometry review, with no texture/shader compilation dependency.
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.world = bpy.data.worlds.new("WhiteboxWorld")
    scene.world.color = (0.16,0.17,0.19)
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "BOTH"
    scene.display.shading.curvature_ridge_factor = 1.4
    scene.display.shading.curvature_valley_factor = 1.2
    scene.display.shading.show_object_outline = True
    scene.display.shading.background_type = "WORLD"
    scene.view_settings.view_transform = "Standard"
    # Presentation cameras do not export to UE and remain in a separate outliner collection.
    presentation = collection("90_Presentation_Cameras")
    for entry in registry:
        # Hide other sectors during individual previews so adjacent geometry cannot clip image corners.
        for other in registry:
            bpy.data.collections[other["name"]].hide_render = other["name"] != entry["name"]
        x = entry["index"]*60.0
        cam = camera(presentation,f"CAM_{entry['name']}",(x-37,-43,42),(x,0,0),53)
        render_preview(scene,cam,entry["name"])
    for entry in registry:
        bpy.data.collections[entry["name"]].hide_render = False
    # Top view is a useful measurable level-design plan of all four physically isolated sectors.
    overview = camera(presentation,"CAM_Overview",(30,-105,150),(30,0,0),228)
    render_preview(scene,overview,"Overview",2800,1000)
    scene.camera = overview
    bpy.context.scene.cursor.location = (0,0,0)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT/"FPSDemo_ThreeSector_Whitebox.blend"))
    print(f"[Whitebox] SUCCESS: {OUTPUT}",flush=True)


if __name__ == "__main__":
    main()
