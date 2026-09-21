"""从已确认四枪源构建独立骨骼、成对换弹机械动作和手持弹匣；仅写 Art/Weapons/Animations。"""
import functools
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

# 独立输出避免覆盖已经确认的可编辑静态模型；本脚本只在独立后台 Blender 主线程运行。
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Art/Weapons/Models/Breach_Weapons.blend"
OUT = ROOT / "Art/Weapons/Animations"
# 60fps 可精确表示四个既有时长；事件比例独立保存，声音不会依赖导出帧取整。
FPS = 60
SECONDS = {"Pistol": 1.2, "Rifle": 1.4, "Shotgun": 2.2, "Sniper": 2.5}
# 各枪拔出阶段下移厘米数，后续交接再下移3cm。长弹匣只需让顶部退出机匣，不需下降整匣长度。
# Rifle/Shotgun依据真实55.02cm手臂链与UE镜内锚点校正，保留屈肘余量，禁止靠拉伸手臂弥补旧深轨迹。
# 手枪现在从底盖抓取、狙击采用底部侧握；缩短不必要的下探行程，使真实握点在原骨长内可达。
# 完全退出仍由真实弹匣顶点与机匣开口检查，不能用隐藏或缩放弹匣代替。
MAGAZINE_WITHDRAW_CM = {"Pistol": 12, "Rifle": 6, "Shotgun": 8, "Sniper": 7}
# 固定阶段是手臂、机械动画和声音之间的契约；所有数值范围 0..1。
STAGES = {
    "Tactical": {"reach": .10, "detach": .20, "withdrawn": .32, "approach": .52,
                 "attach": .60, "seat": .68, "ready": .92},
    "Empty": {"reach": .08, "detach": .16, "withdrawn": .28, "approach": .43,
              "attach": .50, "seat": .57, "ready": .96},
}
# 转换只在建立独立骨骼源时执行一次；FBX按已验证敌人骨骼管线转换回 UE厘米/+X前/+Y右/+Z上。
SOURCE_TO_BLENDER = Matrix.Diagonal((100.0, -100.0, 100.0, 1.0))


def traced(function):
    """function 为同步构建函数；捕获至进程结束，无线程边界或跨场景延迟回调。"""
    print(f"[WeaponReload] register {function.__name__}", flush=True)

    @functools.wraps(function)
    def wrapped(*args, **kwargs):
        """args/kwargs 仅用于本次主线程调用；函数名日志覆盖包括逐帧采样在内的全部入口。"""
        print(f"[WeaponReload] call {function.__name__}", flush=True)
        return function(*args, **kwargs)
    return wrapped


@traced
def ue_point(value):
    """value 为 UE模型空间厘米三元组；返回 Blender厘米坐标，禁止再次乘100。"""
    return Vector((value[0], -value[1], value[2]))


@traced
def ue_list(value):
    """value 为 Blender厘米位置；用于写入独立于 Blender对象生命周期的JSON元数据。"""
    return [float(value[0]), float(-value[1]), float(value[2])]


@traced
def rigid_part_name(model, name):
    """model/name 为当前枪型/原始零件名；返回真实机械骨，未知零件固定在body而非猜测蒙皮。"""
    if name.startswith(("Detachable magazine", "Magazine ")):
        return "magazine"
    if model == "Pistol":
        if name.startswith("Grip heel"):
            return "magazine"
        if name.startswith(("Slide", "Ejection port", "Sight", "Front sight", "Front cyan")):
            return "slide"
    if model == "Sniper" and name.startswith("Port bolt"):
        # 狙击的排壳口饰板固定在机匣，不能随栓柄绕X旋转后穿过整个机匣。
        return "body"
    if name.startswith(("Bolt stem", "Bolt knob", "Port bolt")):
        return "bolt"
    return "body"


@traced
def add_box(name, center, size, material, bone, parts):
    """name为补充机械件；center/size使用UE厘米，material借用共享材质，bone/parts记录刚性归属。"""
    bpy.ops.mesh.primitive_cube_add(size=1, location=ue_point(center))
    # obj仅属于当前新骨骼场景；应用变换后各源零件均以武器原点为共同局部坐标。
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.data.materials.append(material)
    # bevel应用后产生的顶点统一赋权，不依赖修改器自动插值。
    bevel = obj.modifiers.new("Mechanical edge", "BEVEL")
    bevel.width = .075
    bevel.segments = 2
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    parts.append((obj, bone))
    return obj


@traced
def prepare_parts(model):
    """读取model独立零件并应用源倒角，返回厘米镜像网格/刚性骨归属；绝不保存回旧Models文件。"""
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    # source_group/source_parts在本次文件内有效；解除集合隐藏后才求值修改器。
    source_group = bpy.data.collections[f"Weapon_{model}"]
    source_group.hide_viewport = False
    source_group.hide_render = False
    source_parts = list(source_group.objects)
    bpy.context.view_layer.update()
    # depsgraph只在当前文件生效，复制后的数据不再依赖它。
    depsgraph = bpy.context.evaluated_depsgraph_get()
    parts = []
    for original in source_parts:  # original为原件借用引用；复制网格保留PBR材质和已求值倒角。
        if original.type != "MESH":
            continue
        # evaluated/data/object分别是求值对象、独立网格和导出源对象，不共享原始顶点缓冲。
        evaluated = original.evaluated_get(depsgraph)
        data = bpy.data.meshes.new_from_object(evaluated, preserve_all_data_layers=True, depsgraph=depsgraph)
        data.transform(SOURCE_TO_BLENDER @ original.matrix_world)
        data.flip_normals()
        if model == "Sniper" and original.name.startswith(("Bolt stem", "Bolt knob")):
            # 原静态概念栓柄在左侧；新骨骼源移至右侧让右手拉栓符合已确认动作，旧Models保持不变。
            data.transform(Matrix.Diagonal((1, -1, 1, 1)))
            data.flip_normals()
        obj = bpy.data.objects.new(f"Reload_{original.name}", data)
        bpy.context.scene.collection.objects.link(obj)
        parts.append((obj, rigid_part_name(model, original.name)))
    # 只删除当前后台加载副本中的旧对象，磁盘静态源保持原样；集合也仅属于这个独立场景。
    keep = {obj for obj, unused_bone in parts}
    for old in tuple(bpy.data.objects):
        if old not in keep:
            bpy.data.objects.remove(old, do_unlink=True)
    for old_collection in tuple(bpy.data.collections):
        if not old_collection.objects:
            bpy.data.collections.remove(old_collection)
    # body/steel为源文件共享材质；新机械小件继续使用同族表面。
    body = bpy.data.materials["M_Breach_Body"]
    steel = bpy.data.materials["M_Breach_Steel"]
    if model == "Pistol":
        # 原静态枪只建了握把底板；补上原本藏在握把内的真实弹匣盒体，保证拔出时不是一片底板。
        add_box("Magazine internal box", (-3.6, 0, -3.7), (3.3, 2.2, 10.4), body, "magazine", parts)
    if model in ("Rifle", "Shotgun"):
        # 已有Port bolt太薄，补一枚可抓取拉机柄。底座仍附着枪机，不重做外壳或枪口。
        add_box("Charging handle stem", (7.8, -5.0 if model == "Shotgun" else -4.2, 11.9),
                (1.2, 2.2, .7), steel, "charging_handle", parts)
        add_box("Charging handle grip", (7.8, -6.2 if model == "Shotgun" else -5.4, 11.9),
                (2.0, .9, 1.0), body, "charging_handle", parts)
    # unit_scale与骨骼数值必须一起为厘米；对象比例恒为1，避免UE额外根缩放。
    scene = bpy.context.scene
    # 独立生成资源可幂等重建，不额外制造.blend1备份；不会影响用户启动Blender的全局偏好文件。
    bpy.context.preferences.filepaths.save_version = 0
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = .01
    scene.render.fps = FPS
    scene.render.fps_base = 1
    scene.frame_start = 1
    scene.frame_end = 1 + round(SECONDS[model] * FPS)
    return parts


@traced
def bounds_for_parts(parts, bone=None):
    """parts为当前独立网格列表，bone可限制机械部件；返回UE厘米包围盒，空集合视为制作错误。"""
    # points是纯数值列表；源对象局部已烘焙成共同武器坐标，因此无需再乘对象矩阵。
    points = [ue_list(vertex.co) for obj, owner in parts if bone is None or owner == bone for vertex in obj.data.vertices]
    if not points:
        raise RuntimeError(f"Missing rigid part for bone {bone}")
    return [[min(point[axis] for point in points) for axis in range(3)],
            [max(point[axis] for point in points) for axis in range(3)]]


@traced
def create_rig(model, parts):
    """为model建立单根骨架；parts每个顶点只绑定一骨，枪根与body恒定，返回rig和纯数据骨清单。"""
    # magazine_bounds/center固定弹匣旋转中心，同时作为手持表现网格的导出原点。
    magazine_bounds = bounds_for_parts(parts, "magazine")
    magazine_center = [(magazine_bounds[0][axis] + magazine_bounds[1][axis]) * .5 for axis in range(3)]
    bones = [{"name": "weapon_root", "parent": None, "head_cm": [0, 0, 0]},
             {"name": "body", "parent": "weapon_root", "head_cm": [0, 0, 0]},
             {"name": "magazine", "parent": "body", "head_cm": magazine_center}]
    if model == "Pistol":
        bones.append({"name": "slide", "parent": "body", "head_cm": [5.4, 0, 10.7]})
    else:
        bones.append({"name": "bolt", "parent": "body", "head_cm": [-2.7, 3.4, 9.2] if model == "Sniper" else [7.8, 0, 11.9]})
        if model != "Sniper":
            bones.append({"name": "charging_handle", "parent": "bolt", "head_cm": [7.8, -5.4 if model == "Rifle" else -6.2, 11.9]})
    bpy.ops.object.select_all(action="DESELECT")
    # Armature为UE识别的对象容器名称，避免其导入成多余的骨骼层。
    data = bpy.data.armatures.new(f"Rig_Breach_{model}")
    rig = bpy.data.objects.new("Armature", data)
    bpy.context.scene.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for definition in bones:  # 父骨在前，全部骨朝+Z，游戏挂点再根据实际参考轴转换。
        joint = data.edit_bones.new(definition["name"])
        joint.head = ue_point(definition["head_cm"])
        joint.tail = joint.head + Vector((0, 0, 2))
        joint.use_deform = True
        if definition["parent"]:
            joint.parent = data.edit_bones[definition["parent"]]
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.show_in_front = True
    for obj, owner in parts:  # obj/owner为当前刚性零件和其唯一归属，无自动权重或运行时布料。
        if owner not in data.bones:
            raise RuntimeError(f"Unknown mechanical bone {obj.name}: {owner}")
        obj.parent = rig
        group = obj.vertex_groups.new(name=owner)
        group.add(list(range(len(obj.data.vertices))), 1, "REPLACE")
        modifier = obj.modifiers.new("Rigid mechanical skin", "ARMATURE")
        modifier.object = rig
    return rig, bones, magazine_center


@traced
def envelope(time, keys):
    """time为归一化动作进度，keys为按时间递增的标量关键点；平滑插值抑制机械运动过冲。"""
    if time <= keys[0][0]:
        return keys[0][1]
    for index in range(1, len(keys)):  # index仅在相邻两关键点间插值，不引入跨段贝塞尔过冲。
        left, right = keys[index - 1], keys[index]
        if time <= right[0]:
            ratio = (time - left[0]) / (right[0] - left[0])
            smooth = ratio * ratio * (3 - 2 * ratio)
            return left[1] + (right[1] - left[1]) * smooth
    return keys[-1][1]


@traced
def set_joint(rig, name, translation=(0, 0, 0), angle_x=0, angle_y=0):
    """rig/name指定骨，translation为UE厘米，角度为模型空间度；姿势必须先重置再单次赋值。"""
    # bone/reference为本帧骨与固定参考旋转；镜像Y后，X/Z轴转角也需取反以维持UE朝向。
    bone = rig.pose.bones[name]
    reference = bone.bone.matrix_local.to_quaternion()
    bone.location = reference.inverted() @ ue_point(translation)
    bone.rotation_quaternion = reference.inverted() @ Quaternion(Vector((1, 0, 0)), math.radians(-angle_x)) @ Quaternion(Vector((0, 1, 0)), math.radians(angle_y)) @ reference


@traced
def sample_pose(rig, model, variant, time):
    """每帧根据model/variant与归一化time重建完整机械姿势；无增量累计，取消后可直接复位。"""
    for joint in rig.pose.bones:  # 所有通道均显式归零，确保根不动且旧动作不会污染新动作。
        joint.rotation_mode = "QUATERNION"
        joint.rotation_quaternion = (1, 0, 0, 0)
        joint.location = (0, 0, 0)
        joint.scale = (1, 1, 1)
    # stages定义跨网格接口；travel依据弹匣顶部退出机匣的实际净空与手臂可达范围，单位厘米。
    stages = STAGES[variant]
    travel = MAGAZINE_WITHDRAW_CM[model]
    distance = envelope(time, [(0, 0), (stages["detach"] - .03, 0), (stages["detach"], 1),
        (stages["withdrawn"], travel), (stages["withdrawn"] + .07, travel + 3),
        (stages["approach"], 5), (stages["attach"], 1), (stages["seat"] - .035, 0),
        (stages["seat"], .25), (stages["seat"] + .025, 0), (1, 0)])
    lateral = envelope(time, [(0, 0), (stages["detach"], 0), (stages["withdrawn"], -5),
        (stages["withdrawn"] + .07, -7), (stages["approach"], -1), (stages["attach"], 0), (1, 0)])
    tilt = envelope(time, [(0, 0), (stages["detach"], 0), (stages["withdrawn"], -12),
        (stages["withdrawn"] + .07, -16), (stages["approach"], -3), (stages["attach"], 0), (1, 0)])
    set_joint(rig, "magazine", (-distance * .07, lateral, -distance), angle_y=tilt)
    if variant == "Empty":
        if model == "Pistol":
            # 空仓套筒从首帧保持后锁；.78的SlideRelease与向前撞止动声严格对齐。
            slide = envelope(time, [(0, -3.6), (.765, -3.6), (.78, 0), (1, 0)])
            set_joint(rig, "slide", (slide, 0, 0))
        elif model in ("Rifle", "Shotgun"):
            # 拉机柄附属bolt，随整枪机后拉；不重复叠加位置导致机械件速度翻倍。
            open_phase = .74 if model == "Rifle" else .76
            close_phase = .82 if model == "Rifle" else .84
            bolt = envelope(time, [(0, 0), (.68, 0), (open_phase, -5.0), (close_phase - .025, -5.0), (close_phase, 0), (1, 0)])
            set_joint(rig, "bolt", (bolt, 0, 0))
        else:
            # 狙击栓先抬起解锁，再沿枪管轴后拉；关闭顺序反向，右手操作由配套手臂片段负责。
            opening = envelope(time, [(0, 0), (.62, 0), (.68, 62), (.83, 62), (.88, 0), (1, 0)])
            distance_bolt = envelope(time, [(0, 0), (.68, 0), (.74, -8), (.78, -8), (.83, 0), (1, 0)])
            set_joint(rig, "bolt", (distance_bolt, 0, 0), angle_x=opening)


@traced
def clip_events(model, variant):
    """返回声音事件名/归一化时间，供音频制作与UE配置共用；所有事件只表现，不结算弹药。"""
    # stages/events为纯元数据，保存为JSON后不依赖Blender动作对象。
    stages = STAGES[variant]
    events = [{"name": "MagOut", "phase": stages["detach"]},
              {"name": "MagIn", "phase": stages["attach"]},
              {"name": "MagSeat", "phase": stages["seat"]}]
    if variant == "Empty":
        if model == "Pistol":
            events.append({"name": "SlideRelease", "phase": .78})
        elif model in ("Rifle", "Shotgun"):
            events.extend([{"name": "ChargingHandle", "phase": .68},
                           {"name": "BoltOpen", "phase": .74 if model == "Rifle" else .76},
                           {"name": "BoltClose", "phase": .82 if model == "Rifle" else .84}])
        else:
            events.extend([{"name": "BoltOpen", "phase": .68}, {"name": "BoltClose", "phase": .88}])
    return events


@traced
def select_export(rig, meshes):
    """rig可为空、meshes为本次导出副本；显式选中防止场景相机或未隐藏源件进入FBX。"""
    bpy.ops.object.select_all(action="DESELECT")
    for mesh in meshes:
        mesh.select_set(True)
    if rig is not None:
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
    elif meshes:
        bpy.context.view_layer.objects.active = meshes[0]


@traced
def export_fbx(path, rig, meshes, animated):
    """path只允许专属输出目录；rig/meshes借用至同步导出结束，animated控制是否烘焙当前动作。"""
    select_export(rig, meshes)
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True,
        object_types={"ARMATURE", "MESH"} if rig is not None else {"MESH"},
        axis_forward="-Y", axis_up="Z", apply_unit_scale=True, apply_scale_options="FBX_SCALE_UNITS",
        add_leaf_bones=False, use_armature_deform_only=True, use_mesh_modifiers=True,
        use_triangles=True, mesh_smooth_type="FACE", bake_anim=animated,
        bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
        bake_anim_step=1, bake_anim_simplify_factor=0)


@traced
def joined_copy(parts, name, rig=None, offset=None):
    """parts为待合并刚性件；name为资产名，rig为空导出静态弹匣，offset为UE厘米原点偏移。"""
    # copies生命周期仅为本次FBX导出；去掉Armature防止动作当前姿势烘焙到参考顶点。
    copies = []
    bpy.ops.object.select_all(action="DESELECT")
    for original, unused_bone in parts:
        obj = original.copy()
        obj.data = original.data.copy()
        obj.parent = None
        obj.matrix_world = Matrix.Identity(4)
        obj.modifiers.clear()
        bpy.context.scene.collection.objects.link(obj)
        obj.select_set(True)
        copies.append(obj)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    joined = bpy.context.object
    joined.name = name
    if offset is not None:
        joined.data.transform(Matrix.Translation(-ue_point(offset)))
        joined.vertex_groups.clear()
    # 全枪UV单独展开，旧源UV不受影响；当前材质为无纹理PBR也保留未来贴图入口。
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.009)
    bpy.ops.object.mode_set(mode="OBJECT")
    if rig is not None:
        joined.parent = rig
        modifier = joined.modifiers.new("Export rigid skin", "ARMATURE")
        modifier.object = rig
    return joined


@traced
def build_clips(model, rig, magazine_center, parts):
    """为model烘焙两机械Sequence；parts用于真实弹匣退出净空，返回阶段/声音/抓点元数据，不结算补弹。"""
    # scene/entries属于本武器场景；前一场景的对象不会传入本函数。
    scene = bpy.context.scene
    entries = []
    for variant in ("Tactical", "Empty"):
        action = bpy.data.actions.new(f"A_Breach_{model}_Reload_{variant}")
        action.use_fake_user = True
        rig.animation_data_create()
        rig.animation_data.action = action
        for frame in range(scene.frame_start, scene.frame_end + 1):
            # time包含精确0/1端点；键入位置/旋转/比例，使根恒定可以在UE侧逐帧验收。
            time = (frame - scene.frame_start) / (scene.frame_end - scene.frame_start)
            scene.frame_set(frame)
            sample_pose(rig, model, variant, time)
            for joint in rig.pose.bones:
                joint.keyframe_insert(data_path="rotation_quaternion", frame=frame, group=joint.name)
                joint.keyframe_insert(data_path="location", frame=frame, group=joint.name)
                joint.keyframe_insert(data_path="scale", frame=frame, group=joint.name)
            if rig.pose.bones["weapon_root"].location.length > 1e-6 or rig.pose.bones["body"].location.length > 1e-6:
                raise RuntimeError(f"Moving weapon root: {model}/{variant}/{frame}")
        export_fbx(OUT / "FBX" / f"{action.name}.fbx", rig, [], True)
        # hand_targets保存弹匣中心与机械操作点，用于手臂作者匹配；位置在稳定武器锚点空间。
        hand_targets = []
        for time in sorted({0.0, 1.0, *STAGES[variant].values(), .68, .74, .78, .82, .84, .88}):
            sample_pose(rig, model, variant, time)
            bpy.context.view_layer.update()
            joint = rig.pose.bones["magazine"]
            center = joint.matrix @ joint.bone.matrix_local.inverted() @ ue_point(magazine_center)
            hand_targets.append({"phase": time, "magazine_center_cm": ue_list(center)})
        # 在“完全拔出”时检查所有弹匣顶点已低于机匣开口；缩短路径不能让弹匣仍插在外壳内。
        sample_pose(rig, model, variant, STAGES[variant]["withdrawn"])
        bpy.context.view_layer.update()
        magazine_joint = rig.pose.bones["magazine"]
        magazine_transform = magazine_joint.matrix @ magazine_joint.bone.matrix_local.inverted()
        magazine_top_cm = max((magazine_transform @ vertex.co).z for obj, owner in parts if owner == "magazine" for vertex in obj.data.vertices)
        opening_z_cm = -9.7 if model == "Pistol" else 3.0
        withdrawn_clearance_cm = opening_z_cm - magazine_top_cm
        if withdrawn_clearance_cm < .3:
            raise RuntimeError(f"Magazine not fully withdrawn: {model}/{variant} clearance={withdrawn_clearance_cm}cm")
        entries.append({"name": action.name, "variant": variant, "seconds": SECONDS[model],
            "frames": scene.frame_end - scene.frame_start, "fps": FPS, "loop": False,
            "stages": STAGES[variant], "sound_events": clip_events(model, variant), "hand_targets": hand_targets,
            "withdrawn_clearance_cm": withdrawn_clearance_cm, "magazine_top_at_withdraw_cm": magazine_top_cm,
            "magazine_opening_z_cm": opening_z_cm})
    return entries


@traced
def render_previews(model, rig, entry):
    """渲染真实骨骼姿势的中立/弹匣抽出/空仓机械操作三帧，供制作与UE导入后的对照验收。"""
    # scene/bounds/span为当前场景设置；相机按实际长度/弹匣位移留边，不用概念图替代实模。
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 1200
    scene.render.resolution_y = 760
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.world.color = (.045, .065, .079)
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "BOTH"
    scene.display.shading.background_type = "WORLD"
    scene.view_settings.view_transform = "Standard"
    bounds = entry["bounds_cm"]
    span = max(bounds[1][0] - bounds[0][0], 65)
    target = ue_point(((bounds[0][0] + bounds[1][0]) * .5, 0, -4))
    bpy.ops.object.camera_add(location=target + Vector((span * .4, span * 1.65, span * .48)))
    camera = bpy.context.object
    camera.name = f"Preview_{model}"
    camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = span * 1.28
    camera.data.clip_end = 10000
    scene.camera = camera
    for variant, time, label in (("Tactical", 0, "Neutral"), ("Tactical", .32, "MagazineOut"),
                                  ("Empty", .74 if model != "Pistol" else .70, "Mechanism")):
        rig.animation_data.action = bpy.data.actions[f"A_Breach_{model}_Reload_{variant}"]
        scene.frame_set(round(1 + time * SECONDS[model] * FPS))
        scene.render.filepath = str(OUT / "Previews" / f"{model}_{label}.png")
        bpy.ops.render.render(write_still=True)
    rig.animation_data.action = bpy.data.actions[f"A_Breach_{model}_Reload_Tactical"]
    scene.frame_set(1)


@traced
def validate_exports():
    """重新读取实际FBX检查厘米边界、刚性权重、骨层级和动作首尾；返回纯数据QA，不代替UE视觉验收。"""
    # manifest/records仅用于这一轮离线验证；所有读取路径来自本脚本专属输出。
    manifest = json.loads((OUT / "weapon_animation_manifest.json").read_text(encoding="utf-8"))
    records = []
    for entry in manifest["weapons"]:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.context.scene.unit_settings.system = "METRIC"
        bpy.context.scene.unit_settings.scale_length = .01
        bpy.ops.import_scene.fbx(filepath=str(OUT / "FBX" / f"{entry['mesh']}.fbx"), use_anim=False)
        # rigs/meshes是实际FBX重导入对象，禁止从原源对象取代检验。
        rigs = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
        meshes = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
        if len(rigs) != 1 or len(meshes) != 1:
            raise RuntimeError(f"FBX object count mismatch {entry['model']}: {len(rigs)}/{len(meshes)}")
        rig = rigs[0]
        mesh = meshes[0]
        actual_bones = {bone.name for bone in rig.data.bones}
        expected_bones = {bone["name"] for bone in entry["bones"]}
        if actual_bones != expected_bones:
            raise RuntimeError(f"FBX bone names changed {entry['model']}: {actual_bones}")
        for definition in entry["bones"]:  # 检查父骨而不只检查名称，避免同名骨以错误层级导入。
            bone = rig.data.bones[definition["name"]]
            if (bone.parent.name if bone.parent else None) != definition["parent"]:
                raise RuntimeError(f"FBX hierarchy mismatch {entry['model']}/{bone.name}")
        for vertex in mesh.data.vertices:
            if len(vertex.groups) != 1 or abs(vertex.groups[0].weight - 1) > 1e-5:
                raise RuntimeError(f"FBX nonrigid vertex {entry['model']}/{vertex.index}")
        positions = [ue_list(mesh.matrix_world @ vertex.co) for vertex in mesh.data.vertices]
        bounds = [[min(point[axis] for point in positions) for axis in range(3)],
                  [max(point[axis] for point in positions) for axis in range(3)]]
        error_cm = max(abs(bounds[side][axis] - entry["bounds_cm"][side][axis]) for side in (0, 1) for axis in range(3))
        if error_cm > .02:
            raise RuntimeError(f"FBX unit/axis bounds mismatch {entry['model']}: {error_cm}cm")
        # 动作从各自FBX重新载入；根矩阵逐帧恒定，末帧所有机械骨应恢复参考局部姿势。
        clips = []
        for clip in entry["clips"]:
            bpy.ops.wm.read_factory_settings(use_empty=True)
            bpy.context.scene.unit_settings.system = "METRIC"
            bpy.context.scene.unit_settings.scale_length = .01
            bpy.context.scene.render.fps = FPS
            bpy.ops.import_scene.fbx(filepath=str(OUT / "FBX" / f"{clip['name']}.fbx"), use_anim=True)
            rig = next(obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE")
            action = rig.animation_data.action
            start, end = (int(round(value)) for value in action.frame_range)
            if abs((end - start) / FPS - clip["seconds"]) > .001:
                raise RuntimeError(f"FBX duration mismatch {clip['name']}: {start}..{end}")
            bpy.context.scene.frame_set(start)
            root_matrix = rig.pose.bones["weapon_root"].matrix.copy()
            moved = False
            for frame in range(start, end + 1):
                bpy.context.scene.frame_set(frame)
                root_delta = rig.pose.bones["weapon_root"].matrix - root_matrix
                if max(abs(component) for row in root_delta for component in row) > 1e-4:
                    raise RuntimeError(f"FBX root animation {clip['name']}/{frame}")
                if rig.pose.bones["magazine"].location.length > 3:
                    moved = True
                for joint in rig.pose.bones:
                    if max(abs(component - 1) for component in joint.scale) > 1e-4:
                        raise RuntimeError(f"FBX hidden/scaled mechanical bone {clip['name']}/{joint.name}/{frame}")
            if not moved:
                raise RuntimeError(f"FBX missing magazine animation {clip['name']}")
            for joint in rig.pose.bones:
                if joint.location.length > .01 or abs(joint.rotation_quaternion.angle) > .001:
                    raise RuntimeError(f"FBX end pose not reset {clip['name']}/{joint.name}")
            clips.append({"name": clip["name"], "seconds": (end - start) / FPS, "frames_checked": end - start + 1,
                          "constant_root": True, "constant_unit_scale": True, "neutral_endpoint": True})
        records.append({"model": entry["model"], "bones": len(expected_bones), "bounds_error_cm": error_cm,
                        "rigid_weights": True, "clips": clips})
    (OUT / "fbx_validation.json").write_text(json.dumps({"success": True, "weapons": records}, indent=2), encoding="utf-8")
    print("BREACH_WEAPON_ANIMATIONS_FBX_VALIDATION_SUCCESS", flush=True)


@traced
def main():
    """独立后台入口：构建4骨骼枪、8机械动作、4手持弹匣及清单；失败直接非零退出不伪报成功。"""
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "FBX").mkdir(exist_ok=True)
    (OUT / "Previews").mkdir(exist_ok=True)
    # source_manifest为现有模型的厘米Socket/材质权威记录；entries只包含新资源。
    source_manifest = json.loads((ROOT / "Art/Weapons/Models/weapons_manifest.json").read_text(encoding="utf-8"))
    entries = []
    for source_entry in source_manifest["weapons"]:
        model = source_entry["name"]
        parts = prepare_parts(model)
        rig, bones, magazine_center = create_rig(model, parts)
        # entry保持所有坐标为UE模型空间厘米；bone reference轴需导入后由真实参考姿势转换Socket。
        entry = {"model": model, "mesh": f"SK_Breach_{model}", "skeleton": f"SKEL_Breach_Weapon_{model}",
                 "blend": f"Breach_{model}_Reload.blend", "bones": bones, "bounds_cm": bounds_for_parts(parts),
                 "magazine_center_cm": magazine_center,
                 "magazine_withdraw_cm": MAGAZINE_WITHDRAW_CM[model],
                 "magazine_max_drop_cm": MAGAZINE_WITHDRAW_CM[model] + 3,
                 # 手腕位于弹匣左侧且略向后；实际手指接触应由共享手臂骨架与IK烘焙校正。
                 "magazine_grip_cm": [magazine_center[0] - 2, -5, magazine_center[2] - 1],
                 "magazine_grip_offset_cm": [-2, -5, -1],
                 "bolt_grip_cm": ([-2.7, 10.8, 9.0] if model == "Sniper" else [-4, -3.5, 10.8]
                                  if model == "Pistol" else [7.8, -6.2 if model == "Shotgun" else -5.4, 11.9]),
                 "mechanism_grip_bone": "slide" if model == "Pistol" else "bolt",
                 "model_changes": (["Sniper bolt handle mirrored to UE +Y for right-hand operation; original static source unchanged"]
                                   if model == "Sniper" else ["Added internal pistol magazine body"] if model == "Pistol"
                                   else ["Added graspable charging handle to existing bolt"]),
                 "magazine_visibility": "always visible with unit scale; arms bake follows magazine bone; optional duplicate mesh disabled by default",
                 "material_roles": sorted({material.name for obj, unused_bone in parts for material in obj.data.materials}),
                 "sockets": {"Muzzle": {"bone": "body", "world_cm": source_entry["muzzle_cm"]},
                             "Grip_L": {"bone": "body", "world_cm": source_entry["grip_l_cm"]},
                             "Sight": {"bone": "body", "world_cm": source_entry["sight_cm"]},
                             "Grip_R": {"bone": "body", "world_cm": [0, 0, 0]}}}
        joined = joined_copy(parts, entry["mesh"], rig)
        joined.data.calc_loop_triangles()
        entry["triangles"] = len(joined.data.loop_triangles)
        export_fbx(OUT / "FBX" / f"{entry['mesh']}.fbx", rig, [joined], False)
        bpy.data.objects.remove(joined, do_unlink=True)
        magazine_parts = [(obj, owner) for obj, owner in parts if owner == "magazine"]
        magazine_name = f"SM_Breach_{model}_Magazine"
        joined = joined_copy(magazine_parts, magazine_name, offset=magazine_center)
        export_fbx(OUT / "FBX" / f"{magazine_name}.fbx", None, [joined], False)
        bpy.data.objects.remove(joined, do_unlink=True)
        magazine_bounds = bounds_for_parts(parts, "magazine")
        entry["magazine_presentation"] = {"mesh": magazine_name, "origin_cm": magazine_center,
            "bounds_local_cm": [[magazine_bounds[side][axis] - magazine_center[axis] for axis in range(3)] for side in (0, 1)],
            "size_cm": [magazine_bounds[1][axis] - magazine_bounds[0][axis] for axis in range(3)]}
        entry["clips"] = build_clips(model, rig, magazine_center, parts)
        # 部件表保留艺术可维护性；rigid_bone让后续补细节时能核查是否错误绑定整个body。
        entry["parts"] = [{"name": obj.name, "rigid_bone": owner, "vertices": len(obj.data.vertices)} for obj, owner in parts]
        render_previews(model, rig, entry)
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT / entry["blend"]))
        entries.append(entry)
        # 每枪保存一次manifest便于并行的手臂/UE制作读取，不把部分结果声称全部完成。
        (OUT / "weapon_animation_manifest.json").write_text(json.dumps({"version": 1, "units": "centimetres",
            "coordinates": "UE +X forward +Y right +Z up", "root_motion": False, "fps": FPS,
            "materials_directory": "/Game/Weapons/Materials/Breach", "weapons": entries}, indent=2), encoding="utf-8")
    validate_exports()
    print("BREACH_WEAPON_ANIMATIONS_BUILD_SUCCESS meshes=4 clips=8 magazines=4", flush=True)


if __name__ == "__main__":
    # --validate-only用于不重做资源的只读FBX回归；仅更新专属验证报告，不重写Blend/FBX。
    if "--validate-only" in sys.argv:
        validate_exports()
    else:
        main()
