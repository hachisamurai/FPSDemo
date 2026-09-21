"""在 UE Editor Python 内导入六个 WAV，创建/更新两个 Cue、分类衰减资产并保存验证。

运行前编译 FPSDemoEditor 模块。UE5.4 图连接由 DemoAudioAssetLibrary 桥接；
本文件只调用 Editor API，从不直接读取/伪造/写入 .uasset 字节。
"""
from pathlib import Path
import hashlib
import json
import unreal

# 项目根由 Editor 配置决定，允许 GUI Execute Python Script 或命令行运行。
PROJECT_ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
# 与合成脚本一致的用户指定源目录，报告中的哈希用于防止导入过期或错误文件。
SOURCE_ROOT = PROJECT_ROOT / "SourceAssets/Audio/SFX/Weapons/Sword"
# 内容路径分离用途；Settings 只包含共享配置，不与 SoundWave 混放。
CONTENT_ROOT = "/Game/Audio/SFX/Weapons/Sword"


def load_or_create(name: str, folder: str, asset_class, factory):
    """name/folder 为目标资产标识，asset_class/factory 为 Editor 类型；复用兼容资产供重复导入。"""
    unreal.log(f"[WeaponAudioImport] load_or_create {folder}/{name}")
    # path/asset 仅用于 Editor UObject 访问；同名异类资源保留并报错。
    path = f"{folder}/{name}"
    asset = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset:
        if not isinstance(asset, asset_class):
            raise TypeError(f"Refusing to replace unrelated asset {path}")
        return asset
    unreal.EditorAssetLibrary.make_directory(folder)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, asset_class, factory)
    if not asset:
        raise RuntimeError(f"Editor failed to create {path}")
    return asset


def configure_attenuation(kind: str):
    """kind 为 Fire/Hit，返回 Editor 创建并持有的空间衰减资产；所有距离单位 cm。"""
    unreal.log(f"[WeaponAudioImport] configure_attenuation {kind}")
    # settings 先完整构造再赋回资产，避免 Python struct 复制导致嵌套设置未生效。
    asset = load_or_create(f"ATT_Sword_{kind}", f"{CONTENT_ROOT}/Settings", unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = unreal.SoundAttenuationSettings()
    settings.set_editor_property("attenuate", True)
    settings.set_editor_property("spatialize", True)
    settings.set_editor_property("distance_algorithm", unreal.AttenuationDistanceModel.LINEAR)
    settings.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(150.0 if kind == "Fire" else 120.0, 0.0, 0.0))
    settings.set_editor_property("falloff_distance", 3000.0 if kind == "Fire" else 2200.0)
    asset.set_editor_property("attenuation", settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save attenuation {kind}")
    return asset


def import_waves(kind: str, source_report: dict) -> list:
    """kind 指定用途，source_report 为复读校验报告；返回三个成功导入的 SoundWave 对象。"""
    unreal.log(f"[WeaponAudioImport] import_waves {kind}")
    # tasks 在同步 AssetTools 调用期间由 Python 保活；不用异步 lambda。
    tasks = []
    # variant 为稳定的变体编号 1..3，用来匹配源报告和目标资源名。
    for variant in range(1, 4):
        # filename 与 record 必须匹配现有报告，导入前验证实际文件哈希。
        filename = SOURCE_ROOT / kind / f"SW_Sword_{kind}_{variant:02d}.wav"
        record = next(row for row in source_report["files"] if row["file"] == filename.relative_to(PROJECT_ROOT).as_posix())
        if hashlib.sha256(filename.read_bytes()).hexdigest() != record["sha256"]:
            raise ValueError(f"WAV changed since validation: {filename}")
        if (record["sample_rate_hz"], record["bit_depth"], record["channels"], record["duration_seconds"], record["clipped_samples"]) != (48000, 24, 1, 0.4, 0):
            raise ValueError(f"Source validation failed: {filename}")
        # task 持有单个 WAV 的同步导入配置，加入列表后由 AssetTools 执行。
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(filename))
        task.set_editor_property("destination_path", f"{CONTENT_ROOT}/{kind}/Waves")
        task.set_editor_property("destination_name", filename.stem)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("replace_existing_settings", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    # waves 是本次 Cue 的三个输入，顺序对应变体编号，但 Cue 随机选择。
    waves = []
    # task 是已完成导入的任务，paths 是 Editor 实际返回的对象路径而非猜测路径。
    for task in tasks:
        paths = task.get_editor_property("imported_object_paths")
        if len(paths) != 1:
            raise RuntimeError(f"Import did not produce exactly one SoundWave: {paths}")
        # 导入任务返回 ObjectPath，EditorAssetLibrary 需要 PackageName；显式截去对象段消除弃用警告。
        sound_wave = unreal.EditorAssetLibrary.load_asset(paths[0].split(".", 1)[0])
        if not isinstance(sound_wave, unreal.SoundWave):
            raise TypeError(f"Imported wrong asset type: {paths[0]}")
        sound_wave.set_editor_property("looping", False)
        sound_wave.set_editor_property("volume", 1.0)
        sound_wave.set_editor_property("pitch", 1.0)
        # 不使用低采样率重采样档；原始 WAV 始终为 PCM24，引擎内部格式由平台编码器决定。
        sound_wave.set_editor_property("sample_rate_quality", unreal.SoundwaveSampleRateSettings.MAX)
        if not unreal.EditorAssetLibrary.save_loaded_asset(sound_wave, only_if_is_dirty=False):
            raise RuntimeError(f"Failed to save {paths[0]}")
        waves.append(sound_wave)
    return waves


def main() -> None:
    """导入、编排、验证并保存；任一步出错抛异常使 commandlet 失败，不输出虚假完成报告。"""
    unreal.log("[WeaponAudioImport] main")
    if not hasattr(unreal, "DemoAudioAssetLibrary"):
        raise RuntimeError("Build/load FPSDemoEditor before running this script")
    # source_report 来自实际生成报告；cue_paths 保存验证通过的 Editor 包路径。
    source_report = json.loads((SOURCE_ROOT / "audio_validation.json").read_text(encoding="utf-8"))
    cue_paths = []
    # kind 决定用途分类；waves/attenuation/cue 为该组 Editor 资产的同步借用引用。
    for kind in ("Fire", "Hit"):
        waves = import_waves(kind, source_report)
        attenuation = configure_attenuation(kind)
        cue = load_or_create(f"SC_Sword_{kind}", f"{CONTENT_ROOT}/{kind}/Cues", unreal.SoundCue, unreal.SoundCueFactoryNew())
        if not unreal.DemoAudioAssetLibrary.configure_weapon_cue(cue, waves, attenuation):
            raise RuntimeError(f"Cue authoring failed: {kind}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(cue, only_if_is_dirty=False):
            raise RuntimeError(f"Cue save failed: {kind}")
        cue_paths.append(cue.get_path_name())
    # 报告是普通 JSON，说明导入来源与配置；.uasset 仅由上面的 Editor API 创建。
    report = {"passed": True, "editor_version": unreal.SystemLibrary.get_engine_version(), "cues": cue_paths,
              "pitch_range": [0.95, 1.05], "gain_db_range": [-1.5, 1.5],
              "gain_linear_range": [10**(-1.5/20), 10**(1.5/20)],
              "variant_selection": "equal weights, random without replacement",
              "source_format": "48kHz PCM24 Mono; editor import may convert internal sample representation"}
    # output_path 为可重生成的导入报告，不参与运行时打包与资产序列化。
    output_path = PROJECT_ROOT / "Saved/Audio/weapon_audio_import.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    unreal.log("WEAPON_AUDIO_IMPORT_SUCCESS " + json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()
