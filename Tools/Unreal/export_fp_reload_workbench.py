"""只读导出当前四枪手臂动作及组件空间测量，作为Blender重设计工作台；不导入、不保存Content。"""
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

import unreal

# 工作台目录独立于原模板参考及新制作源；每次显式执行覆盖本目录同名快照。
ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "Art/Player/Animations/CurrentUE"
ARMS_PATH = "/Game/FirstPersonArms/Character/Mesh/SK_Mannequin_Arms"
ANIMATION_PATH = "/Game/Weapons/Animations"
# 骨名使用UE当前共享骨架；lowerarm原点是肘关节，hand原点是腕关节。
SAMPLE_BONES = (
    "root", "ik_hand_gun", "ik_hand_r", "ik_hand_l",
    "clavicle_r", "upperarm_r", "lowerarm_r", "lowerarm_twist_01_r", "lowerarm_twist_02_r", "hand_r",
    "clavicle_l", "upperarm_l", "lowerarm_l", "lowerarm_twist_01_l", "lowerarm_twist_02_l", "hand_l",
)
# UE包文件的开始SHA256；只追踪本次读取资源，结束时逐个确认没有改写。
SOURCE_HASHES = {}


def required(path, asset_type):
    """path为指定/Game包，asset_type限定资源类；借用UE包对象并冻结磁盘SHA256，不保存资源。"""
    unreal.log(f"[CALL] FPWorkbench.required {path}")
    asset = unreal.load_asset(path)  # 当前Editor只读对象，最终生命周期仍属于资源包。
    if not isinstance(asset, asset_type):
        raise RuntimeError(f"Missing or wrong resource type: {path}")
    disk_path = ROOT / "Content" / (path.removeprefix("/Game/").split(".")[0] + ".uasset")  # 项目/Game文件映射，不访问引擎或插件包。
    if not disk_path.is_file():
        raise RuntimeError(f"Cannot locate source package: {disk_path}")
    SOURCE_HASHES[str(disk_path.relative_to(ROOT)).replace("\\", "/")] = hashlib.sha256(disk_path.read_bytes()).hexdigest()
    return asset


def transform_data(value):
    """value为UE组件/局部空间FTransform值；返回cm/xyzw及显式列向量4x4矩阵，不手工猜测FBX轴变换。"""
    unreal.log("[CALL] FPWorkbench.transform_data")
    origin = value.transform_location(unreal.Vector(0, 0, 0))  # 仿射矩阵平移列，单位UE厘米。
    axis_x = value.transform_location(unreal.Vector(1, 0, 0)) - origin  # 包含缩放的局部X单位轴在目标空间的像。
    axis_y = value.transform_location(unreal.Vector(0, 1, 0)) - origin  # 包含缩放的局部Y单位轴。
    axis_z = value.transform_location(unreal.Vector(0, 0, 1)) - origin  # 包含缩放的局部Z单位轴。
    return {
        "translation_cm": [value.translation.x, value.translation.y, value.translation.z],
        "rotation_xyzw": [value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w],
        "scale": [value.scale3d.x, value.scale3d.y, value.scale3d.z],
        "matrix4x4_column_vector": [
            [axis_x.x, axis_y.x, axis_z.x, origin.x],
            [axis_x.y, axis_y.y, axis_z.y, origin.y],
            [axis_x.z, axis_y.z, axis_z.z, origin.z],
            [0.0, 0.0, 0.0, 1.0],
        ],
    }


def export_fbx(asset):
    """asset为已保存SkeletalMesh/AnimSequence；动画包含原预览手臂网格，完整导出后核对文件并返回快照元数据。"""
    unreal.log(f"[CALL] FPWorkbench.export_fbx {asset.get_path_name()}")
    destination = OUTPUT / f"{asset.get_name()}.fbx"  # 只写工具独占快照目录，不使用资源导入源路径。
    options = unreal.FbxExportOption()  # 同步任务私有选项，不写Editor全局默认或保存资产。
    options.set_editor_property("ascii", False)
    options.set_editor_property("fbx_export_compatibility", unreal.FbxExportCompatibility.FBX_2013)
    options.set_editor_property("force_front_x_axis", False)
    options.set_editor_property("level_of_detail", False)
    options.set_editor_property("collision", False)
    options.set_editor_property("export_preview_mesh", True)
    options.set_editor_property("map_skeletal_motion_to_root", False)
    task = unreal.AssetExportTask()  # 任务和Exporter只活到本次同步导出完成。
    task.object = asset
    task.filename = str(destination)
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.options = options
    if isinstance(asset, unreal.AnimSequence):
        task.exporter = unreal.AnimSequenceExporterFBX()
    if not unreal.Exporter.run_asset_export_task(task) or not destination.is_file() or destination.stat().st_size < 1024:
        raise RuntimeError(f"FBX export failed: {asset.get_path_name()}; errors={list(task.errors)}")
    return {"file": destination.name, "absolute_file": str(destination).replace("\\", "/"),
            "bytes": destination.stat().st_size, "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}


def phase_samples(clip):
    """clip为源清单中的机械阶段或None待机；合并关键事件和均匀十等分，便于定位穿模开始/结束边界。"""
    unreal.log("[CALL] FPWorkbench.phase_samples")
    phases = {round(index / 10.0, 6) for index in range(11)}  # 0..1包含端点，保持跨武器统一观察时刻。
    phases.update((.16, .28, .32, .35, .39, .68, .74, .88))  # 制作工作台指定的拔匣/手腕过渡/复位观察点，Idle也保留便于同相位比较。
    if clip:
        phases.update(float(value) for value in clip["stages"].values())
        phases.update(float(event["phase"]) for event in clip["sound_events"])  # 声音阶段用于核对机械接触，而不作为动画Notify。
        phases.update(float(target["phase"]) for target in clip["hand_targets"])  # 覆盖机构拉栓等稀疏关键阶段。
    return sorted(phases)


def sample_animation(sequence, options, phases, config, weapon_sequence, weapon_options):
    """在关键phases离线求UE实际骨姿势；options均借用当前网格，结果只有JSON值，WORLD枚举表示组件空间。"""
    unreal.log(f"[CALL] FPWorkbench.sample_animation {sequence.get_path_name()}")
    samples = []  # 一段动作的所有关键时刻，导出完成后一次写JSON。
    for phase in phases:  # phase为归一化动作时间[0,1]，不加入运行时Slot混合/支撑手IK。
        unreal.log(f"[CALL] FPWorkbench.sample_phase sequence={sequence.get_name()} phase={phase:.4f}")
        seconds = sequence.get_play_length() * phase  # 该手臂素材原始秒数，不受GAS运行时速率影响。
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, seconds, options)  # Raw+当前网格重定向骨长，离线不创建Actor。
        component_bones = {}  # 各骨到手臂网格组件空间的完整姿态，而非局部平移猜测。
        for name in SAMPLE_BONES:  # 右前臂根/腕与枪托冲突可由upperarm/lowerarm/hand实际链定位。
            component_bones[name] = transform_data(unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD))
        anchor = unreal.AnimPoseExtensions.get_bone_pose(pose, "ik_hand_gun", unreal.AnimPoseSpaces.WORLD)  # 武器挂接控制骨真实组件变换。
        gun_to_arms = config.attach_offset * anchor  # UE ComposeTransforms是先局部offset后挂点；当前配置应为恒等offset。
        sample = {"phase": phase, "seconds": seconds, "component_bones": component_bones,
                  "weapon_anchor_component": transform_data(anchor),
                  "weapon_mesh_to_arms_component": transform_data(gun_to_arms)}  # 枪局部几何转换到手臂组件空间的对齐矩阵。
        if weapon_sequence:
            gun_pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(weapon_sequence, weapon_sequence.get_play_length()*phase, weapon_options)  # 同归一化phase的实际机械资源。
            gun_bones = {}  # 枪内骨分别保留枪组件与最终手臂组件变换，方便直接放入Blender同场景。
            for bone_name in unreal.AnimPoseExtensions.get_bone_names(gun_pose):  # 读取导入后的真实骨列表，不假定手枪也有bolt。
                gun_bone = unreal.AnimPoseExtensions.get_bone_pose(gun_pose, bone_name, unreal.AnimPoseSpaces.WORLD)  # 枪网格局部组件姿势。
                gun_bones[str(bone_name)] = {"weapon_component": transform_data(gun_bone),
                                            "arms_component": transform_data(gun_bone * gun_to_arms)}
            sample["mechanical_bones"] = gun_bones
        samples.append(sample)
    return samples


def main():
    """新Editor进程同步导出12段当前手臂和一个网格；仅末尾发布成功清单，并验证所有已读包SHA256保持不变。"""
    unreal.log("[CALL] FPWorkbench.main")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    unreal.load_module("AnimationBlueprintLibrary")
    arms = required(ARMS_PATH, unreal.SkeletalMesh)  # 当前已保存模板蒙皮，任何导出动作都不改变该资源。
    options = unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=arms)  # 默认Raw求值，含当前骨长重定向，不提取根运动。
    source = json.loads((ROOT / "Art/Weapons/Animations/weapon_animation_manifest.json").read_text(encoding="utf-8"))  # 只用于列出阶段和机械FBX源路径。
    report = {"version": 1, "exported_utc": datetime.now(timezone.utc).isoformat(),
              "status": "current UE snapshot for redesign; NOT final visual approval",
              "units": "centimetres", "coordinates": "UE +X forward +Y right +Z up",
              "pose_space": "AnimPoseSpaces.WORLD is skeletal mesh COMPONENT space, not scene world",
              "matrix_convention": "column vector: target_position = matrix4x4_column_vector @ [x,y,z,1]; translation in final column",
              "fbx_options": {"compatibility": "FBX_2013", "force_front_x_axis": False, "export_preview_mesh": True, "lods": False},
              "evaluation": "Raw AnimSequence with current preview mesh; no runtime Slot blend, support-hand IK or camera sway",
              "bone_roles": {"weapon_anchor": "ik_hand_gun", "right_wrist": "hand_r", "right_forearm_and_elbow_joint": "lowerarm_r", "right_shoulder": "upperarm_r"},
              "arms_mesh": {"ue_asset": ARMS_PATH, "fbx": export_fbx(arms)}, "animations": []}  # 单一工作台清单，所有路径为真实导出位置。
    for entry in source["weapons"]:  # 四枪顺序与源机械清单一致，不搜索或导出额外Content。
        model = entry["model"]  # 稳定Pistol/Rifle/Shotgun/Sniper标识。
        bp = required(f"/Game/Weapons/Blueprints/BP_Weapon_{model}", unreal.Blueprint)  # 读取已保存正式配置，而非脚本假定值。
        config = unreal.get_default_object(bp.generated_class()).get_editor_property("config")  # 只读CDO值。
        layer = required(f"{ANIMATION_PATH}/ABP_FP_{model}", unreal.AnimBlueprint)  # 单一AnimSet配置来源。
        settings = unreal.get_default_object(layer.generated_class()).get_editor_property("anim_set")  # 首版不生成手部弹匣替身。
        weapon_mesh = required(config.mesh.get_path_name(), unreal.SkeletalMesh)  # 配置实际网格，供机械同phase求值。
        weapon_options = unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=weapon_mesh)  # 只在当前武器使用，不访问运行Pawn。
        for variant in ("Idle", "Tactical", "Empty"):  # 4待机+8换弹；Fire不属于本次重设计导出范围。
            clip = next((item for item in entry["clips"] if item["variant"] == variant), None)  # Idle没有机械动作，使用当前参考网格。
            suffix = "Idle" if variant == "Idle" else f"Reload_{variant}"  # UE当前动作命名契约。
            sequence = required(f"{ANIMATION_PATH}/Arms/A_FP_{model}_{suffix}", unreal.AnimSequence)  # 直接读取现有成品，不调用任何重烘焙函数。
            weapon_sequence = required(f"{ANIMATION_PATH}/Mechanical/{clip['name']}", unreal.AnimSequence) if clip else None  # 当前机械Sequence与手臂配对。
            animation = {"model": model, "variant": variant, "ue_asset": sequence.get_path_name(), "seconds": sequence.get_play_length(),
                         "fbx": export_fbx(sequence), "attach_socket": str(config.attach_socket), "attach_offset": transform_data(config.attach_offset),
                         "support_hand_grip_cm": [settings.support_hand_grip.x, settings.support_hand_grip.y, settings.support_hand_grip.z],
                         "support_hand_ik_alpha": settings.support_hand_ik_alpha,
                         "weapon_mesh_ue_asset": weapon_mesh.get_path_name(),
                         "weapon_mesh_source_fbx": str(ROOT / "Art/Weapons/Animations/FBX" / f"{entry['mesh']}.fbx").replace("\\", "/"),
                         "mechanical_source_fbx": str(ROOT / "Art/Weapons/Animations/FBX" / f"{clip['name']}.fbx").replace("\\", "/") if clip else None,
                         "samples": sample_animation(sequence, options, phase_samples(clip), config, weapon_sequence, weapon_options)}  # 当前动作完整对齐快照。
            report["animations"].append(animation)
    for relative, before_hash in SOURCE_HASHES.items():  # 文件级验证导出过程没有写入读取过的Content包。
        if hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() != before_hash:
            raise RuntimeError(f"Source package changed during read-only export: {relative}")
    report["source_content_sha256"] = SOURCE_HASHES
    report["source_content_unchanged"] = True
    (OUTPUT / "current_ue_workbench_manifest.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    (OUTPUT / "README.md").write_text(
        "# 当前UE手臂动作重设计快照\n\n"
        "由Tools/Unreal/export_fp_reload_workbench.py只读导出，包含4个Idle、8个Reload及原始手臂网格。\n"
        "这些是存在右前臂穿枪托问题的当前输入，不是最终视觉通过版本。Content资源没有保存，文件SHA256已前后核对。\n\n"
        "current_ue_workbench_manifest.json记录实际UE资产、FBX路径/哈希、片长、挂点、配置和关键phase。\n"
        "component_bones及weapon_anchor_component使用UE厘米组件空间；matrix4x4_column_vector为列向量，平移在最后一列。\n"
        "lowerarm_r原点就是肘关节，hand_r为腕关节；前臂段为这两点之间，upperarm_r为肩部。\n"
        "weapon_mesh_to_arms_component可把枪的模型空间点变换到手臂组件空间，mechanical_bones同时提供两套组件矩阵。\n"
        "FBX保留引擎正式轴转换（force_front_x_axis=false），Blender正常FBX导入后应以至少3个不共线骨位置核对轴/单位，不能直接把UE矩阵当Blender矩阵。\n"
        "动画FBX含原预览手臂网格；导入多段时需管理重复网格和Action，禁止覆盖正式Content。\n"
        "采样来自Raw序列，不含运行时Slot混合、公共支撑手IK、相机位置和轻摆；待机IK的当前输入在清单中，换弹时该权重由运行时强制为0。\n"
        "工具可重跑覆盖本目录同名快照；导出失败不发布成功标记，修正后重跑，开始重新制作前请另存原快照。\n",
        encoding="utf-8")
    unreal.log(f"FP_RELOAD_WORKBENCH_EXPORT_SUCCESS animations={len(report['animations'])} packages={len(SOURCE_HASHES)} manifest={OUTPUT / 'current_ue_workbench_manifest.json'}")


if __name__ == "__main__":
    main()
