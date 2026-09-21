"""只读检查右上臂、前臂与手腕实体代理和真实枪托零件体积，覆盖长枪待机及换弹60Hz。

以普通独立Editor-Cmd运行，无需启用或重编GeometryScripting。默认读取真实UE导出蒙皮厚度、
当前Sequence/武器CDO和Blender实物枪托OBB，输出Saved/Logs/FPWeaponIntersections.json。
仅已存在GeometryScript反射API时直接读取LOD0并额外评价LBS，不把不可用测量写成零穿插。
有厚度胶囊是保守体积检查，不是骨轴射线；失败不因观感猜测而自动放宽。另计算实际线性蒙皮
顶点落入枪托OBB的佐证。未做完整三角面对三角面检测，报告会明确这一边界。

维护：修改手臂网格/蒙皮后先运行build_fp_reload_workbench.py更新真实权重半径；修改枪托后
同步更新stock_collision_proxy.json。代理包含肩附近和twist主权重点，不删掉会碰撞的部位。
上臂范围由旧Sniper Empty .68真实upperarm_twist_02_r穿托案例补齐；历史Before报告只测前臂/腕，
不得拿其passed证明旧全右臂无穿模。上臂胶囊为保守包络，假阳性需独立实际蒙皮BVH复核，
保留失败与原始半径/最小间距，不通过缩半径、改阈值或跳帧使结果变绿。
真实蒙皮复核见Art/Player/Animations/workbench_skin_diagnostic.json；必须核对它与当前动画来源
一致后人工判读。该外部结果不会覆盖本脚本的proxy失败，避免过期复核绕过新动画问题。
"""
import json
import hashlib
import math
from pathlib import Path

import unreal

# 只读输入均限定到当前工程资产及制作清单；唯一写入位于Saved，不保存Content/CDO。
ROOT = Path(__file__).resolve().parents[2]
DEST = "/Game/Weapons/Animations"
MANIFEST_PATH = ROOT / "Art/Weapons/Animations/weapon_animation_manifest.json"
STOCK_PATH = ROOT / "Art/Player/Animations/stock_collision_proxy.json"
RADIUS_PATH = ROOT / "Art/Player/Animations/forearm_skin_radius.json"
REPORT_PATH = ROOT / "Saved/Logs/FPWeaponIntersections.json"
# 与实际动作烘焙频率一致，额外取制作阶段边界；不能靠降低采样率略过短暂穿插。
FPS = 60
# 0.02cm仅用于浮点/模型压缩噪声，不是可接受的美术穿模容差，报告仍保留更小正交叠。
NUMERICAL_EPSILON_CM = .02
# 四段前臂半径取各自真实LOD0顶点最大径向距离，保留袖口的形状差异，避免全臂一根假细线。
FOREARM_BINS = 4
# 上臂同样分四段；肩的厚袖和肘侧twist分别使用实际顶点包络，不沿用前臂的更细半径。
UPPERARM_BINS = 4
# 区域顶点按下臂/扭转骨权重和归类；0.25涵盖肘腕蒙皮过渡，权重较低的指尖不扩大整条前臂。
REGION_WEIGHT_THRESHOLD = .25


def required(path, asset_type):
    """path为既有只读UE包，asset_type为必需类型；缺数据不能以空代理视为通过。"""
    unreal.log(f"[CALL] FPWeaponIntersections.required {path}")
    asset = unreal.load_asset(path)  # 同步借用，由Editor资产系统持有。
    if not isinstance(asset, asset_type):
        raise RuntimeError(f"Missing/wrong asset: {path}")
    return asset


def vector_data(value):
    """value为UE厘米向量，返回有限JSON值副本，不输出UObject地址。"""
    unreal.log("[CALL] FPWeaponIntersections.vector_data")
    result = [float(value.x), float(value.y), float(value.z)]
    if not all(math.isfinite(component) for component in result):
        raise RuntimeError(f"Nonfinite position: {result}")
    return result


def pose_at(sequence, phase, options):
    """sequence/options为当前同步借用资产；phase为0..1，采样真实保存的完整骨姿势。"""
    unreal.log(f"[CALL] FPWeaponIntersections.pose_at {sequence.get_name()} {phase:.6f}")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, phase * sequence.get_play_length(), options)


def bone(pose, name):
    """返回此帧组件空间骨Transform；UE AnimPose WORLD在这里不是游戏世界空间。"""
    unreal.log(f"[CALL] FPWeaponIntersections.bone {name}")
    return unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD)


def extract_arm_geometry(arms):
    """读取实际UE LOD0与骨权重，估计分段胶囊半径并保存LBS所需骨局部顶点；不修改原资产。"""
    unreal.log("[CALL] FPWeaponIntersections.extract_arm_geometry")
    unreal.load_module("GeometryScriptingCore")
    # dynamic只是临时内存副本，不创建包、不调用CopyMeshToAsset，也不重新导入手臂。
    dynamic = unreal.DynamicMesh()
    copied, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        arms, dynamic, unreal.GeometryScriptCopyMeshFromAssetOptions(), unreal.GeometryScriptMeshReadLOD())
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS or copied != dynamic:
        raise RuntimeError(f"Cannot read actual skeletal LOD0: {outcome}")
    unused_mesh, bone_infos = unreal.GeometryScript_BoneWeights.get_all_bones_info(dynamic)
    if not bone_infos:
        raise RuntimeError("LOD0 has no skeletal reference transforms; cannot derive measured arm radius")
    # by_index/by_name来自当前UE MeshDescription，不假定FBX骨排序或使用过时参考JSON。
    by_index = {int(info.index): info for info in bone_infos}
    by_name = {str(info.name): info for info in bone_infos}
    if any(name not in by_name for name in ("upperarm_r", "lowerarm_r", "hand_r")):
        raise RuntimeError("Actual LOD0 is missing right upperarm/forearm/wrist bones")
    shoulder = by_name["upperarm_r"].world_transform.translation  # 参考肩到肘用于真实上臂蒙皮包络。
    elbow = by_name["lowerarm_r"].world_transform.translation
    wrist = by_name["hand_r"].world_transform.translation
    axis = wrist - elbow  # 参考姿势骨轴，厘米；下面使用投影同时测量真实厚度。
    length_squared = axis.length() ** 2
    if length_squared < 1:
        raise RuntimeError("Degenerate reference forearm")
    forearm_indices = {index for index, info in by_index.items()
                       if str(info.name).endswith("_r") and str(info.name).startswith("lowerarm")}
    upperarm_indices = {index for index, info in by_index.items()
                        if str(info.name).endswith("_r") and str(info.name).startswith("upperarm")}
    upperarm_axis = elbow - shoulder  # 与其真实肩肘骨长一致，厘米。
    upperarm_length_squared = upperarm_axis.length() ** 2
    if upperarm_length_squared < 1:
        raise RuntimeError("Degenerate reference upperarm")
    hand_index = int(by_name["hand_r"].index)
    # bins记录真实顶点分布而不是人为常量；stored用于逐帧线性蒙皮额外佐证。
    bins = [[] for unused in range(FOREARM_BINS)]
    upperarm_bins = [[] for unused in range(UPPERARM_BINS)]  # 包含全部主轴端点以外肩袖点的真实距离。
    wrist_radial = []
    stored = []
    # UE Python不同生成版本对IDs缩写可能暴露i_ds或ids；两者都只调用正式只读反射函数。
    count_function = getattr(unreal.GeometryScript_MeshQueries, "get_num_vertex_i_ds", None)
    if count_function is None:
        count_function = getattr(unreal.GeometryScript_MeshQueries, "get_num_vertex_ids")
    vertex_ids = count_function(dynamic)
    for vertex_id in range(vertex_ids):  # 每个有效LOD0顶点只读一次，源网格可含ID空洞。
        position, valid = unreal.GeometryScript_MeshQueries.get_vertex_position(dynamic, vertex_id)
        if not valid:
            continue
        unused_mesh, weights, valid_weights = unreal.GeometryScript_BoneWeights.get_vertex_bone_weights(dynamic, vertex_id)
        if not valid_weights:
            raise RuntimeError(f"Missing LOD0 skin weights at vertex {vertex_id}")
        # forearm_weight决定袖子区域，wrist_weight用于包含手腕过渡；不是删除与枪托接触的高风险顶点。
        forearm_weight = sum(float(weight.weight) for weight in weights if int(weight.bone_index) in forearm_indices)
        wrist_weight = sum(float(weight.weight) for weight in weights if int(weight.bone_index) == hand_index)
        upperarm_weight = sum(float(weight.weight) for weight in weights if int(weight.bone_index) in upperarm_indices)
        relative = position - elbow
        projection = float((relative.x * axis.x + relative.y * axis.y + relative.z * axis.z) / length_squared)
        axis_point = elbow + axis * projection
        radial = (position - axis_point).length()
        is_forearm = forearm_weight >= REGION_WEIGHT_THRESHOLD
        is_upperarm = upperarm_weight >= REGION_WEIGHT_THRESHOLD
        is_wrist = wrist_weight >= REGION_WEIGHT_THRESHOLD and .85 <= projection <= 1.15
        if not is_forearm and not is_wrist and not is_upperarm:
            continue
        if is_upperarm:
            upperarm_relative = position - shoulder  # 当前点投影到肩肘参考轴，不拿前臂轴替代。
            upperarm_projection = float((upperarm_relative.x * upperarm_axis.x + upperarm_relative.y * upperarm_axis.y
                                         + upperarm_relative.z * upperarm_axis.z) / upperarm_length_squared)
            upperarm_index = min(UPPERARM_BINS - 1, max(0, int(upperarm_projection * UPPERARM_BINS)))
            upperarm_clamped = min((upperarm_index + 1) / UPPERARM_BINS, max(upperarm_index / UPPERARM_BINS, upperarm_projection))
            upperarm_bins[upperarm_index].append(float((position - (shoulder + upperarm_axis * upperarm_clamped)).length()))
        if is_forearm:
            index = min(FOREARM_BINS - 1, max(0, int(projection * FOREARM_BINS)))
            bins[index].append(float(radial))
        if .85 <= projection <= 1.15:
            wrist_radial.append(float(radial))
        influences = []  # 按真实全部权重LBS，不把入选顶点粗暴刚性绑到lowerarm。
        weight_sum = sum(float(weight.weight) for weight in weights)
        if abs(weight_sum - 1.0) > .02:
            raise RuntimeError(f"Unexpected skin weight sum: vertex={vertex_id} sum={weight_sum}")
        for weight in weights:
            info = by_index.get(int(weight.bone_index))  # 每一影响骨必须有当前ReferencePose。
            if info is None:
                raise RuntimeError(f"Missing reference bone {weight.bone_index}")
            influences.append((str(info.name), float(weight.weight) / weight_sum,
                               info.world_transform.inverse_transform_location(position)))
        stored.append({"vertex_id": vertex_id, "reference": position, "influences": influences,
                       "reference_projection": projection, "region": "upperarm" if is_upperarm else "forearm" if is_forearm else "wrist"})
    if any(not values for values in bins + upperarm_bins) or not wrist_radial or len(stored) < 20:
        raise RuntimeError("Insufficient measured upperarm/forearm/wrist vertices; refusing guessed radius")
    # 最大半径覆盖该段所有入选真实顶点；不使用百分位裁剪或根据失败结果缩小胶囊。
    radii = [max(values) for values in bins]
    wrist_radius = max(wrist_radial)
    metadata = {"mesh": arms.get_path_name(), "lod": 0, "source_vertex_ids": int(vertex_ids),
                "selected_vertices": len(stored), "reference_elbow_cm": vector_data(elbow),
                "reference_wrist_cm": vector_data(wrist), "reference_forearm_length_cm": math.sqrt(length_squared),
                "bone_region": [str(by_index[index].name) for index in sorted(forearm_indices)],
                "weight_threshold": REGION_WEIGHT_THRESHOLD, "bin_vertex_counts": [len(values) for values in bins],
                "capsule_radii_cm": radii, "wrist_sphere_radius_cm": wrist_radius,
                "upperarm": {"reference_length_cm": math.sqrt(upperarm_length_squared),
                             "bin_vertex_counts": [len(values) for values in upperarm_bins],
                             "capsule_radii_cm": [max(values) for values in upperarm_bins]},
                "radius_method": "maximum perpendicular distance of actual LOD0 weighted region vertices; four axial bins; no percentile trimming"}
    unreal.log(f"FP_ARM_VOLUME_MEASURED vertices={len(stored)} radii={radii} wrist={wrist_radius:.4f}")
    return stored, radii, wrist_radius, metadata


def read_exported_skin_radii(arms):
    """当前源码引擎未编译GeometryScript时读取真实UE导出蒙皮实测；核对当前骨长并明确不提供LBS顶点佐证。"""
    unreal.log("[CALL] FPWeaponIntersections.read_exported_skin_radii")
    measured = json.loads(RADIUS_PATH.read_text(encoding="utf-8"))  # Blender从UE源FBX逐顶点权重/位置测得，不是人工半径表。
    source = (ROOT / measured["source"]).resolve()
    if not source.is_file() or measured.get("units") != "centimetres" or len(measured.get("sample_detail", [])) < 20:
        raise RuntimeError("Missing measured source skin data; cannot fall back to guessed capsule radii")
    idle = required("/Game/FirstPersonArms/Animations/FP_Rifle_Idle", unreal.AnimSequence)
    pose = pose_at(idle, 0, unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=arms))
    actual_length = (bone(pose, "hand_r").translation - bone(pose, "lowerarm_r").translation).length()
    source_length = float(measured["axis_length_cm"])
    if abs(actual_length - source_length) > .05:
        raise RuntimeError(f"Exported skin bone length is stale: source={source_length} current={actual_length}")
    bins = [[] for unused in range(FOREARM_BINS)]  # 所有源顶点都参与，不丢弃最大值或只选视觉安全的区段。
    wrist_samples = []
    for sample in measured["sample_detail"]:
        alpha = float(sample["alpha"])
        radial = float(sample["radius_cm"])
        if not math.isfinite(alpha) or not math.isfinite(radial) or radial < 0:
            raise RuntimeError("Invalid measured skin vertex")
        index = min(FOREARM_BINS - 1, max(0, int(alpha * FOREARM_BINS)))
        # 骨端之外的真实袖口顶点到胶囊端点仍有纵向距离，半径合并该距离，不能仅取横截面而漏覆盖。
        clamped = min((index + 1) / FOREARM_BINS, max(index / FOREARM_BINS, alpha))
        distance_to_segment = math.hypot(radial, (alpha - clamped) * source_length)
        bins[index].append(distance_to_segment)
        if alpha >= .85:
            wrist_samples.append(math.hypot(radial, (alpha - 1.0) * source_length))
    if any(not values for values in bins) or not wrist_samples:
        raise RuntimeError("Measured source does not cover every forearm segment and wrist")
    radii = [max(values) for values in bins]
    wrist_radius = max(wrist_samples)
    # 上臂新必需输入来自同一真实FBX；若仍使用旧半径文件就明确失败，不能默默缩回前臂覆盖范围。
    upperarm = measured.get("upperarm")
    if not isinstance(upperarm, dict) or len(upperarm.get("sample_detail", [])) < 20:
        raise RuntimeError("Missing actual upperarm skin samples; regenerate forearm_skin_radius.json with upperarm coverage")
    upperarm_source_length = float(upperarm["axis_length_cm"])
    upperarm_actual_length = (bone(pose, "lowerarm_r").translation - bone(pose, "upperarm_r").translation).length()
    if abs(upperarm_actual_length - upperarm_source_length) > .05:
        raise RuntimeError(f"Exported upperarm length is stale: source={upperarm_source_length} current={upperarm_actual_length}")
    upperarm_bins = [[] for unused in range(UPPERARM_BINS)]  # 不排除肩、twist或靠肘的真实主权重顶点。
    for sample in upperarm["sample_detail"]:
        alpha = float(sample["alpha"])  # 肩为0、肘为1，真实袖子可伸出区间，不能裁掉其体积。
        radial = float(sample["radius_cm"])
        if not math.isfinite(alpha) or not math.isfinite(radial) or radial < 0:
            raise RuntimeError("Invalid measured upperarm skin vertex")
        index = min(UPPERARM_BINS - 1, max(0, int(alpha * UPPERARM_BINS)))
        clamped = min((index + 1) / UPPERARM_BINS, max(index / UPPERARM_BINS, alpha))
        upperarm_bins[index].append(math.hypot(radial, (alpha - clamped) * upperarm_source_length))
    if any(not values for values in upperarm_bins):
        raise RuntimeError("Measured source does not cover every upperarm segment")
    upperarm_radii = [max(values) for values in upperarm_bins]
    metadata = {"mesh": arms.get_path_name(), "lod": 0, "source": str(source),
                "source_fbx_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "radius_manifest_sha256": hashlib.sha256(RADIUS_PATH.read_bytes()).hexdigest(),
                "current_forearm_length_cm": actual_length, "source_forearm_length_cm": source_length,
                "selected_vertices": len(measured["sample_detail"]), "bin_vertex_counts": [len(values) for values in bins],
                "capsule_radii_cm": radii, "wrist_sphere_radius_cm": wrist_radius,
                "radius_method": "maximum actual UE-exported LOD0 skin vertex distance to assigned reference segment; all supplied weighted vertices included, no percentile trimming",
                "upperarm": {"selected_vertices": len(upperarm["sample_detail"]),
                             "source_length_cm": upperarm_source_length, "current_length_cm": upperarm_actual_length,
                             "bin_vertex_counts": [len(values) for values in upperarm_bins], "capsule_radii_cm": upperarm_radii},
                "actual_skinning_evidence_available": False,
                "limitation": "GeometryScriptingCore binary unavailable; pose is current UE Sequence but thickness is measured from UE-exported source FBX; full LBS intersections not evaluated"}
    unreal.log(f"FP_ARM_EXPORTED_VOLUME_MEASURED forearmVertices={len(measured['sample_detail'])} forearmRadii={radii} wrist={wrist_radius:.4f} upperarmVertices={len(upperarm['sample_detail'])} upperarmRadii={upperarm_radii}")
    return [], radii, wrist_radius, metadata


def segment_box_distance(first, second, extents):
    """局部线段与以原点为中心真实OBB的有符号最近距离；解析分段二次外距+内部面深度，无粗骨轴采样。"""
    unreal.log("[CALL] FPWeaponIntersections.segment_box_distance")
    # 先求线段到盒外部的精确最短距离，分界是各轴穿过六个真实包围面时刻。
    start = (float(first.x), float(first.y), float(first.z))
    finish = (float(second.x), float(second.y), float(second.z))
    delta = tuple(finish[index] - start[index] for index in range(3))
    breaks = {0.0, 1.0}
    for index in range(3):
        if abs(delta[index]) > 1e-12:
            for plane in (-extents[index], extents[index]):
                parameter = (plane - start[index]) / delta[index]
                if 0 < parameter < 1:
                    breaks.add(parameter)
    points = sorted(breaks)
    candidates = list(points)
    for low, high in zip(points, points[1:]):
        middle = (low + high) * .5  # 在此区间盒外活跃轴不变，平方距离是一个确定二次式。
        numerator = 0.0
        denominator = 0.0
        for index in range(3):
            value = start[index] + delta[index] * middle
            if value < -extents[index] or value > extents[index]:
                boundary = -extents[index] if value < -extents[index] else extents[index]
                numerator += delta[index] * (start[index] - boundary)
                denominator += delta[index] * delta[index]
        if denominator > 1e-12:
            candidates.append(min(high, max(low, -numerator / denominator)))
    # 线段进入盒内时求六个面距离下包络的最大值；相交点/区间端点涵盖所有可能极值。
    planes = [(extents[index] - sign * start[index], -sign * delta[index])
              for index in range(3) for sign in (-1.0, 1.0)]
    for index, first_plane in enumerate(planes):
        for second_plane in planes[index + 1:]:
            slope = first_plane[1] - second_plane[1]
            if abs(slope) > 1e-12:
                parameter = (second_plane[0] - first_plane[0]) / slope
                if 0 < parameter < 1:
                    candidates.append(parameter)
    minimum = float("inf")
    best_parameter = 0.0
    for parameter in candidates:
        coordinates = [start[index] + delta[index] * parameter for index in range(3)]
        excess = [abs(coordinates[index]) - extents[index] for index in range(3)]
        signed_distance = math.sqrt(sum(max(value, 0.0) ** 2 for value in excess)) + min(max(excess), 0.0)
        if signed_distance < minimum:
            minimum = signed_distance
            best_parameter = parameter
    return minimum, best_parameter


def prepare_stock(proxy_entry):
    """读取models导出的逐零件OBB，严格校验单位/正尺度；不合并成全枪大盒掩盖空隙。"""
    unreal.log(f"[CALL] FPWeaponIntersections.prepare_stock {proxy_entry.get('model')}")
    prepared = []
    for part in proxy_entry["stock_pieces"]:  # 每个OBB来自真实源对象局部bounds和完整对象变换。
        extents = [float(value) for value in part["half_extents_cm"]]
        if len(extents) != 3 or not all(math.isfinite(value) and value > 0 for value in extents):
            raise RuntimeError(f"Invalid physical stock extents: {part}")
        rotation = part["rotation_quat_xyzw"]
        transform = unreal.Transform()
        transform.translation = unreal.Vector(*part["center_cm"])
        transform.rotation = unreal.Quat(*rotation)
        transform.scale3d = unreal.Vector(1, 1, 1)
        prepared.append({"name": part["name"], "bone": part.get("bone", "body"),
                         "transform": transform, "extents": extents})
    return prepared


def point_to_stock(point, anchor, attach, current_body, reference_body, box):
    """依次把手臂组件点变到枪Actor、当前刚性骨参考空间和OBB局部，所有输入均为本帧值。"""
    unreal.log("[CALL] FPWeaponIntersections.point_to_stock")
    weapon_point = attach.inverse_transform_location(anchor.inverse_transform_location(point))
    reference_point = reference_body.transform_location(current_body.inverse_transform_location(weapon_point))
    return box.inverse_transform_location(reference_point)


def validate_clip(model, kind, sequence, mechanical, config, options, source_vertices, radii, wrist_radius, upperarm_radii, stock):
    """radii/upperarm_radii为实测四段厘米半径，wrist_radius为实测腕球；读取真实姿势，保存代理重叠与可用LBS佐证。"""
    unreal.log(f"[CALL] FPWeaponIntersections.validate_clip {model}/{kind}")
    gun_options = unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=config.mesh)
    reference_gun = pose_at(mechanical, 0.0, gun_options)  # body/stock的源参考空间，随后按各帧当前刚性骨变换。
    reference_bodies = {part["bone"]: bone(reference_gun, part["bone"]) for part in stock}
    frames = max(1, round(sequence.get_play_length() * FPS))
    phases = [index / frames for index in range(frames + 1)]
    result = {"model": model, "variant": kind, "arms": sequence.get_path_name(), "mechanical": mechanical.get_path_name(),
              "sample_hz": FPS, "frames": [], "collision_frames": 0, "maximum_penetration_cm": 0.0,
              "maximum_skinned_vertex_depth_cm": 0.0, "minimum_capsule_clearance_cm": float("inf"),
              "stock_parts": [part["name"] for part in stock]}
    used_bones = sorted({name for vertex in source_vertices for name, unused_weight, unused_local in vertex["influences"]})
    for phase in phases:
        pose = pose_at(sequence, phase, options)
        gun_pose = pose_at(mechanical, 0.0 if kind == "Idle" else phase, gun_options)
        anchor = bone(pose, "ik_hand_gun")  # 稳定枪挂点，与当前武器Config.AttachOffset组成精确相对空间。
        shoulder = bone(pose, "upperarm_r").translation  # 上臂胶囊从真实肩关节到实际肘，不用前臂端点延长猜测。
        elbow = bone(pose, "lowerarm_r").translation
        wrist = bone(pose, "hand_r").translation
        axis = wrist - elbow
        transforms = {name: bone(pose, name) for name in used_bones}
        current_bodies = {name: bone(gun_pose, name) for name in reference_bodies}
        # 只使用当前实际骨姿势线性蒙皮；没有先运行另一套IK来替换失败的真实动画。
        skinned = []
        for vertex in source_vertices:
            position = unreal.Vector()
            for name, weight, local in vertex["influences"]:
                position += transforms[name].transform_location(local) * weight
            skinned.append((vertex["vertex_id"], position))
        capsules = [(f"forearm_{index}", elbow + axis * (index / FOREARM_BINS),
                     elbow + axis * ((index + 1) / FOREARM_BINS), radii[index]) for index in range(FOREARM_BINS)]
        upperarm_axis = elbow - shoulder  # 上臂四段与实际骨轴同时运动，逐帧记录其保守实体包络。
        capsules.extend((f"upperarm_{index}", shoulder + upperarm_axis * (index / UPPERARM_BINS),
                         shoulder + upperarm_axis * ((index + 1) / UPPERARM_BINS), upperarm_radii[index])
                        for index in range(UPPERARM_BINS))
        capsules.append(("wrist", wrist, wrist, wrist_radius))  # 零长胶囊就是手腕球体，仍有实际测得半径。
        # 输出原始同空间端点与最小间距，用于审核意外阴性；不能只记录碰撞后宣告所有未触发帧正确。
        weapon_elbow = config.attach_offset.inverse_transform_location(anchor.inverse_transform_location(elbow))
        weapon_wrist = config.attach_offset.inverse_transform_location(anchor.inverse_transform_location(wrist))
        weapon_shoulder = config.attach_offset.inverse_transform_location(anchor.inverse_transform_location(shoulder))
        frame = {"phase": phase, "seconds": phase * sequence.get_play_length(), "collisions": [],
                 "shoulder_weapon_cm": vector_data(weapon_shoulder),
                 "elbow_weapon_cm": vector_data(weapon_elbow), "wrist_weapon_cm": vector_data(weapon_wrist),
                 "minimum_capsule_clearance_cm": float("inf"), "nearest_pair": None,
                 "maximum_penetration_cm": 0.0, "maximum_skinned_vertex_depth_cm": 0.0}
        for part in stock:
            current_body = current_bodies[part["bone"]]
            reference_body = reference_bodies[part["bone"]]
            part_overlaps = False  # 保守胶囊未触盒时无需做数千顶点复核；不能用复核阴性覆盖胶囊阳性。
            for label, first, second, radius in capsules:
                local_first = point_to_stock(first, anchor, config.attach_offset, current_body, reference_body, part["transform"])
                local_second = point_to_stock(second, anchor, config.attach_offset, current_body, reference_body, part["transform"])
                signed_distance, line_parameter = segment_box_distance(local_first, local_second, part["extents"])
                clearance = signed_distance - radius  # 正值为实体表面间距，负值为保守体积重叠，厘米。
                if clearance < frame["minimum_capsule_clearance_cm"]:
                    frame["minimum_capsule_clearance_cm"] = clearance
                    frame["nearest_pair"] = {"part": part["name"], "region": label, "radius_cm": radius,
                                             "axis_alpha": line_parameter, "box_local_first_cm": vector_data(local_first),
                                             "box_local_second_cm": vector_data(local_second)}
                penetration = max(0.0, radius - signed_distance)  # 球扫掠在真实盒体内的保守深度，cm；不是零半径骨轴。
                frame["maximum_penetration_cm"] = max(frame["maximum_penetration_cm"], penetration)
                if penetration > NUMERICAL_EPSILON_CM:
                    part_overlaps = True
                    frame["collisions"].append({"part": part["name"], "region": label, "radius_cm": radius,
                                               "penetration_cm": penetration, "axis_signed_clearance_cm": signed_distance,
                                               "axis_alpha": line_parameter})
            # 顶点落入OBB为实际蒙皮佐证：不将胶囊未确认的重叠静默丢弃，二者分别报告。
            deepest = 0.0
            deepest_vertex = None
            if part_overlaps and skinned:
                # 把同一完整空间变换缓存成仿射基，再复核全部实际蒙皮顶点；避免几百万Python/UObject调用。
                origin = point_to_stock(unreal.Vector(), anchor, config.attach_offset, current_body, reference_body, part["transform"])
                basis_x = point_to_stock(unreal.Vector(1, 0, 0), anchor, config.attach_offset, current_body, reference_body, part["transform"]) - origin
                basis_y = point_to_stock(unreal.Vector(0, 1, 0), anchor, config.attach_offset, current_body, reference_body, part["transform"]) - origin
                basis_z = point_to_stock(unreal.Vector(0, 0, 1), anchor, config.attach_offset, current_body, reference_body, part["transform"]) - origin
                affine = ((origin.x, basis_x.x, basis_y.x, basis_z.x),
                          (origin.y, basis_x.y, basis_y.y, basis_z.y),
                          (origin.z, basis_x.z, basis_y.z, basis_z.z))
                for vertex_id, position in skinned:
                    px, py, pz = float(position.x), float(position.y), float(position.z)
                    local = [row[0] + row[1] * px + row[2] * py + row[3] * pz for row in affine]
                    depth = min(part["extents"][index] - abs(local[index]) for index in range(3))
                    if depth > deepest:
                        deepest = float(depth)
                        deepest_vertex = vertex_id
            frame["maximum_skinned_vertex_depth_cm"] = max(frame["maximum_skinned_vertex_depth_cm"], deepest)
            if deepest > NUMERICAL_EPSILON_CM:
                frame.setdefault("skinned_vertex_intrusions", []).append({"part": part["name"], "vertex_id": deepest_vertex, "depth_cm": deepest})
        if frame["collisions"] or frame.get("skinned_vertex_intrusions"):
            result["collision_frames"] += 1
        result["maximum_penetration_cm"] = max(result["maximum_penetration_cm"], frame["maximum_penetration_cm"])
        result["minimum_capsule_clearance_cm"] = min(result["minimum_capsule_clearance_cm"], frame["minimum_capsule_clearance_cm"])
        result["maximum_skinned_vertex_depth_cm"] = max(result["maximum_skinned_vertex_depth_cm"], frame["maximum_skinned_vertex_depth_cm"])
        result["frames"].append(frame)
    if not source_vertices:
        # 缺运行期LBS顶点不是零侵入；显式null和能力标志防止下游把未测量读成已通过。
        result["maximum_skinned_vertex_depth_cm"] = None
        for frame in result["frames"]:
            frame["maximum_skinned_vertex_depth_cm"] = None
    result["status"] = "failed" if result["collision_frames"] else "passed"
    result["classification"] = "conservative_proxy_overlap_requires_skin_review" if result["collision_frames"] else "no_proxy_overlap_in_sampled_scope"
    unreal.log(f"FP_STOCK_CLIP {model}/{kind} status={result['status']} collisionFrames={result['collision_frames']} capsuleDepth={result['maximum_penetration_cm']:.4f}cm skinnedVertexDepth={result['maximum_skinned_vertex_depth_cm']}")
    return result


def main():
    """串行Editor只读入口；完整报告写完再失败，既不修改资产，也不以缺少代理/插件跳过验证。"""
    unreal.log("[CALL] FPWeaponIntersections.main")
    report = {"status": "running", "method": "actual UE LOD0 skin-weight-derived thick upperarm/forearm capsules and wrist sphere vs individual Blender stock OBBs; supplementary actual LBS vertex-in-OBB checks",
              "scope": "right upperarm including twist/corrective dominant skin, forearm and wrist; palm/fingers excluded",
              "limitations": ["Conservative per-part OBB/capsule screening; not full skinned triangle-vs-triangle collision",
                              "Palm and fingers are not cleared by this result; capsule intersections require independent actual skinned geometry review",
                              "A zero intersecting-vertex count alone does not clear a capsule collision or exclude a triangle crossing"],
              "fps": FPS, "numerical_epsilon_cm": NUMERICAL_EPSILON_CM, "clips": []}
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
    try:
        unreal.load_module("AnimationBlueprintLibrary")
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        proxies = json.loads(STOCK_PATH.read_text(encoding="utf-8"))
        proxy_models = {entry["model"]: entry for entry in proxies["weapons"]}
        arms = required("/Game/FirstPersonArms/Character/Mesh/SK_Mannequin_Arms", unreal.SkeletalMesh)
        if hasattr(unreal, "GeometryScript_AssetUtils"):
            source_vertices, radii, wrist_radius, report["arm_volume"] = extract_arm_geometry(arms)
        else:
            source_vertices, radii, wrist_radius, report["arm_volume"] = read_exported_skin_radii(arms)
            report["method"] = "actual UE-exported upperarm/forearm weighted skin thickness capsules and wrist sphere vs individual Blender stock OBBs, using current UE Sequence bone poses"
        upperarm_radii = report["arm_volume"]["upperarm"]["capsule_radii_cm"]  # 必须来自真实蒙皮测量；不存在则失败。
        options = unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=arms)
        report["stock_proxy_source"] = str(STOCK_PATH)
        for entry in manifest["weapons"]:
            model = entry["model"]
            if model not in proxy_models:
                raise RuntimeError(f"Missing stock source model: {model}")
            stock = prepare_stock(proxy_models[model])
            if model == "Pistol":
                if stock:
                    raise RuntimeError("Pistol unexpectedly has stock proxies; inspect source selection")
                report["clips"].append({"model": model, "status": "not_applicable", "reason": "actual pistol model has no shoulder stock"})
                continue
            if not stock:
                raise RuntimeError(f"Cannot skip missing stock proxies: {model}")
            bp = required(f"/Game/Weapons/Blueprints/BP_Weapon_{model}", unreal.Blueprint)
            config = unreal.get_default_object(bp.generated_class()).get_editor_property("config")
            if str(config.attach_socket) != "ik_hand_gun" or not config.weapon_anim_layer_class:
                raise RuntimeError(f"Unsupported actual animation attach contract: {model}")
            if (config.attach_offset.scale3d - unreal.Vector(1, 1, 1)).length() > .001:
                raise RuntimeError("Scaled guns require world-space capsule radius conversion")
            settings = unreal.get_default_object(config.weapon_anim_layer_class).get_editor_property("anim_set")
            for kind, sequence, mechanical in (
                    ("Idle", settings.idle_pose, settings.tactical_reload.weapon_sequence),
                    ("Tactical", required(f"{DEST}/Arms/A_FP_{model}_Reload_Tactical", unreal.AnimSequence), settings.tactical_reload.weapon_sequence),
                    ("Empty", required(f"{DEST}/Arms/A_FP_{model}_Reload_Empty", unreal.AnimSequence), settings.empty_reload.weapon_sequence)):
                report["clips"].append(validate_clip(model, kind, sequence, mechanical, config, options,
                                                      source_vertices, radii, wrist_radius, upperarm_radii, stock))
        report["failed_clips"] = sum(clip["status"] == "failed" for clip in report["clips"])
        report["status"] = "failed" if report["failed_clips"] else "passed"
        REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
        if report["failed_clips"]:
            raise RuntimeError(f"FP_WEAPON_INTERSECTION_FAILED clips={report['failed_clips']}; {REPORT_PATH}")
        unreal.log("FP_WEAPON_INTERSECTION_SUCCESS measured_upperarm_forearm_wrist_volume_vs_actual_stock_parts")
    except Exception as error:
        report["status"] = "failed"
        report["exception"] = str(error)
        REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
        raise


if __name__ == "__main__":
    main()
