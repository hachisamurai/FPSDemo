"""从可编辑模型的独立刚性零件导出受击凸体；不改模型、骨架或动画，厘米轴与 UE 一致。"""
import json
from pathlib import Path
import bpy
from mathutils import geometry

# 仅读取项目专属 Blender 源；输出用于编辑器资产工具，不在运行时加载 JSON。
ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "Art/Enemies/Models/enemy_hit_geometry.json"
# 装饰件由背后的实体挡弹；忽略细小螺丝/灯带可避免数百个冗余查询形状。
DECORATION = ("Recess bolt", "Bolt slot", "Rear ventilation", "Core radiator block",
              "Shoulder vent fin", "Halo charged strip", "Nozzle amber ring", "Core power ring",
              "Reactor luminous ring", "Sensor amber slit", "Crown sensor amber", "Impact rubber ridge")
# 环必须按短弧分凸体，整环取 convex hull 会封住中央孔并错误遮挡核心。
RINGS = ("Nozzle rim", "Reactor outer rim", "Inner reactor rim", "Halo structural ring",
         "Halo metallic rail", "Halo armor segment")


def ue_point(value):
    """value 是 Blender 模型空间厘米坐标；Y 反射与已有 FBX 管线保持一致。"""
    print("[CALL] ue_point", flush=True)
    return [round(value.x, 5), round(-value.y, 5), round(value.z, 5)]


def part_shapes(obj):
    """obj 是借用的未求值刚性源零件；输出凸体模型空间点集，不含倒角且不读取动画姿势。"""
    print(f"[CALL] part_shapes {obj.name}", flush=True)
    # vertices 使用对象矩阵而非 Armature 修改器，保证清单永久绑定参考姿势。
    vertices = [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
    if obj.name.startswith(RINGS):
        # 源环每个截面四点；最多约 22.5 度一段，保持孔洞且控制查询形状数量。
        steps = len(vertices) // 4 - 1
        groups = []  # 每个短弧整体凸化，角部误差不超过约 2% 内半径。
        for start in range(0, steps, 4):  # start 是当前截面，不创建跨越圆环中心的大凸体。
            groups.append(vertices[start * 4:(min(start + 4, steps) + 1) * 4])
        return groups
    if obj.name.startswith("Core shutter "):
        # 护板轮廓内缘略凹，先在源 YZ 平面三角剖分，再沿 X 挤出，保持真实能量窗口。
        count = len(vertices) // 2
        front = vertices[:count]  # 原始 hull 的首圈是前表面，对应后圈拥有同一索引。
        triangles = geometry.tessellate_polygon([front])
        result = []  # 每个六点三棱柱独立凸化，不会填补中央真实孔洞。
        for triangle in triangles:
            # Blender 5.2 返回源索引，旧版返回 Vector；两者都精确指向源轮廓，不作最近点猜测。
            indices = [point if isinstance(point, int) else front.index(point) for point in triangle]
            result.append([vertices[index] for index in indices] + [vertices[index + count] for index in indices])
        return result
    return [vertices]


def main():
    """后台同步入口，逐模型读取并验证一骨权重；只保存受击几何清单，不保存 .blend。"""
    print("[CALL] main export enemy hit geometry", flush=True)
    models = []  # 累积纯 JSON 数据，Blender 对象引用不跨 open_mainfile。
    for name in ("Chaser", "Warden"):
        bpy.ops.wm.open_mainfile(filepath=str(ROOT / f"Art/Enemies/Models/Breach_{name}_Rigged.blend"))
        shapes = []  # 当前网格全部实体凸体；每项说明骨与原始零件，便于调试来源。
        for obj in bpy.data.objects:
            if obj.type != "MESH" or not obj.vertex_groups or obj.name.startswith(DECORATION):
                continue
            if len(obj.vertex_groups) != 1:
                raise RuntimeError(f"Rigid source must have one bone: {obj.name}")
            bone = obj.vertex_groups[0].name  # 源材质/相机/诊断叠加没有权重，因此自动排除。
            for index, points in enumerate(part_shapes(obj)):
                shapes.append({"bone": bone, "part": obj.name, "piece": index,
                               "vertices_cm": [ue_point(point) for point in points]})
        models.append({"name": name, "mesh": f"SK_Breach_{name}", "shapes": shapes})
        print(f"HIT_GEOMETRY_MODEL {name} shapes={len(shapes)} bones={len(set(shape['bone'] for shape in shapes))}", flush=True)
    OUTPUT.write_text(json.dumps({"version": 1, "units": "centimetres", "space": "UE_mesh_reference",
                                  "models": models}, indent=2), encoding="utf-8")
    print(f"BREACH_HIT_GEOMETRY_SUCCESS {OUTPUT}", flush=True)


if __name__ == "__main__":
    main()
