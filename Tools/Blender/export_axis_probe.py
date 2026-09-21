"""One-off reusable FBX coordinate calibration; no existing Blender file is opened or changed."""
import bpy
from pathlib import Path
from mathutils import Matrix

# The project-relative probe uses known non-symmetric dimensions to detect axis swaps and sign flips.
output = Path(__file__).resolve().parents[2] / "Art/Whitebox/FBX/SM_WB_AxisProbe.fbx"
print("[Whitebox] call export_axis_probe", flush=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.context.scene.unit_settings.system = "METRIC"
bpy.context.scene.unit_settings.scale_length = 1.0
bpy.ops.mesh.primitive_cube_add(size=1, location=(3,7,3))
# Probe mesh has world centre (3,7,3)m and extent (1,2,3)m; origin stays at (0,0,0).
probe = bpy.context.object
probe.name = "SM_WB_AxisProbe"
probe.dimensions = (2,4,6)
bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
bpy.context.scene.cursor.location = (0,0,0)
bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
# Same export-only Y correction as production meshes; reverse winding to preserve outward faces.
probe.data.transform(Matrix.Diagonal((1,-1,1,1)))
probe.data.flip_normals()
bpy.ops.export_scene.fbx(filepath=str(output),use_selection=True,object_types={"MESH"},
    bake_anim=False,axis_forward="-Y",axis_up="Z",apply_unit_scale=True,
    apply_scale_options="FBX_SCALE_UNITS",use_triangles=True,mesh_smooth_type="FACE")
