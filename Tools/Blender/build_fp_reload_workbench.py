"""从只读UE手臂FBX与Breach骨骼枪创建可编辑联合布局工作台；不改Template或运行C++。"""
import functools
import json
import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector, Quaternion

# 工作台及诊断均独占美术目录；UE导出资源只读，旧静态/骨骼源不覆盖。
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Art/Player/Animations"
GUNS = ROOT / "Art/Weapons/Animations"
MODELS = ("Pistol", "Rifle", "Shotgun", "Sniper")
# 实际枪托部件白名单；不把机匣、握把或整枪总包围盒误算成实心枪托。
STOCK_PREFIXES = ("Reload_Stock extension", "Reload_Stock upper", "Reload_Stock lower",
                  "Reload_Shoulder butt", "Reload_Butt pad", "Reload_Cheek rest", "Reload_Stock latch")
# 骨骼枪源制作时已镜像Y；JSON一律转回UE厘米/+X前/+Y右/+Z上。
GUN_BLENDER_TO_UE = Matrix.Diagonal((1, -1, 1, 1))
# 当前UE快照是重设计前只读导出；候选参数由root维护的JSON统一驱动，不在Blender复制另一份数值。
CURRENT = OUT / "CurrentUE"
PROFILE = OUT / "fp_pose_profiles.json"


def traced(function):
    """捕获同步构建函数至进程结束；所有Blender数据访问均在当前主线程，无异步悬空引用。"""
    print(f"[FPWorkbench] register {function.__name__}", flush=True)

    @functools.wraps(function)
    def wrapped(*args, **kwargs):
        """args/kwargs为本次主线程参数，function是装饰器捕获的函数，调用日志不改输入资产。"""
        print(f"[FPWorkbench] call {function.__name__}", flush=True)
        return function(*args, **kwargs)
    return wrapped


@traced
def vector_values(value):
    """value是临时向量，只返回厘米或无量纲纯数值列表，避免JSON保存UObject/Blender对象引用。"""
    return [float(component) for component in value]


@traced
def obb_for_vertices(points):
    """points为真实部件UE厘米顶点；枪托沿Y挤出，XZ主轴PCA保留其斜率，避免AABB填满空隙。"""
    # values/covariance/eigenvectors只用于本次离线OBB，真实三角面同时导出供命中复核。
    values = np.asarray(points, dtype=np.float64)
    centered = values[:, (0, 2)] - values[:, (0, 2)].mean(axis=0)
    covariance = centered.T @ centered / len(centered)
    eigenvalues, eigenvectors = np.linalg.eigh(covariance)
    long_axis = eigenvectors[:, int(np.argmax(eigenvalues))]
    if long_axis[0] < 0:
        long_axis = -long_axis
    # 三轴右手正交，Quaternion能直接用于UE Transform；Y保持真实挤出方向不混入不稳定PCA薄轴。
    axis_x = np.array((long_axis[0], 0, long_axis[1]))
    axis_y = np.array((0, 1, 0))
    axis_z = np.cross(axis_x, axis_y)
    rotation = np.column_stack((axis_x, axis_y, axis_z))
    coordinates = values @ rotation
    minimum = coordinates.min(axis=0)
    maximum = coordinates.max(axis=0)
    center = rotation @ ((minimum + maximum) * .5)
    half_extent = (maximum - minimum) * .5
    quaternion = Matrix(rotation.tolist()).to_quaternion()
    # 顶点必须在生成的盒内，拒绝轴/中心计算错误；保留极小数值容差。
    error = np.max(np.abs((values - center) @ rotation) - half_extent)
    if error > 1e-5:
        raise RuntimeError(f"OBB does not enclose actual mesh: {error}")
    return {"center_cm": center.tolist(), "rotation_quat_xyzw": [quaternion.x, quaternion.y, quaternion.z, quaternion.w],
            "half_extents_cm": half_extent.tolist(), "axis_x": axis_x.tolist(), "axis_y": axis_y.tolist(), "axis_z": axis_z.tolist()}


@traced
def extract_stock_proxies():
    """逐枪读取已有独立骨骼Blend，提取枪托真实零件OBB和三角网格；仅写工作台诊断JSON。"""
    OUT.mkdir(parents=True, exist_ok=True)
    # report以weapon_root参考组件空间描述，与UE骨轴局部坐标不同，挂枪时需乘实际Anchor变换。
    report = {"version": 1, "units": "centimetres", "space": "UE weapon mesh component / weapon_root reference",
              "coordinates": "+X forward +Y right +Z up", "weapons": []}
    for model in MODELS:
        source = GUNS / f"Breach_{model}_Reload.blend"
        bpy.ops.wm.open_mainfile(filepath=str(source))
        pieces = []
        for obj in bpy.context.scene.objects:
            if obj.type != "MESH" or not obj.name.startswith(STOCK_PREFIXES):
                continue
            # 金属件已经应用源倒角，所有顶点body权重为1；root/body动画恒定，直接参考顶点即真实形状。
            points = [vector_values(GUN_BLENDER_TO_UE @ obj.matrix_world @ vertex.co) for vertex in obj.data.vertices]
            obj.data.calc_loop_triangles()
            triangles = [list(triangle.vertices) for triangle in obj.data.loop_triangles]
            piece = {"name": obj.name, "bone": "body", "source": str(source.relative_to(ROOT)),
                     "vertices_cm": points, "triangles": triangles, **obb_for_vertices(points)}
            pieces.append(piece)
        if model != "Pistol" and not pieces:
            raise RuntimeError(f"Missing real stock geometry: {model}")
        report["weapons"].append({"model": model, "stock_pieces": pieces})
        print(f"FP_STOCK_PROXY_READY {model} pieces={len(pieces)}", flush=True)
    (OUT / "stock_collision_proxy.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return report


@traced
def extract_forearm_radii():
    """从原UE手臂真实蒙皮网格估计右前臂/腕截面半径；仅诊断，不改变权重或源骨架。"""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = .01
    bpy.ops.import_scene.fbx(filepath=str(OUT / "Reference/SK_Mannequin_Arms.fbx"), use_anim=False)
    # rig/mesh是实际导入对象；骨与点都变到同一Blender世界厘米空间后做距离计算。
    rig = next(obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE")
    mesh = next(obj for obj in bpy.context.scene.objects if obj.type == "MESH")
    elbow = rig.matrix_world @ rig.pose.bones["lowerarm_r"].matrix.translation
    wrist = rig.matrix_world @ rig.pose.bones["hand_r"].matrix.translation
    axis = wrist - elbow
    group_names = {group.index: group.name for group in mesh.vertex_groups}
    samples = []
    for vertex in mesh.data.vertices:
        # 只有主要属于右前臂/腕的点用于半径估计，手指/肩部不扩大整个胶囊。
        influences = [(group_names[influence.group], influence.weight) for influence in vertex.groups]
        if not influences:
            continue
        dominant_name, dominant_weight = max(influences, key=lambda item: item[1])  # 同步当前顶点比较，不跨帧捕获对象。
        if not (dominant_name.endswith("_r") and dominant_name.startswith(("lowerarm", "wrist"))):
            continue
        point = mesh.matrix_world @ vertex.co
        alpha = (point - elbow).dot(axis) / axis.length_squared
        radial = (point - (elbow + axis * alpha)).length
        samples.append({"vertex": vertex.index, "bone": dominant_name, "weight": dominant_weight, "alpha": alpha, "radius_cm": radial})
    forearm = [sample["radius_cm"] for sample in samples if .03 <= sample["alpha"] <= .82]
    distal = [sample["radius_cm"] for sample in samples if .82 < sample["alpha"] <= 1.08]
    if not forearm or not distal:
        raise RuntimeError("Missing forearm skin samples")
    report = {"units": "centimetres", "source": "Art/Player/Animations/Reference/SK_Mannequin_Arms.fbx",
              "method": "dominant lowerarm/wrist skin influences; radial distance to actual reference elbow-wrist axis",
              "axis_length_cm": axis.length, "samples": len(samples),
              "forearm_radius_max_cm": max(forearm), "forearm_radius_p99_cm": float(np.percentile(forearm, 99)),
              "wrist_radius_max_cm": max(distal), "wrist_radius_p99_cm": float(np.percentile(distal, 99)),
              "suggested_safety_margin_cm": .25, "sample_detail": samples}
    # 上臂需要独立包络；实际旧狙击动作穿托的是upperarm_twist_02_r，不能用前臂半径假定覆盖全臂。
    shoulder = rig.matrix_world @ rig.pose.bones["upperarm_r"].matrix.translation
    upper_axis = elbow - shoulder
    upper_samples = []
    for vertex in mesh.data.vertices:
        influences = [(group_names[influence.group], influence.weight) for influence in vertex.groups]
        if not influences:
            continue
        # Lambda只在本次顶点权重排序同步调用，无外部捕获或异步生命周期。
        dominant_name, dominant_weight = max(influences, key=lambda item: item[1])
        if not (dominant_name.endswith("_r") and dominant_name.startswith("upperarm")):
            continue
        point = mesh.matrix_world @ vertex.co
        alpha = (point - shoulder).dot(upper_axis) / upper_axis.length_squared
        radial = (point - (shoulder + upper_axis * alpha)).length
        upper_samples.append({"vertex": vertex.index, "bone": dominant_name, "weight": dominant_weight, "alpha": alpha, "radius_cm": radial})
    report["upperarm"] = {"axis": "upperarm_r shoulder to lowerarm_r elbow", "axis_length_cm": upper_axis.length,
                          "method": "dominant upperarm/_r influences, including original twist and corrective skin",
                          "sample_detail": upper_samples, "samples": len(upper_samples)}
    (OUT / "forearm_skin_radius.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"FP_FOREARM_RADIUS_READY forearm={max(forearm):.3f}cm wrist={max(distal):.3f}cm", flush=True)
    return report


@traced
def assign_action(rig, action):
    """rig为工作台副本、action为已加载动作；显式选Action Slot以兼容Blender5动画槽，不改源FBX。"""
    rig.animation_data_create()
    rig.animation_data.action = action
    if action is not None and len(action.slots):
        rig.animation_data.action_slot = action.slots[0]


@traced
def action_frame(action, phase):
    """action范围来自实际FBX，phase为归一化时间；返回可能为小数的帧位置以精确显示阶段。"""
    return float(action.frame_range[0]) + phase * float(action.frame_range[1] - action.frame_range[0])


@traced
def set_frame(value):
    """value为带小数的帧索引；scene只属于后台工作台，更新依赖图后再读取真实骨/蒙皮。"""
    bpy.context.scene.frame_set(math.floor(value), subframe=value - math.floor(value))
    bpy.context.view_layer.update()


@traced
def move_to_collection(obj, collection):
    """obj为工作台对象，collection为该枪集合；只迁移当前文件链接，不动源资源。"""
    for previous in tuple(obj.users_collection):
        previous.objects.unlink(obj)
    collection.objects.link(obj)


@traced
def import_arms_action(entry, collection, existing_rig=None):
    """导入entry实际UE FBX；第一动作保留网格/骨架，其余只保留Action并删除导入临时副本。"""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(CURRENT / entry["fbx"]["file"]), use_anim=True)
    created = [obj for obj in bpy.data.objects if obj not in before]
    rig = next(obj for obj in created if obj.type == "ARMATURE")
    action = rig.animation_data.action
    action.name = f"CurrentUE_{entry['model']}_{entry['variant']}"
    action.use_fake_user = True
    if existing_rig is None:
        rig.name = f"Arms_{entry['model']}"
        meshes = [obj for obj in created if obj.type == "MESH"]
        for obj in created:
            move_to_collection(obj, collection)
        for mesh in meshes:
            mesh.name = f"SK_UE_Arms_{entry['model']}"
            # FBX未携带UE材质贴图，工作台只用颜色区分外壳/内衬；保留原材质名、几何、权重与骨架。
            for material in mesh.data.materials:
                material.diffuse_color = (.12, .15, .18, 1) if "02" in material.name else (.46, .50, .53, 1)
        return rig, meshes, action
    if set(existing_rig.data.bones.keys()) != set(rig.data.bones.keys()):
        raise RuntimeError(f"Skeleton differs across UE clips: {entry['fbx']['file']}")
    for obj in created:
        bpy.data.objects.remove(obj, do_unlink=True)
    return existing_rig, [], action


@traced
def calibrate_coordinates(rig, action, entry):
    """用六个实际骨位置拟合UE组件→Blender世界刚性映射，避免猜测FBX轴或额外100倍缩放。"""
    assign_action(rig, action)
    set_frame(action_frame(action, 0))
    sample = entry["samples"][0]
    names = ("upperarm_l", "lowerarm_l", "hand_l", "upperarm_r", "lowerarm_r", "hand_r")
    ue = np.array([sample["component_bones"][name]["translation_cm"] for name in names])
    blender = np.array([vector_values(rig.matrix_world @ rig.pose.bones[name].matrix.translation) for name in names])
    ue_center = ue.mean(axis=0)
    blender_center = blender.mean(axis=0)
    left, values, right_transpose = np.linalg.svd((ue - ue_center).T @ (blender - blender_center))
    rotation = right_transpose.T @ left.T  # 保留UE到Blender必要反射，不强行把左手坐标当右手旋转。
    translation = blender_center - rotation @ ue_center
    error = float(np.max(np.linalg.norm((ue @ rotation.T + translation) - blender, axis=1)))
    if error > .1:
        raise RuntimeError(f"UE FBX coordinate calibration failed {entry['model']}: {error}cm")
    transform = Matrix.Identity(4)
    for row in range(3):
        for column in range(3):
            transform[row][column] = float(rotation[row, column])
    transform.translation = Vector(translation)
    print(f"FP_COORDINATE_CALIBRATION {entry['model']} error={error:.6f}cm det={np.linalg.det(rotation):.3f}", flush=True)
    return transform, error


@traced
def append_gun(model, arms_rig, coordinate_map, entry, collection):
    """追加现有枪械Blend的真实独立部件、骨和动作；用校准后的稳定控制骨挂枪，不依附操作手。"""
    path = GUNS / f"Breach_{model}_Reload.blend"
    with bpy.data.libraries.load(str(path), link=False) as (source, target):
        target.objects = [name for name in source.objects if name == "Armature" or name.startswith(("Reload_", "Magazine internal", "Charging handle"))]
        target.actions = [name for name in source.actions if name.startswith(f"A_Breach_{model}_Reload_")]
    objects = [obj for obj in target.objects if obj is not None]
    rig = next(obj for obj in objects if obj.type == "ARMATURE")
    rig.name = f"Gun_{model}"
    for obj in objects:
        collection.objects.link(obj)
    # Empty复制FBX真实骨矩阵，局部校准矩阵补偿FBX骨轴和源枪Y反射；不改变参考顶点/骨权重。
    anchor = bpy.data.objects.new(f"WeaponAnchor_{model}", None)
    collection.objects.link(anchor)
    anchor.empty_display_type = "ARROWS"
    anchor.empty_display_size = 5
    constraint = anchor.constraints.new("COPY_TRANSFORMS")
    constraint.target = arms_rig
    constraint.subtarget = "ik_hand_gun"
    bone_world = arms_rig.matrix_world @ arms_rig.pose.bones["ik_hand_gun"].matrix
    desired_world = coordinate_map @ Matrix(entry["samples"][0]["weapon_mesh_to_arms_component"]["matrix4x4_column_vector"]) @ GUN_BLENDER_TO_UE
    rig.parent = anchor
    rig.matrix_parent_inverse = Matrix.Identity(4)
    rig.matrix_basis = bone_world.inverted() @ desired_world
    bpy.context.view_layer.update()
    # 明确检查挂接后的枪口坐标，防止一个旋转看起来合理但根镜像了。
    origin_error = (rig.matrix_world.translation - desired_world.translation).length
    if origin_error > .1:
        raise RuntimeError(f"Gun anchor mismatch {model}: {origin_error}")
    actions = {"Tactical": bpy.data.actions[f"A_Breach_{model}_Reload_Tactical"],
               "Empty": bpy.data.actions[f"A_Breach_{model}_Reload_Empty"]}
    for action in actions.values():
        action.use_fake_user = True
    return rig, actions, anchor


@traced
def smooth(phase, begin, end):
    """归一化阶段平滑权重，仅复用狙击伸手/回握契约，实际旧机械动作仍由UE快照提供。"""
    ratio = max(0, min(1, (phase - begin) / (end - begin)))
    return ratio * ratio * (3 - 2 * ratio)


@traced
def solve_candidate(rig, coordinate_map, gun_matrix_ue_local, model, variant, phase, profile, old_right_local):
    """在工作台副本求解候选右臂；保留手腕原世界朝向和左臂动作，不拉伸骨或变动模板蒙皮。"""
    # 所有骨先采世界矩阵值副本，后续修改父骨不会污染原来的长度和旋转参考。
    upper = rig.pose.bones["upperarm_r"]
    lower = rig.pose.bones["lowerarm_r"]
    hand = rig.pose.bones["hand_r"]
    a = rig.matrix_world @ upper.matrix
    b = rig.matrix_world @ lower.matrix
    c = rig.matrix_world @ hand.matrix
    shoulder = a.translation.copy()
    previous_elbow = b.translation.copy()
    previous_hand = c.translation.copy()
    upper_length = (previous_elbow - shoulder).length
    lower_length = (previous_hand - previous_elbow).length
    new_grip = gun_matrix_ue_local @ Vector(profile["right_wrist_weapon_cm"])
    old_grip = gun_matrix_ue_local @ Vector(old_right_local)
    bolt_alpha = smooth(phase, .59, .67) * (1 - smooth(phase, .88, .97)) if model == "Sniper" and variant == "Empty" else 0
    target = previous_hand + (new_grip - old_grip) * (1 - bolt_alpha) if bolt_alpha else new_grip
    if bolt_alpha:
        # 右手拉栓过渡在枪右侧绕行；中间加弧形外移，完全握栓时回到真实旧目标。
        target += gun_matrix_ue_local.to_3x3() @ Vector((0, profile["sniper_bolt_reach_clearance_cm"] * math.sin(math.pi * bolt_alpha), 0))
    pole = shoulder + coordinate_map.to_3x3() @ Vector(profile["right_elbow_from_shoulder_cm"])
    direction = target - shoulder
    distance = direction.length
    if distance >= upper_length + lower_length or distance <= abs(upper_length - lower_length):
        raise RuntimeError(f"Candidate wrist unreachable {model}/{variant} phase={phase}: {distance}")
    direction.normalize()
    bend = pole - shoulder
    bend -= direction * bend.dot(direction)
    bend.normalize()
    along = (upper_length * upper_length + distance * distance - lower_length * lower_length) / (2 * distance)
    height = math.sqrt(max(0, upper_length * upper_length - along * along))
    elbow = shoulder + direction * along + bend * height
    upper_delta = (previous_elbow - shoulder).rotation_difference(elbow - shoulder)
    lower_delta = (previous_hand - previous_elbow).rotation_difference(target - elbow)
    upper_new = upper_delta.to_matrix().to_4x4() @ a
    lower_new = lower_delta.to_matrix().to_4x4() @ b
    upper_new.translation = shoulder
    lower_new.translation = elbow
    hand_new = c.copy()
    hand_new.translation = target
    inverse = rig.matrix_world.inverted()
    for joint, matrix in ((upper, upper_new), (lower, lower_new), (hand, hand_new)):
        joint.matrix = inverse @ matrix
        joint.keyframe_insert(data_path="location")
        joint.keyframe_insert(data_path="rotation_quaternion")
        joint.keyframe_insert(data_path="scale")
        bpy.context.view_layer.update()
    return {"right_wrist_weapon_cm": vector_values(gun_matrix_ue_local.inverted() @ target),
            "right_elbow_weapon_cm": vector_values(gun_matrix_ue_local.inverted() @ elbow),
            "chain_length_cm": upper_length + lower_length, "shoulder_wrist_cm": distance}


@traced
def build_candidate_action(rig, gun, source_action, entry, coordinate_map, profile, old_right_local):
    """复制完整当前UE动作，仅逐帧改右上臂/前臂/腕；保留源动作供工作台直接比较。"""
    action = source_action.copy()
    action.name = f"Candidate_{entry['model']}_{entry['variant']}"
    action.use_fake_user = True
    start, end = (int(round(value)) for value in source_action.frame_range)
    record = {"model": entry["model"], "variant": entry["variant"], "action": action.name, "samples": []}
    for frame in range(start, end + 1):
        phase = (frame - start) / (end - start)
        assign_action(rig, source_action)
        set_frame(frame)
        # 缓存当前骨局部以便切Action后保留所有辅助/手指骨的原姿势；不重算UE蒙皮。
        source_basis = {joint.name: joint.matrix_basis.copy() for joint in rig.pose.bones}
        gun_matrix = gun.matrix_world @ GUN_BLENDER_TO_UE
        assign_action(rig, action)
        for joint in rig.pose.bones:
            joint.matrix_basis = source_basis[joint.name]
        bpy.context.view_layer.update()
        sample = solve_candidate(rig, coordinate_map, gun_matrix, entry["model"], entry["variant"], phase, profile, old_right_local)
        if frame in (start, end) or abs(phase - .32) < .01 or abs(phase - .74) < .01:
            record["samples"].append({"phase": phase, **sample})
    return action, record


@traced
def show_group(groups, model):
    """只显示当前枪集合；四组共享骨架数据但姿势独立，隐藏只是工作台预览组织方式。"""
    for name, group in groups.items():
        group.hide_render = name != model
        group.hide_viewport = name != model
    bpy.context.view_layer.update()


@traced
def render_layout(model, stage, variant, phase, rigs, actions, gun_actions, groups, coordinate_map, entry, output):
    """实际联合场景渲染侧面与第一人称视角；图片来自蒙皮手臂+真实枪几何，不用静态拼图伪造接触。"""
    show_group(groups, model)
    arms, gun = rigs[model]
    assign_action(arms, actions[(model, variant)])
    assign_action(gun, None if variant == "Idle" else gun_actions[(model, variant)])
    if variant == "Idle":
        for joint in gun.pose.bones:
            joint.matrix_basis = Matrix.Identity(4)
    set_frame(action_frame(actions[(model, variant)], phase))
    scene = bpy.context.scene
    weapon_origin = gun.matrix_world.translation
    for view in ("Side", "FirstPerson"):
        camera = bpy.data.objects.get("WorkbenchCamera")
        if camera is None:
            bpy.ops.object.camera_add()
            camera = bpy.context.object
            camera.name = "WorkbenchCamera"
        if view == "Side":
            target = weapon_origin + coordinate_map.to_3x3() @ Vector((-12, 0, -8))
            camera.location = target + coordinate_map.to_3x3() @ Vector((-42, 150, 38))
            camera.rotation_euler = (target - camera.location).to_track_quat("-Z", "Y").to_euler()
            camera.data.type = "ORTHO"
            camera.data.ortho_scale = 142 if model == "Sniper" else 112
        else:
            # 当前运行Mesh1P相对相机(-10,0,-147)；用于画面参考，修复本身不移动镜头。
            camera.location = coordinate_map @ Vector((10, 0, 147))
            forward = coordinate_map.to_3x3() @ Vector((1, 0, 0))
            camera.rotation_euler = forward.to_track_quat("-Z", "Y").to_euler()
            camera.data.type = "PERSP"
            camera.data.lens = 18
            camera.data.sensor_width = 36
        # 第一人称采用Engine/BaseEngine.ini默认10cm近裁剪，侧视保留0.5cm；这仅匹配投影，不改变相交检测。
        camera.data.clip_start = 10 if view == "FirstPerson" else .5
        camera.data.clip_end = 10000
        scene.camera = camera
        scene.render.filepath = str(output / f"{model}_{stage}_{view}.png")
        bpy.ops.render.render(write_still=True)


@traced
def build_workbench():
    """组装四枪共享原手臂骨架/蒙皮的实际联合场景，保存当前UE与候选动作和24张检查图。"""
    manifest = json.loads((CURRENT / "current_ue_workbench_manifest.json").read_text(encoding="utf-8"))
    profile = json.loads(PROFILE.read_text(encoding="utf-8"))
    reference = json.loads((OUT / "Reference/arms_reference.json").read_text(encoding="utf-8"))
    right_reference = next(bone["component"]["translation"] for bone in reference["bones"] if bone["name"] == "hand_r")
    old_right_local = vector_values(Vector(right_reference) - Vector(reference["grip"]["translation"]))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = .01
    scene.render.fps = 60
    scene.render.fps_base = 1
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 1600
    scene.render.resolution_y = 1000
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.world = bpy.data.worlds.new("FPLayoutStudio")
    scene.world.color = (.035, .052, .065)
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "BOTH"
    scene.display.shading.background_type = "WORLD"
    scene.view_settings.view_transform = "Standard"
    bpy.context.preferences.filepaths.save_version = 0
    # groups/rigs/actions均只拥有工作台副本；shared_data为唯一原骨架数据，四个姿势实例共享其层级。
    groups = {}
    rigs = {}
    sources = {}
    candidates = {}
    gun_actions = {}
    mappings = {}
    shared_data = None
    shared_mesh = None
    report = {"status": "editable geometric candidate, requires UE skin/weapon intersection regression",
              "source_manifest": str((CURRENT / "current_ue_workbench_manifest.json").relative_to(ROOT)),
              "profile": profile, "models": [], "candidate_actions": []}
    for model in MODELS:
        group = bpy.data.collections.new(f"FP_{model}")
        scene.collection.children.link(group)
        groups[model] = group
        entries = {entry["variant"]: entry for entry in manifest["animations"] if entry["model"] == model}
        arms, meshes, action = import_arms_action(entries["Idle"], group)
        if shared_data is None:
            shared_data = arms.data
            shared_data.name = "UE_Original_Arms_Skeleton"
            shared_mesh = meshes[0].data
        else:
            arms.data = shared_data
            meshes[0].data = shared_mesh
        sources[(model, "Idle")] = action
        mapping, error = calibrate_coordinates(arms, action, entries["Idle"])
        mappings[model] = mapping
        gun, mechanical, anchor = append_gun(model, arms, mapping, entries["Idle"], group)
        rigs[model] = (arms, gun)
        gun_actions[(model, "Tactical")] = mechanical["Tactical"]
        gun_actions[(model, "Empty")] = mechanical["Empty"]
        for variant in ("Tactical", "Empty"):
            unused_rig, unused_meshes, imported = import_arms_action(entries[variant], group, arms)
            sources[(model, variant)] = imported
        for variant in ("Idle", "Tactical", "Empty"):
            assign_action(gun, None if variant == "Idle" else mechanical[variant])
            candidate, action_report = build_candidate_action(arms, gun, sources[(model, variant)], entries[variant], mapping, profile, old_right_local)
            candidates[(model, variant)] = candidate
            report["candidate_actions"].append(action_report)
        report["models"].append({"model": model, "collection": group.name, "arms": arms.name,
                                 "gun": gun.name, "ue_to_blender": [list(row) for row in mapping],
                                 "alignment_max_error_cm": error, "shared_skeleton": shared_data.name,
                                 "bones": len(shared_data.bones), "arm_vertices": len(meshes[0].data.vertices)})
        group.hide_render = True
        group.hide_viewport = True
    output = OUT / "WorkbenchPreviews"
    output.mkdir(exist_ok=True)
    for model in MODELS:
        for stage, variant, phase in (("Grip", "Idle", 0), ("Magazine", "Tactical", .32), ("Mechanism", "Empty", .74)):
            render_layout(model, stage, variant, phase, rigs, candidates, gun_actions, groups, mappings[model], None, output)
    # 保存打开时为步枪候选持握；源/候选Actions均保留且有fake_user，用户可以在Action Editor切换比较。
    show_group(groups, "Rifle")
    assign_action(rigs["Rifle"][0], candidates[("Rifle", "Idle")])
    assign_action(rigs["Rifle"][1], None)
    for joint in rigs["Rifle"][1].pose.bones:
        joint.matrix_basis = Matrix.Identity(4)
    set_frame(1)
    scene.frame_start = 1
    scene.frame_end = 151
    scene["README"] = "FP collections: show one. Arms Actions CurrentUE_* preserve exported poses; Candidate_* use fp_pose_profiles.json. Pair Tactical/Empty with Gun A_Breach_* action. Shared original UE bones/skin; no Template edits."
    scene["candidate_profile"] = json.dumps(profile)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "Breach_FP_Reload_Workbench.blend"))
    (OUT / "workbench_manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print("FP_RELOAD_WORKBENCH_SUCCESS models=4 source_actions=12 candidate_actions=12 previews=24", flush=True)


@traced
def main():
    """先提取真实碰撞参考；CurrentUE导出完整后才组装动作场景，缺失资源明确停止而不覆盖假场景。"""
    extract_stock_proxies()
    extract_forearm_radii()
    print("FP_WORKBENCH_GEOMETRY_REFERENCES_SUCCESS", flush=True)
    if "--extract-only" not in sys.argv:
        build_workbench()


if __name__ == "__main__":
    main()
