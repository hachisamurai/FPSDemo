"""UE5.4 内离线导入四枪开火声、创建三变体 Cue，并只更新武器 BP 的 Config.FireSound。

其他配置（动画/模型/伤害/射速）按当前资产原值保留；所有 uasset 由 Editor API 保存。
"""
from pathlib import Path
import hashlib
import json
import sys
import unreal

# 工具与源报告位于项目内部；导入辅助函数复用旧音频流程的类型检查与保存行为。
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0, str(ROOT / "Scripts/Audio"))
from import_weapon_sfx import load_or_create

# 独立目录不覆盖 Sword Fire/Hit；旧资产继续为未配置的原生类提供回退。
SOURCE = ROOT / "SourceAssets/Audio/SFX/Weapons/Breach"
CONTENT = "/Game/Audio/SFX/Weapons/Breach"
NAMES = ("Pistol", "Rifle", "Shotgun", "Sniper")


def settings_for(name, report):
    """name 为四枪 ID，report 为源验证报告；返回 Editor 同步借用的 Cue 配置结构体。"""
    unreal.log(f"[FourWeaponAudio] settings_for {name}")
    # profile 包含制作时长和小幅音高范围；增益/总音量分离以保留多声部混音余量。
    profile = report["profiles"][name]
    settings = unreal.DemoWeaponCueSettings()
    settings.set_editor_property("pitch_range", unreal.Vector2D(*profile["pitch_range"]))
    settings.set_editor_property("gain_db_range", unreal.Vector2D(*report["cue_gain_db_range"]))
    settings.set_editor_property("duration_seconds", profile["duration_seconds"])
    settings.set_editor_property("volume", report["cue_volume"])
    return settings


def import_waves(name, report):
    """name 指定枪型，report 含磁盘哈希；返回三个完成导入并保存的 SoundWave。"""
    unreal.log(f"[FourWeaponAudio] import_waves {name}")
    # tasks 同步导入期间保活；先验证全部三个源文件，避免哈希失败后发生部分导入。
    tasks = []
    for variant in range(1, 4):  # variant 为 1..3 的稳定资源编号。
        path = SOURCE / name / f"SW_{name}_Fire_{variant:02d}.wav"
        record = next(row for row in report["files"] if row["file"] == path.relative_to(ROOT).as_posix())
        if hashlib.sha256(path.read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError(f"Source changed after validation: {path}")
        if (record["sample_rate_hz"], record["bit_depth"], record["channels"], record["clipped_samples"]) != (48000, 24, 1, 0):
            raise ValueError(f"Invalid WAV report: {path}")
        # task 仅负责当前源的同步导入；路径已由本脚本固定，不覆盖别的类别。
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(path))
        task.set_editor_property("destination_path", f"{CONTENT}/{name}/Waves")
        task.set_editor_property("destination_name", path.stem)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    # waves 由 Editor 包拥有；Python 列表只在 Cue 构图时保持访问引用。
    waves = []
    for task in tasks:  # paths 必须来自真实导入结果，禁止把预期路径当成功证明。
        paths = task.get_editor_property("imported_object_paths")
        if len(paths) != 1:
            raise RuntimeError(f"Expected one imported SoundWave: {paths}")
        sound = unreal.EditorAssetLibrary.load_asset(paths[0].split(".", 1)[0])
        if not isinstance(sound, unreal.SoundWave):
            raise TypeError(f"Not a SoundWave: {paths}")
        sound.set_editor_property("looping", False)
        sound.set_editor_property("volume", 1.0)
        sound.set_editor_property("pitch", 1.0)
        sound.set_editor_property("sample_rate_quality", unreal.SoundwaveSampleRateSettings.MAX)
        if not unreal.EditorAssetLibrary.save_loaded_asset(sound, only_if_is_dirty=False):
            raise RuntimeError(f"Failed to save {sound.get_name()}")
        waves.append(sound)
    return waves


def assign_weapon(name, cue):
    """name 为既有 BP 后缀，cue 为已经验证/保存的对象；只改 FireSound，返回更新前后资产路径。"""
    unreal.log(f"[FourWeaponAudio] assign_weapon {name}")
    # CDO/Config 在同步编辑期间借用；整个结构体读改写保证当前模型/动画/属性配置不会被默认值覆盖。
    path = f"/Game/Weapons/Blueprints/BP_Weapon_{name}"
    blueprint = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(blueprint, unreal.Blueprint):
        raise TypeError(f"Missing weapon blueprint: {path}")
    cdo = unreal.get_default_object(blueprint.generated_class())
    config = cdo.get_editor_property("config")
    previous = config.get_editor_property("fire_sound")
    config.set_editor_property("fire_sound", cue)
    cdo.set_editor_property("config", config)
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save BP: {path}")
    if unreal.get_default_object(blueprint.generated_class()).get_editor_property("config").get_editor_property("fire_sound") != cue:
        raise RuntimeError(f"BP compile lost sound assignment: {path}")
    return {"weapon": path, "previous_sound": previous.get_path_name() if previous else None,
            "fire_sound": cue.get_path_name()}


def main():
    """校验报告→导入→构建/验证→赋给 BP；任何失败抛异常，成功报告仅在全部完成后生成。"""
    unreal.log("[FourWeaponAudio] main")
    report = json.loads((SOURCE / "audio_validation.json").read_text(encoding="utf-8"))  # 磁盘验证是导入前置条件。
    if not report["passed"] or len(report["files"]) != 12:
        raise ValueError("Expected 12 validated fire sources")
    # 沿用已校验 Fire 衰减，保持当前枪口距离和整体场景听觉空间一致。
    attenuation = unreal.EditorAssetLibrary.load_asset("/Game/Audio/SFX/Weapons/Sword/Settings/ATT_Sword_Fire")
    if not isinstance(attenuation, unreal.SoundAttenuation):
        raise RuntimeError("Existing fire attenuation missing")
    assignments = []  # 每个已经成功持久化的武器绑定审计。
    for name in NAMES:  # name 是当前固定的武器类别，cue/waves 是 Editor 所有的借用对象。
        waves = import_waves(name, report)
        cue = load_or_create(f"SC_{name}_Fire", f"{CONTENT}/{name}/Cues", unreal.SoundCue, unreal.SoundCueFactoryNew())
        if not unreal.DemoAudioAssetLibrary.configure_weapon_cue_with_settings(cue, waves, attenuation, settings_for(name, report)):
            raise RuntimeError(f"Cue validation failed: {name}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(cue, only_if_is_dirty=False):
            raise RuntimeError(f"Cue save failed: {name}")
        assignments.append(assign_weapon(name, cue))
    # JSON 字段说明见音频模块文档；日志与报告只证明导入，不假装经过扬声器试听。
    output = ROOT / "Saved/Audio/four_weapon_audio_import.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({"passed": True, "assignments": assignments,
                                  "editor_version": unreal.SystemLibrary.get_engine_version()}, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    unreal.log("FOUR_WEAPON_AUDIO_IMPORT_SUCCESS")


if __name__ == "__main__":
    main()
