"""Render clean first-person backdrops from the authored whitebox for the UI proposal; no HUD is baked in."""
import bpy
from pathlib import Path
from mathutils import Vector

# Output belongs to the project preview assets; rendering does not resave the source .blend.
OUTPUT = Path(__file__).resolve().parents[2] / "Art/Whitebox/Previews/UI"
OUTPUT.mkdir(exist_ok=True)
# The loaded scene retains its real geometry and materials; camera/light settings are temporary.
scene = bpy.context.scene
scene.render.image_settings.file_format = "JPEG"
scene.render.image_settings.quality = 82
scene.render.resolution_x = 1280
scene.render.resolution_y = 720
scene.render.resolution_percentage = 100
# Camera is local to this rendering session; height matches a standing first-person viewpoint in metres.
data = bpy.data.cameras.new("UI_FirstPerson")
data.type = "PERSP"
data.lens = 23
cam = bpy.data.objects.new("UI_FirstPerson",data)
scene.collection.objects.link(cam)
scene.camera = cam
# Tuples contain output name, camera XYZ and focal target XYZ in Blender metres.
for name, position, target in [("hub",(-67,0,1.7),(-59,0,1.5)),
                               ("combat",(-11,-1,1.7),(4,0,1.5)),
                               ("boss",(109,0,1.7),(128,0,1.5))]:
    print(f"[Whitebox] render_ui_background {name}",flush=True)
    cam.location = position
    cam.rotation_euler = (Vector(target)-cam.location).to_track_quat("-Z","Y").to_euler()
    scene.render.filepath = str(OUTPUT/f"{name}.jpg")
    bpy.ops.render.render(write_still=True)
