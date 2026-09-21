"""离线探针仅用于给 UE 真碰撞回归选择稳定射线；读取动画源网格，不替代 Chaos 实测。"""
from pathlib import Path
import bpy
from mathutils import Vector

# 读取已制作动画源，不保存 Blender 文件或改变碰撞清单。
ROOT = Path(__file__).resolve().parents[2]


def probe(model, seconds, y, z):
    """model/seconds 选择 Boss Global 或普通 Idle 姿势；y/z 为 UE 模型空间厘米，射线从 +X 穿向 -X。"""
    print(f"[CALL] probe model={model} seconds={seconds} y={y} z={z}", flush=True)
    # depsgraph 包含当前烘焙骨骼姿势和倒角，返回最靠前的可见源实体。
    depsgraph = bpy.context.evaluated_depsgraph_get()
    start, end = Vector((400, -y, z)), Vector((-400, -y, z))  # 反射 Y 后转 Blender 世界厘米。
    hits = []  # 只记录实际网格命中，摄影棚/叠加对象无权重被排除。
    for obj in bpy.data.objects:
        if obj.type != "MESH" or not obj.vertex_groups:
            continue
        evaluated = obj.evaluated_get(depsgraph)  # 当前动画求值副本由 depsgraph 管理，不能保存或跨帧持有。
        inverse = evaluated.matrix_world.inverted()  # ray_cast 输入为对象局部空间。
        origin, target = inverse @ start, inverse @ end
        success, location, normal, index = evaluated.ray_cast(origin, (target - origin).normalized(), distance=(target - origin).length)
        if success:
            world = evaluated.matrix_world @ location  # 再转世界后按射线距离排序，无坐标尺度混用。
            hits.append(((world - start).length, obj.name, obj.vertex_groups[0].name, [round(world.x, 3), round(-world.y, 3), round(world.z, 3)]))
    hits.sort()  # 距离为首字段，无 lambda 捕获或异步生命周期。
    print(f"SOURCE_HIT_PROBE {model} seconds={seconds} yz=({y},{z}) hits={hits[:3]}", flush=True)


def main():
    """在固定时间采样预定射线；本工具的结果供真正 UE PhysicsAsset 测试使用。"""
    print("[CALL] main source hit probes", flush=True)
    for model in ("Chaser", "Warden"):
        bpy.ops.wm.open_mainfile(filepath=str(ROOT / f"Art/Enemies/Animations/Breach_{model}_Combat.blend"))
        rig = bpy.data.objects["Armature"]  # 每次打开场景重新借用，防止前一场景悬空引用。
        rig.animation_data.action = bpy.data.actions[f"A_Breach_{model}_{'Global' if model == 'Warden' else 'Idle'}"]
        for seconds in ((0, 2) if model == "Warden" else (0,)):
            bpy.context.scene.frame_set(1 + round(seconds * 24))
            bpy.context.view_layer.update()
            for y, z in (((0, 24), (0, 54), (30, 24), (-139, -53)) if model == "Warden" else ((0, -22), (0, 20), (-65, -36))):
                probe(model, seconds, y, z)


if __name__ == "__main__":
    main()
