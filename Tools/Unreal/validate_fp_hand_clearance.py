"""只读离线逐帧检查8套换弹：骨长、可达性、抓匣误差、肩肘腕镜头安全域。

在最新资源导入保存完成后，用独立UnrealEditor-Cmd -run=pythonscript执行本文件。
只读取Content/CDO并写Saved/Logs/FPHandClearance.json，不生成Actor、不修改蓝图或游戏状态。
本检查使用骨链中心线，是发现明显错误的几何筛查；不替代蒙皮三角面/手指/枪身的真实RHI观感检查。
右腕作者契约来自fp_pose_profiles.json：right_wrist_weapon_cm为锚点骨空间厘米位置；
sniper_bolt_reach_clearance_cm为狙击空仓过渡向枪右侧绕行的非负厘米幅度，端点为零。
模板仅提供真实骨长基线，不再从旧GripPoint推导当前枪械右腕目标；所有筛查阈值独立于姿势配置。
"""
import json
import math
from pathlib import Path

import unreal

# 稳定资源目录与生成器一致；只读这些资产，输出仅属于Saved诊断目录。
ROOT = Path(__file__).resolve().parents[2]
DEST = "/Game/Weapons/Animations"
REPORT_PATH = ROOT / "Saved/Logs/FPHandClearance.json"
MANIFEST_PATH = ROOT / "Art/Weapons/Animations/weapon_animation_manifest.json"
# 与当前离线烘焙入口共用的作者姿势契约；只读配置，不用模板旧枪握点猜测新枪右腕。
POSE_PROFILES_PATH = ROOT / "Art/Player/Animations/fp_pose_profiles.json"
# 左手配置区分掌匣接触、转腕和张合轨迹；不能再将旧magazine_grip中心偏移当成新腕点。
LEFT_CONTACTS_PATH = ROOT / "Art/Player/Animations/left_hand_contacts.json"
LEFT_TRACKS_PATH = ROOT / "Art/Player/Animations/left_hand_contact_tracks.json"
# 源片段60Hz烘焙；额外采样真实阶段边界，防止关键时刻刚好落在两帧之间。
FPS = 60
# 四分之一厘米允许压缩/浮点误差，约为27cm单骨长度的0.9%；再大就不是合理导出误差。
BONE_LENGTH_TOLERANCE_CM = .25
# 允许目标越界0.5cm和稳定抓握1cm残差；真实腕应几乎精确到位，超过即说明IK夹紧/目标错位。
REACH_TOLERANCE_CM = .5
CONTACT_TOLERANCE_CM = 1.0
# 安全域是相机前方中央圆柱，+X深度、YZ径向。核心半径5.5cm筛查会明显遮挡准星的骨轴。
# 从4cm开始排除正常位于镜头后侧的肩根；22cm以内的中央骨轴会产生较大视角遮挡。
CORE_DEPTH_CM = (4.0, 22.0)
CORE_RADIUS_CM = 5.5
# 外圈只给警告，允许正常第一人称手臂从画面底部进入，不把保守中心线代理说成真实网格碰撞。
WARNING_DEPTH_CM = (4.0, 28.0)
WARNING_RADIUS_CM = 9.0


def required(path, asset_type):
    """path为UE包路径，asset_type约束真实类型；缺失必须失败，不拿空姿势继续宣告通过。"""
    unreal.log(f"[CALL] FPHandClearance.required {path}")
    asset = unreal.load_asset(path)  # 当前同步借用的只读UObject，由Editor资产系统保持有效。
    if not isinstance(asset, asset_type):
        raise RuntimeError(f"Missing/wrong asset: {path}")
    return asset


def vector_data(value):
    """value为当前采样Vector；只把有限厘米数值写JSON，不保存跨帧UObject引用。"""
    unreal.log("[CALL] FPHandClearance.vector_data")
    result = [float(value.x), float(value.y), float(value.z)]
    if not all(math.isfinite(component) for component in result):
        raise RuntimeError(f"Nonfinite position: {result}")
    return result


def ease(phase, begin, end):
    """phase/begin/end为0..1阶段，返回设计接触权重；只评价目标意图，不修改实际动画。"""
    unreal.log("[CALL] FPHandClearance.ease")
    ratio = min(1.0, max(0.0, (phase - begin) / max(end - begin, .001)))
    return ratio * ratio * (3 - 2 * ratio)


def lerp(first, second, alpha):
    """first/second是组件空间厘米位置，alpha是0..1设计混合权重；返回独立Vector值。"""
    unreal.log("[CALL] FPHandClearance.lerp")
    return first + (second - first) * alpha


def bone_transform(pose, name):
    """pose为此帧FAnimPose值、name为必需骨；World在此API中是网格组件空间，不是游戏世界坐标。"""
    unreal.log(f"[CALL] FPHandClearance.bone_transform {name}")
    return unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD)


def pose_at(sequence, phase, options):
    """sequence/options借用到同步评估结束，phase范围0..1；完整离线采样真实保存的Sequence。"""
    unreal.log(f"[CALL] FPHandClearance.pose_at {sequence.get_name()} phase={phase:.6f}")
    return unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, sequence.get_play_length() * phase, options)


def mesh_camera_transform():
    """读取原生DemoCharacter CDO第一人称网格相对相机变换；不生成角色，不猜测运行Mesh位移。"""
    unreal.log("[CALL] FPHandClearance.mesh_camera_transform")
    character_class = unreal.load_class(None, "/Script/FPSDemo.DemoCharacter")  # 项目实际原生角色类。
    if not character_class:
        raise RuntimeError("DemoCharacter class unavailable")
    character = unreal.get_default_object(character_class)  # 只读CDO；不写变量、不注册/移动组件。
    components = character.get_components_by_class(unreal.SkeletalMeshComponent)
    meshes = [component for component in components if component.get_name() == "CharacterMesh1P"]
    if len(meshes) != 1:
        raise RuntimeError(f"Expected one CharacterMesh1P default subobject, found {len(meshes)}")
    mesh = meshes[0]
    parent = mesh.get_attach_parent()  # 第一人称网格必须直接附着相机；其他层级不能简化成相对位置。
    if not isinstance(parent, unreal.CameraComponent):
        raise RuntimeError("Mesh1P is not directly attached to camera")
    transform = mesh.get_relative_transform()  # 完整相对Transform包括未来旋转/比例，避免只加固定偏移。
    if (transform.scale3d - unreal.Vector(1, 1, 1)).length() > .001:
        raise RuntimeError("Scaled Mesh1P needs separate world-space length tolerances")
    return transform, {"class": character_class.get_path_name(), "mesh": mesh.get_name(),
                       "parent": parent.get_name(), "translation_cm": vector_data(transform.translation),
                       "rotation_quat_xyzw": [transform.rotation.x, transform.rotation.y, transform.rotation.z, transform.rotation.w]}


def segment_clearance(first, second, depth_range):
    """first/second为相机空间骨轴端点，depth_range为圆柱前后X；返回截取线段最小YZ半径或None。"""
    unreal.log("[CALL] FPHandClearance.segment_clearance")
    # delta与区间参数只用于解析求最近点，不依赖粗采样，细长骨轴也不会漏穿安全域。
    delta = second - first
    low = 0.0
    high = 1.0
    if abs(delta.x) < 1e-8:
        if not depth_range[0] <= first.x <= depth_range[1]:
            return None
    else:
        first_limit = (depth_range[0] - first.x) / delta.x
        second_limit = (depth_range[1] - first.x) / delta.x
        low = max(low, min(first_limit, second_limit))
        high = min(high, max(first_limit, second_limit))
        if low > high:
            return None
    radial_delta_squared = delta.y * delta.y + delta.z * delta.z
    closest = low if radial_delta_squared < 1e-8 else min(high, max(low, -(first.y * delta.y + first.z * delta.z) / radial_delta_squared))
    point = first + delta * closest  # 此点在合法深度范围且到相机主视轴最近，厘米。
    return {"radius_cm": math.hypot(point.y, point.z), "camera_point_cm": vector_data(point), "segment_alpha": closest}


def chain_data(pose, side, camera_transform):
    """读取side=l/r肩肘腕及锁骨，返回组件位置与相机记录；不运行IK、蒙皮或游戏更新。"""
    unreal.log(f"[CALL] FPHandClearance.chain_data {side}")
    # component_points保留UE向量便于距离运算；记录另转成JSON纯数值。
    component_points = {"shoulder": bone_transform(pose, f"upperarm_{side}").translation,
                        "elbow": bone_transform(pose, f"lowerarm_{side}").translation,
                        "wrist": bone_transform(pose, f"hand_{side}").translation,
                        "clavicle": bone_transform(pose, f"clavicle_{side}").translation}
    camera_points = {name: camera_transform.transform_location(point) for name, point in component_points.items()}
    upper_length = (component_points["elbow"] - component_points["shoulder"]).length()
    lower_length = (component_points["wrist"] - component_points["elbow"]).length()
    record = {"camera_cm": {name: vector_data(point) for name, point in camera_points.items()},
              "upper_length_cm": upper_length, "lower_length_cm": lower_length,
              "shoulder_to_wrist_cm": (component_points["wrist"] - component_points["shoulder"]).length()}
    return component_points, camera_points, record


def intended_targets(model, clip, phase, anchor, gun_pose, mag_offset, bolt_offset, support, magazine_grip, bolt_grip, right_grip, bolt_reach_clearance, left_samples):
    """用实际锚点/机械骨重建目标；right_grip为配置骨空间腕点，bolt_reach_clearance为非负厘米绕行幅度，不修改真实Sequence。"""
    unreal.log(f"[CALL] FPHandClearance.intended_targets {model} {phase:.6f}")
    # stages为JSON声音/机械共用阶段；直接比较全接触区手腕与真实弹匣，不比较作者算出的同一个结果。
    stages = clip["stages"]
    magazine = bone_transform(gun_pose, "magazine")
    magazine_target = anchor.transform_location(magazine.transform_location(mag_offset))
    # left_samples为当前片段的60Hz枪局部作者轨迹；只读线性插值用于发现压缩/烘焙/IK夹紧造成的偏离。
    sample_frame = phase * (len(left_samples)-1)
    first_index = min(math.floor(sample_frame),len(left_samples)-1)
    second_index = min(first_index+1,len(left_samples)-1)
    left_target = anchor.transform_location(lerp(unreal.Vector(*left_samples[first_index]["wrist_cm"]),unreal.Vector(*left_samples[second_index]["wrist_cm"]),sample_frame-first_index))
    # 只在明确握紧后的区间检测腕与真实机械弹匣的相对误差；张手靠近阶段不伪装成已抓住。
    grip_alpha = 1.0 if clip["stages"]["detach"]-.03 <= phase <= clip["stages"]["seat"] else 0.0
    right_target = anchor.transform_location(right_grip)  # 对应作者Anchor.TransformPosition(RightGrip)，禁止沿用模板GripPoint的旧相对腕点。
    handle_alpha = 0.0
    bolt_alpha = 0.0
    if clip["variant"] == "Empty" and model == "Sniper":
        # 狙击右手独立跟随真实bolt，左手回护木；与稳定锚点无循环读取。
        bolt_alpha = ease(phase, .59, .67) * (1 - ease(phase, .88, .97))
        bolt = bone_transform(gun_pose, "bolt")
        right_target = lerp(right_target, anchor.transform_location(bolt.transform_location(bolt_offset)), bolt_alpha)
        clearance_offset = unreal.Vector(0, math.sin(math.pi * bolt_alpha) * bolt_reach_clearance, 0)  # 枪局部+Y绕行；权重0/1均回到实际握点，单位厘米。
        right_target += anchor.transform_direction(clearance_offset)  # TransformDirection对应TransformVectorNoScale，只旋转偏移，不叠加位置或骨缩放。
    elif clip["variant"] == "Empty":
        # 其他枪操作手柄的作者阶段；在过渡区只检测可达性，不误判为必须保持弹匣贴合。
        handle_alpha = ease(phase, .60, .69) * (1 - ease(phase, .82, .94))
        # 新左手轨迹已包含随真实bolt后拉的机械接触；这里仅保留阶段标识用于报告。
    return {"l": left_target, "r": right_target}, magazine_target, grip_alpha, handle_alpha, bolt_alpha


def validate_clip(entry, clip, settings, config, arms, options, camera_transform, baseline, right_grip, bolt_reach_clearance):
    """校验实际保存片段；right_grip/bolt_reach_clearance来自同份作者配置，所有失败汇总且不改变原骨长/接触/安全域阈值。"""
    unreal.log(f"[CALL] FPHandClearance.validate_clip {entry['model']}/{clip['variant']}")
    # 加载使用运行子AnimBP的唯一配置源；同时核对实际资源路径，防止配置指向另一套同名动画。
    model = entry["model"]
    sequence = required(f"{DEST}/Arms/A_FP_{model}_Reload_{clip['variant']}", unreal.AnimSequence)
    pair = settings.empty_reload if clip["variant"] == "Empty" else settings.tactical_reload
    gun = pair.weapon_sequence
    if not isinstance(gun, unreal.AnimSequence) or gun.get_name() != clip["name"]:
        raise RuntimeError(f"Mechanical configuration mismatch {model}/{clip['variant']}")
    if abs(sequence.get_play_length() - clip["seconds"]) > .02 or abs(gun.get_play_length() - clip["seconds"]) > .02:
        raise RuntimeError(f"Clip duration mismatch {model}/{clip['variant']}")
    gun_options = unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=config.mesh)
    gun_start = pose_at(gun, 0, gun_options)
    support = settings.support_hand_grip  # 从导入后的子类配置读取，禁止复写原21或新14厘米常量。
    left_contacts = json.loads(LEFT_CONTACTS_PATH.read_text(encoding="utf-8"))  # 离线只读配置，实际手腕仍由保存的Sequence测量。
    left_samples = json.loads(LEFT_TRACKS_PATH.read_text(encoding="utf-8"))["weapons"][model][clip["variant"]]["samples"]
    magazine_grip = unreal.Vector(*left_contacts["weapons"][model]["Magazine"]["wrist_cm"])
    bolt_grip = unreal.Vector(*entry["bolt_grip_cm"])
    mag_offset = bone_transform(gun_start, "magazine").inverse_transform_location(magazine_grip)
    bolt_offset = bone_transform(gun_start, "bolt").inverse_transform_location(bolt_grip) if model == "Sniper" else unreal.Vector()
    frame_count = round(sequence.get_play_length() * FPS)
    phases = sorted({frame / frame_count for frame in range(frame_count + 1)} | set(clip["stages"].values()) | {event["phase"] for event in clip["sound_events"]})
    result = {"model": model, "variant": clip["variant"], "arms": sequence.get_path_name(), "mechanical": gun.get_path_name(),
              "seconds": sequence.get_play_length(), "support_grip_cm": vector_data(support),
              "right_grip_cm": vector_data(right_grip), "sniper_bolt_reach_clearance_cm": bolt_reach_clearance,
              "magazine_grip_cm": vector_data(magazine_grip), "frames": [], "errors": [], "warnings": [],
              "max_contact_error_cm": 0.0, "max_target_overreach_cm": 0.0, "max_bone_length_delta_cm": 0.0}
    for phase in phases:
        # 每帧真实Sequence重新评估；镜头位置从CDO完整Transform算出，不经过蓝图或运行时输入。
        pose = pose_at(sequence, phase, options)
        gun_pose = pose_at(gun, phase, gun_options)
        anchor = bone_transform(pose, "ik_hand_gun")
        targets, mag_target, grip_alpha, handle_alpha, bolt_alpha = intended_targets(model, clip, phase, anchor, gun_pose, mag_offset, bolt_offset, support, magazine_grip, bolt_grip, right_grip, bolt_reach_clearance,left_samples)
        frame_record = {"phase": phase, "seconds": phase * sequence.get_play_length(), "anchor_camera_cm": vector_data(camera_transform.transform_location(anchor.translation)),
                        "magazine_contact_alpha": grip_alpha, "handle_alpha": handle_alpha, "bolt_alpha": bolt_alpha, "sides": {}}
        for side in ("l", "r"):
            component, camera, chain = chain_data(pose, side, camera_transform)
            baseline_chain = baseline[side]  # 模板骨长真值，与当前帧实际距离比较以发现骨拉伸。
            upper_delta = abs(chain["upper_length_cm"] - baseline_chain["upper_length_cm"])
            lower_delta = abs(chain["lower_length_cm"] - baseline_chain["lower_length_cm"])
            result["max_bone_length_delta_cm"] = max(result["max_bone_length_delta_cm"], upper_delta, lower_delta)
            if max(upper_delta, lower_delta) > BONE_LENGTH_TOLERANCE_CM:
                result["errors"].append({"type": "bone_length_changed", "phase": phase, "side": side, "upper_delta_cm": upper_delta, "lower_delta_cm": lower_delta})
            total_length = baseline_chain["upper_length_cm"] + baseline_chain["lower_length_cm"]
            intended_distance = (targets[side] - component["shoulder"]).length()
            overreach = max(0.0, intended_distance - total_length)
            chain["intended_wrist_camera_cm"] = vector_data(camera_transform.transform_location(targets[side]))
            chain["intended_distance_cm"] = intended_distance
            chain["target_overreach_cm"] = overreach
            chain["actual_wrist_error_cm"] = (targets[side] - component["wrist"]).length()
            chain["reach_margin_cm"] = total_length - intended_distance
            result["max_target_overreach_cm"] = max(result["max_target_overreach_cm"], overreach)
            if overreach > REACH_TOLERANCE_CM or chain["shoulder_to_wrist_cm"] > total_length + REACH_TOLERANCE_CM:
                result["errors"].append({"type": "unreachable_wrist", "phase": phase, "side": side, "overreach_cm": overreach})
            if side == "l" and grip_alpha >= .995 and handle_alpha <= .005:
                # 稳定抓匣区比较手腕与实际机械骨的接触目标，明确排除伸手/放手/拉机柄混合阶段。
                contact_error = (component["wrist"] - mag_target).length()
                frame_record["left_magazine_contact_error_cm"] = contact_error
                result["max_contact_error_cm"] = max(result["max_contact_error_cm"], contact_error)
                if contact_error > CONTACT_TOLERANCE_CM:
                    result["errors"].append({"type": "magazine_contact_gap", "phase": phase, "side": side, "error_cm": contact_error})
            chain["segments"] = {}
            for name, first, second in (("upperarm", camera["shoulder"], camera["elbow"]), ("forearm", camera["elbow"], camera["wrist"])):
                core = segment_clearance(first, second, CORE_DEPTH_CM)
                warning = segment_clearance(first, second, WARNING_DEPTH_CM)
                chain["segments"][name] = {"core": core, "warning": warning}
                if core is not None and core["radius_cm"] < CORE_RADIUS_CM:
                    result["errors"].append({"type": "central_camera_intrusion", "phase": phase, "side": side, "segment": name, **core})
                elif warning is not None and warning["radius_cm"] < WARNING_RADIUS_CM:
                    result["warnings"].append({"type": "near_camera_review", "phase": phase, "side": side, "segment": name, **warning})
            frame_record["sides"][side] = chain
        result["frames"].append(frame_record)
    result["status"] = "failed" if result["errors"] else "passed_with_warnings" if result["warnings"] else "passed"
    unreal.log(f"FP_HAND_CLIP {model}/{clip['variant']} status={result['status']} samples={len(phases)} contact={result['max_contact_error_cm']:.3f}cm reach={result['max_target_overreach_cm']:.3f}cm errors={len(result['errors'])} warnings={len(result['warnings'])}")
    return result


def main():
    """独立Editor只读入口；写完整逐帧JSON后对实质失败抛异常，警告明确保留视觉复核边界。"""
    unreal.log("[CALL] FPHandClearance.main")
    unreal.load_module("AnimationBlueprintLibrary")
    # 报告一开始标记未完成，避免前次成功结果在本次资源缺失时被误读。
    report = {"status": "running", "method": "offline animation bone-centerline screening; not skinned triangle collision",
              "limits": {"fps": FPS, "bone_length_tolerance_cm": BONE_LENGTH_TOLERANCE_CM,
                         "target_overreach_tolerance_cm": REACH_TOLERANCE_CM, "contact_tolerance_cm": CONTACT_TOLERANCE_CM,
                         "core_depth_cm": CORE_DEPTH_CM, "core_radius_cm": CORE_RADIUS_CM,
                         "warning_depth_cm": WARNING_DEPTH_CM, "warning_radius_cm": WARNING_RADIUS_CM}, "clips": []}
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
    try:
        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        pose_profiles = json.loads(POSE_PROFILES_PATH.read_text(encoding="utf-8"))  # 当前作者配置纯值快照；不读取或修改制作工作台FBX/JSON。
        if pose_profiles["version"] != 1 or pose_profiles["units"] != "centimetres":
            raise RuntimeError("Unsupported FP pose profile units/version")
        right_grip_values = pose_profiles["right_wrist_weapon_cm"]  # 三个有限厘米分量，位于武器锚点骨空间。
        if len(right_grip_values) != 3 or not all(math.isfinite(value) for value in right_grip_values):
            raise RuntimeError("FP pose profile right wrist must contain three finite centimetre values")
        right_grip = unreal.Vector(*right_grip_values)  # 对应C++作者RightGrip，不再用模板右腕减GripPoint。
        bolt_reach_clearance = float(pose_profiles["sniper_bolt_reach_clearance_cm"])  # 狙击空仓绕行幅度，厘米；0合法，负值或非有限数与作者入口一致拒绝。
        if not math.isfinite(bolt_reach_clearance) or bolt_reach_clearance < 0:
            raise RuntimeError("FP pose profile sniper bolt clearance must be finite and nonnegative")
        report["pose_profile"] = {"path": str(POSE_PROFILES_PATH.relative_to(ROOT)).replace("\\", "/"),
                                  "version": pose_profiles["version"], "right_wrist_weapon_cm": vector_data(right_grip),
                                  "sniper_bolt_reach_clearance_cm": bolt_reach_clearance}  # 报告冻结本次期望来源，方便区分旧资源未重烘焙与数值阈值问题。
        arms = required("/Game/FirstPersonArms/Character/Mesh/SK_Mannequin_Arms", unreal.SkeletalMesh)
        template = required("/Game/FirstPersonArms/Animations/FP_Rifle_Idle", unreal.AnimSequence)
        options = unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=arms)
        base_pose = pose_at(template, 0, options)
        camera_transform, camera_description = mesh_camera_transform()
        report["camera_basis"] = camera_description
        baseline = {}  # 每侧真实模板链长；不使用假定27.5厘米平均骨长。
        for side in ("l", "r"):
            unused_component, unused_camera, baseline[side] = chain_data(base_pose, side, camera_transform)
        report["baseline"] = baseline
        for entry in manifest["weapons"]:
            bp = required(f"/Game/Weapons/Blueprints/BP_Weapon_{entry['model']}", unreal.Blueprint)
            config = unreal.get_default_object(bp.generated_class()).get_editor_property("config")
            if not config.mesh or not config.weapon_anim_layer_class:
                raise RuntimeError(f"Weapon animation configuration missing: {entry['model']}")
            settings = unreal.get_default_object(config.weapon_anim_layer_class).get_editor_property("anim_set")
            for clip in entry["clips"]:
                report["clips"].append(validate_clip(entry, clip, settings, config, arms, options, camera_transform, baseline, right_grip, bolt_reach_clearance))
        if len(report["clips"]) != 8:
            raise RuntimeError("Expected exactly eight reload clips")
        # 不在首个误差帧中断：汇总全部八套供作者集中修正，避免反复启动Editor寻找下一处问题。
        errors = sum(len(clip["errors"]) for clip in report["clips"])
        warnings = sum(len(clip["warnings"]) for clip in report["clips"])
        report["error_count"] = errors
        report["warning_count"] = warnings
        report["status"] = "failed" if errors else "passed_with_warnings" if warnings else "passed"
        REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
        if errors:
            raise RuntimeError(f"FP_HAND_CLEARANCE_FAILED errors={errors} warnings={warnings}; see {REPORT_PATH}")
        unreal.log(f"FP_HAND_CLEARANCE_SUCCESS clips=8 samples={sum(len(clip['frames']) for clip in report['clips'])} warnings={warnings}")
    except Exception as error:
        # 同步异常只写诊断；不会保存Content或改变运行状态，也不会留下running伪成功状态。
        report["status"] = "failed"
        report["exception"] = str(error)
        REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")
        raise


if __name__ == "__main__":
    main()
