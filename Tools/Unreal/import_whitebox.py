"""UE5.4 Python commandlet: import Blender meshes, check scale, and create a separate editable map.

Only /Game/Whitebox assets are replaced. The original template map is never edited.
"""
import json
from pathlib import Path
import unreal

# This script lives in Tools/Unreal; all authored inputs stay in the project.
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Art/Whitebox"
# Asset namespace is isolated from the template to keep import reruns predictable.
DESTINATION = "/Game/Whitebox/Meshes"
MAP_PATH = "/Game/Whitebox/Maps/L_ThreeSector_Whitebox"


def import_mesh(asset_name):
    """asset_name is an FBX basename under SOURCE/FBX; returns a saved, scene-independent StaticMesh."""
    unreal.log(f"[Whitebox] import_mesh {asset_name}")
    # Automated import task deliberately disables convex collision, which would seal entire rooms.
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE / "FBX" / f"{asset_name}.fbx")
    task.destination_path = DESTINATION
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    # Standard FBX importer options use file units and preserve a local sector pivot.
    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = False
    options.import_materials = True
    options.import_textures = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    options.automated_import_should_detect_type = False
    options.static_mesh_import_data.combine_meshes = True
    options.static_mesh_import_data.auto_generate_collision = False
    options.static_mesh_import_data.generate_lightmap_u_vs = True
    options.static_mesh_import_data.convert_scene = True
    options.static_mesh_import_data.convert_scene_unit = True
    options.static_mesh_import_data.force_front_x_axis = False
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    # Asset must exist before the map can reference it; missing imports fail the commandlet explicitly.
    mesh = unreal.load_asset(f"{DESTINATION}/{asset_name}")
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"StaticMesh import failed: {asset_name}")
    # Static whitebox uses triangle collision; it is intentionally not a physics-simulated object.
    body = mesh.get_editor_property("body_setup")
    body.set_editor_property("collision_trace_flag",unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    unreal.log(f"[Whitebox] imported {asset_name} bounds={mesh.get_bounding_box()}")
    return mesh


def add_actor(actor_class, label, position, rotation=None):
    """actor_class is a UE Actor type; label is its editor name, position is cm, rotation is an optional Rotator."""
    unreal.log(f"[Whitebox] add_actor {label}")
    # EditorActorSubsystem owns spawned actors through the new persistent level.
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    # Reuse only an exact generated label of the expected class; preserve unrelated user-authored Actors.
    for existing in subsystem.get_all_level_actors():
        if existing.get_actor_label() == label and isinstance(existing, actor_class):
            existing.set_actor_location(unreal.Vector(*position), False, False)
            if rotation is not None:
                existing.set_actor_rotation(rotation, False)
            return existing
    actor = subsystem.spawn_actor_from_class(actor_class,unreal.Vector(*position),rotation or unreal.Rotator())
    if actor is None:
        raise RuntimeError(f"Actor spawn failed: {label}")
    actor.set_actor_label(label)
    return actor


def main():
    """Create a fresh whitebox-only map, preserving all original maps and runtime-generated interactive actors."""
    unreal.log("[Whitebox] call main")
    # Probe import verifies FBX units/axis independently of the visually symmetric room shells.
    probe = import_mesh("SM_WB_AxisProbe")
    bounds = probe.get_bounding_box()
    unreal.log(f"WHITEBOX_AXIS_PROBE min={bounds.min} max={bounds.max}")
    # Probe detects wrong units or an omitted Y correction before a playable map is saved.
    if abs(bounds.min.x-200)>0.1 or abs(bounds.min.y-500)>0.1 or abs(bounds.max.z-600)>0.1:
        raise RuntimeError("FBX coordinate calibration failed: expected min=(200,500,0), max=(400,900,600)cm")
    # Manifest documents un-commentable JSON fields in Documentation/Whitebox.md.
    manifest = json.loads((SOURCE/"whitebox_manifest.json").read_text(encoding="utf-8"))
    meshes = {}
    for entry in manifest["areas"]:
        meshes[entry["asset"]] = import_mesh(entry["asset"])
    # Reimport loads the existing authored map and updates named Actors, preserving unrelated edits.
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    # NewLevel refuses existing assets in UE5.4; select the appropriate operation explicitly.
    map_ready = levels.load_level(MAP_PATH) if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH) else levels.new_level(MAP_PATH)
    if not map_ready:
        raise RuntimeError("Could not create whitebox map")
    for entry in manifest["areas"]:
        actor = add_actor(unreal.StaticMeshActor,entry["name"],entry["origin_ue_cm"])
        actor.static_mesh_component.set_static_mesh(meshes[entry["asset"]])
        actor.static_mesh_component.set_collision_profile_name("BlockAll")
        # GameMode detects all four tags and skips procedural fallback geometry only when complete.
        actor.tags = ["DemoAuthoredBlockout",f"DemoArea{entry['index']}"]
    # Gameplay markers are editor-only references; GameMode remains the source of runtime enemy/terminal spawning.
    for item in manifest["markers"]:
        position = [value*100 for value in item["position_m"]]
        position[2] += manifest["ue_height_offset_cm"]
        actor = add_actor(unreal.TargetPoint,item["name"],position)
        actor.tags = ["WhiteboxReference",item["kind"]]
        actor.set_editor_property("is_editor_only_actor",True)
    # Initial player start is inside the safe region; native StartPlay performs the same authoritative relocation.
    add_actor(unreal.PlayerStart,"PlayerStart_SafeHub",[-6650,0,10100])
    sun = add_actor(unreal.DirectionalLight,"Whitebox_Sun",[0,0,15000],unreal.Rotator(-48,-32,0))
    sun.light_component.set_editor_property("intensity",3.0)
    sun.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky = add_actor(unreal.SkyLight,"Whitebox_Sky",[0,0,15000])
    sky.root_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("real_time_capture",True)
    add_actor(unreal.SkyAtmosphere,"Whitebox_Atmosphere",[0,0,0])
    # Native GM overrides old template class defaults; this map points directly to it.
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode",unreal.load_class(None,"/Script/FPSDemo.FPSDemoGameMode"))
    if not levels.save_current_level():
        raise RuntimeError("Could not save whitebox map")
    unreal.EditorAssetLibrary.save_directory("/Game/Whitebox",only_if_is_dirty=True,recursive=True)
    unreal.log("WHITEBOX_IMPORT_SUCCESS: " + MAP_PATH)


main()
