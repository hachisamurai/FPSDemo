"""UE5.4 commandlet：导入 Blender 四武器，重建共享 PBR 材质/挂点/LOD，仅修改 BP 外观配置。"""
import json
from pathlib import Path
import unreal

# 项目源文件与专用资产命名空间；不改模板枪、地图或其他材质。
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Art/Weapons/Models"
DEST = "/Game/Weapons/Meshes/Breach"
MATERIALS = "/Game/Weapons/Materials/Breach"


def save(asset):
    """asset 由 Editor 包拥有；保存失败中止，避免把内存中的导入误报为交付成功。"""
    unreal.log(f"[BreachImport] save {asset.get_path_name()}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Cannot save {asset.get_path_name()}")


def material(name, values):
    """name 为清单材质键；values 是线性 RGBA、金属度、粗糙度，同步 Blender 与 UE 的 PBR 参数。"""
    unreal.log(f"[BreachImport] material {name}")
    # asset 是本脚本专属材质，重跑允许重建其节点；其他路径不受影响。
    asset = unreal.load_asset(f"{MATERIALS}/M_Breach_{name}")
    if not asset:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(f"M_Breach_{name}", MATERIALS, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(asset, unreal.Material):
        raise TypeError(name)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(asset)
    # color 为线性基础色常量；标量只影响外观，不作为弹药类型/技能数值。
    color = unreal.MaterialEditingLibrary.create_material_expression(asset, unreal.MaterialExpressionConstant3Vector, -350, 0)
    color.set_editor_property("constant", unreal.LinearColor(*values[0]))
    unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for index, (property_name, value) in enumerate(((unreal.MaterialProperty.MP_METALLIC, values[1]), (unreal.MaterialProperty.MP_ROUGHNESS, values[2]))):
        # scalar 归材质图所有；index 仅用于整理节点布局。
        scalar = unreal.MaterialEditingLibrary.create_material_expression(asset, unreal.MaterialExpressionConstant, -350, 150+index*100)
        scalar.set_editor_property("r", value)
        unreal.MaterialEditingLibrary.connect_material_property(scalar, "", property_name)
    if name == "Cyan":
        # 微弱自发光标识使背光环境仍可读，不影响场景主照明。
        unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(asset)
    save(asset)
    return asset


def import_weapon(entry, palette):
    """entry 为厘米制几何/挂点清单，palette 为已保存材质；返回验证过尺寸、材质、LOD 的静态网格。"""
    unreal.log(f"[BreachImport] import_weapon {entry['name']}")
    # task/options 只在本次导入使用；禁止材质自动复制，FBX 中的槽名用于绑定共享材质。
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE / "FBX" / f"{entry['asset']}.fbx")
    task.destination_path = DEST
    task.destination_name = entry["asset"]
    task.automated = True
    task.replace_existing = True
    task.save = False
    options = unreal.FbxImportUI()
    options.import_mesh = True
    options.import_as_skeletal = False
    options.import_materials = False
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
    # mesh 由包持有；box_cm 与源尺寸逐轴比对，同时检查非对称狙击枪拉机柄的 Y 朝向。
    mesh = unreal.load_asset(f"{DEST}/{entry['asset']}")
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Import failed {entry['asset']}")
    box_cm = mesh.get_bounding_box()
    for actual, expected in zip((box_cm.min, box_cm.max), entry["bounds_cm"]):
        for axis, coordinate in zip((actual.x, actual.y, actual.z), expected):
            if abs(axis-coordinate) > .15:
                raise RuntimeError(f"Bounds/unit/axis mismatch {entry['asset']}: {box_cm}")
    for index, slot in enumerate(mesh.get_editor_property("static_materials")):
        # 槽名来自 FBX，不能用导出时不稳定的索引假定材质排列。
        key = str(slot.material_slot_name).removeprefix("M_Breach_")
        if key not in palette:
            raise RuntimeError(f"Unknown material slot {key}")
        mesh.set_material(index, palette[key])
    for name, location in (("Muzzle", entry["muzzle_cm"]), ("Grip_L", entry["grip_l_cm"]), ("Sight", entry["sight_cm"])):
        # socket 归该网格所有，重跑复用同名对象；局部 +X 朝前且单位厘米。
        socket = mesh.find_socket(name)
        if not socket:
            socket = unreal.StaticMeshSocket(outer=mesh)
            socket.set_editor_property("socket_name", name)
            mesh.add_socket(socket)
        socket.set_editor_property("relative_location", unreal.Vector(*location))
        socket.set_editor_property("relative_rotation", unreal.Rotator())
        socket.set_editor_property("relative_scale", unreal.Vector(1, 1, 1))
    # LOD0 完整、LOD1 50%、LOD2 25%；固定屏幕阈值让第一人称主视图保留足够细节。
    editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    reductions = unreal.StaticMeshReductionOptions()
    reductions.auto_compute_lod_screen_size = False
    reductions.reduction_settings = [unreal.StaticMeshReductionSettings(percent_triangles=1.0, screen_size=1.0),
                                     unreal.StaticMeshReductionSettings(percent_triangles=.5, screen_size=.15),
                                     unreal.StaticMeshReductionSettings(percent_triangles=.25, screen_size=.06)]
    if editor.set_lods(mesh, reductions) < 0 or editor.get_lod_count(mesh) != 3:
        raise RuntimeError(f"LOD generation failed {entry['asset']}")
    # 一只简单盒碰撞供未来拾取预览使用；运行时持有武器组件始终禁用碰撞。
    editor.remove_collisions(mesh)
    editor.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)
    save(mesh)
    unreal.log(f"BREACH_MESH_VALID {entry['asset']} bounds={box_cm} lods={editor.get_lod_count(mesh)} triangles={entry['triangles']}")
    return mesh


def configure_blueprint(name, mesh):
    """name 为四个现有 BP 后缀，mesh 为已验证资产；只写外观与握持变换，保留伤害/弹药/音效/UI 图标。"""
    unreal.log(f"[BreachImport] configure_blueprint {name}")
    # BP/CDO/config 为当前编辑器对象与值副本；编译后重新读取验证，避免旧 CDO 引用。
    bp = unreal.load_asset(f"/Game/Weapons/Blueprints/BP_Weapon_{name}")
    if not isinstance(bp, unreal.Blueprint):
        raise RuntimeError(f"Missing weapon Blueprint {name}")
    cdo = unreal.get_default_object(bp.generated_class())
    config = cdo.get_editor_property("config")
    config.set_editor_property("static_mesh", mesh)
    # 手枪使用单手展示，其他三枪恢复模板托举手；不改射击/装填时间或角色输入。
    config.set_editor_property("hide_support_arm", name == "Pistol")
    # 抵消模板 GripPoint 在待机姿势中的 P=-11.26/Y=-73.99/R=-.25 度旋转；
    # 握点位置沿用手臂；角色整体手臂偏移负责前移画面，避免只移动枪导致双手悬空。
    config.set_editor_property("attach_offset", unreal.Transform(location=[0, 0, 0],
        rotation=unreal.Rotator(pitch=2.84645, yaw=74.28632, roll=10.90161), scale=[1, 1, 1]))
    cdo.set_editor_property("config", config)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    save(bp)
    if unreal.get_default_object(bp.generated_class()).get_editor_property("config").static_mesh != mesh:
        raise RuntimeError(f"Blueprint assignment failed {name}")


def main():
    """批量入口；所有依赖均在工程内，任何导入/尺寸/写包失败会抛异常且不打印成功标记。"""
    unreal.log("[BreachImport] main")
    # Python commandlet 不像完整编辑器那样自动装载网格编辑模块；显式装载以暴露 LOD 子系统。
    unreal.load_module("StaticMeshEditor")
    # manifest 是只读源清单；palette/entry/mesh 仅在同步 Editor 主线程导入期间使用。
    manifest = json.loads((SOURCE / "weapons_manifest.json").read_text(encoding="utf-8"))
    # 快速挂接迭代只更新 BP，正常调用仍完整重建并校验网格；该开关不影响运行时玩法。
    configure_only = "-BreachConfigureOnly" in unreal.SystemLibrary.get_command_line()
    palette = {} if configure_only else {name: material(name, values) for name, values in manifest["palette"].items()}
    for entry in manifest["weapons"]:
        mesh = unreal.load_asset(f"{DEST}/{entry['asset']}") if configure_only else import_weapon(entry, palette)
        if not isinstance(mesh, unreal.StaticMesh):
            raise RuntimeError(f"Missing imported mesh {entry['asset']}")
        configure_blueprint(entry["name"], mesh)
    unreal.log("BREACH_WEAPONS_IMPORT_SUCCESS")


if __name__ == "__main__":
    main()
