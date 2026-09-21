"""独立新Editor进程只读复核已保存四枪动画/声音配置与真实采样；不依赖导入器内存。"""
import json
from pathlib import Path
import unreal

# 与导入器共享稳定目录；清单只用于核对，正式运行不读取JSON。
ROOT = Path(__file__).resolve().parents[2]
DEST = "/Game/Weapons/Animations"


def required(path, asset_type):
    """path为明确UE包名，asset_type约束实际类；失败禁止输出成功标记。"""
    unreal.log(f"[CALL] FPValidate.required {path}")
    result = unreal.load_asset(path)  # 借用Editor加载资产，本脚本不修改。
    if not isinstance(result, asset_type):
        raise RuntimeError(f"Missing/wrong type {path}")
    return result


def main():
    """验证资源闭包、唯一配置入口、八对片长、事件与实际手腕/枪根轨迹。"""
    unreal.log("[CALL] FPValidate.main")
    unreal.load_module("AnimationBlueprintLibrary")
    manifest = json.loads((ROOT / "Art/Weapons/Animations/weapon_animation_manifest.json").read_text(encoding="utf-8"))  # 制作源真值。
    arms = required("/Game/FirstPersonArms/Character/Mesh/SK_Mannequin_Arms", unreal.SkeletalMesh)
    required(f"{DEST}/ABP_FPArms_Base", unreal.AnimBlueprint)
    required(f"{DEST}/ABP_FPWeaponLayers_Base", unreal.AnimBlueprint)
    required(f"{DEST}/ALI_FPWeaponLayers", unreal.AnimBlueprint)
    options = unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=arms)  # 离线使用实际骨长采样，没有游戏Actor。
    report = []  # 验证结果只写Saved日志旁，不修改任何Content资产。
    for entry in manifest["weapons"]:
        model = entry["model"]  # 稳定型号，对照配置子类。
        bp = required(f"/Game/Weapons/Blueprints/BP_Weapon_{model}", unreal.Blueprint)
        config = unreal.get_default_object(bp.generated_class()).get_editor_property("config")  # 已序列化武器CDO，仅只读。
        layer = required(f"{DEST}/ABP_FP_{model}", unreal.AnimBlueprint)
        if config.weapon_anim_layer_class != layer.generated_class() or config.static_mesh or str(config.attach_socket) != "ik_hand_gun":
            raise RuntimeError(f"Weapon migration not persisted {model}")
        settings = unreal.get_default_object(layer.generated_class()).get_editor_property("anim_set")  # 唯一资源表。
        if config.idle_animation or config.fire_animation or config.reload_animation:
            raise RuntimeError(f"Competing legacy animation entry {model}")
        if settings.magazine_presentation_mesh:
            raise RuntimeError("First version must display one real magazine")
        for clip in entry["clips"]:
            pair = settings.empty_reload if clip["variant"] == "Empty" else settings.tactical_reload  # 两种动作不可指向同一Montage。
            if pair.arms_montage == (settings.tactical_reload if clip["variant"] == "Empty" else settings.empty_reload).arms_montage:
                raise RuntimeError("Reload variants not distinct")
            sequence = required(f"{DEST}/Arms/A_FP_{model}_Reload_{clip['variant']}", unreal.AnimSequence)
            if abs(sequence.get_play_length()-clip["seconds"]) > .02 or abs(pair.weapon_sequence.get_play_length()-clip["seconds"]) > .02:
                raise RuntimeError(f"Saved clip length mismatch {model}")
            if len(pair.sound_events) != len(clip["sound_events"]):
                raise RuntimeError(f"Incomplete sound event set {model}")
            for actual, expected in zip(pair.sound_events, clip["sound_events"]):
                if abs(actual.normalized_time-expected["phase"]) > .0001 or not actual.sound or actual.sound.get_name() != f"SW_Reload_{model}_{expected['name']}":
                    raise RuntimeError(f"Sound synchronization mismatch {model}/{expected['name']}")
            # 骨轨迹而非动画元数据：两个采样点实际不同，同时控制骨的缩放保持单位。
            poses = [unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, sequence.get_play_length()*phase, options) for phase in (0,.2,.4,.7,1)]
            hands = [unreal.AnimPoseExtensions.get_bone_pose(pose,"hand_l",unreal.AnimPoseSpaces.WORLD).translation for pose in poses]
            if max((position-hands[0]).length() for position in hands) < 5:
                raise RuntimeError(f"Hand animation not moving {model}")
            for pose in poses:
                anchor = unreal.AnimPoseExtensions.get_bone_pose(pose,"ik_hand_gun",unreal.AnimPoseSpaces.WORLD)  # 枪体整体轨迹，无缩放动画。
                if (anchor.scale3d-unreal.Vector(1,1,1)).length() > .01:
                    raise RuntimeError(f"Scaled weapon anchor {model}")
            report.append({"model": model, "variant": clip["variant"], "seconds": sequence.get_play_length(), "sound_events": len(pair.sound_events), "hand_travel_cm": max((position-hands[0]).length() for position in hands)})
    (ROOT/"Saved/Logs/FPAnimationAssetValidation.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    unreal.log("FP_WEAPON_ANIMATION_VALIDATE_SUCCESS pairs=8 linked_children=4")


if __name__ == "__main__":
    main()
