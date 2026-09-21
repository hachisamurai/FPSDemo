"""在独立 Editor 进程重载四枪 BP 与 Cue，验证硬引用和真实图节点；不写任何内容资源。"""
from pathlib import Path
import json
import sys
import unreal

# 与导入脚本共享设置转换，避免校验端悄悄采用另一组音高/时长默认值。
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0, str(ROOT / "Scripts/Audio"))
from import_four_weapon_sfx import CONTENT, NAMES, settings_for


def main():
    """从磁盘加载四个 BP 和所有随机分支；失败抛异常，成功写可追踪日志。"""
    unreal.log("[FourWeaponAudio] validate main")
    report = json.loads((ROOT / "SourceAssets/Audio/SFX/Weapons/Breach/audio_validation.json").read_text(encoding="utf-8"))
    for name in NAMES:  # name 是类别，cue/blueprint/config 为只读借用 Editor 对象。
        cue = unreal.EditorAssetLibrary.load_asset(f"{CONTENT}/{name}/Cues/SC_{name}_Fire")
        if not isinstance(cue, unreal.SoundCue) or not unreal.DemoAudioAssetLibrary.validate_weapon_cue_with_settings(cue, settings_for(name, report)):
            raise RuntimeError(f"Persisted Cue invalid: {name}")
        blueprint = unreal.EditorAssetLibrary.load_asset(f"/Game/Weapons/Blueprints/BP_Weapon_{name}")
        config = unreal.get_default_object(blueprint.generated_class()).get_editor_property("config")
        if config.get_editor_property("fire_sound") != cue:
            raise RuntimeError(f"Persisted weapon does not reference its Cue: {name}")
        unreal.log(f"FOUR_WEAPON_AUDIO_BINDING_VALID weapon={name} cue={cue.get_path_name()} rpm={config.rounds_per_minute}")
    # 旧命中 Cue/原生回退开火声也必须可重载，新增参数接口默认不应改变它们。
    for kind in ("Fire", "Hit"):  # kind 标识不由新导入脚本修改的原有资源。
        legacy = unreal.EditorAssetLibrary.load_asset(f"/Game/Audio/SFX/Weapons/Sword/{kind}/Cues/SC_Sword_{kind}")
        if not unreal.DemoAudioAssetLibrary.validate_weapon_cue(legacy):
            raise RuntimeError(f"Legacy audio regression: {kind}")
    unreal.log("FOUR_WEAPON_AUDIO_RELOAD_VALIDATION_SUCCESS")


if __name__ == "__main__":
    main()
