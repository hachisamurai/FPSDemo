"""必须在新 Editor 进程运行，验证磁盘保存的 Cue 可重新加载，避免只验证内存图。"""
import unreal


def main() -> None:
    """通过原生编辑器桥接验证两份磁盘 Cue；失败即抛异常。"""
    unreal.log("[WeaponAudioValidation] main")
    # kind/path/cue 为本次加载的用途、资产标识和 Editor 持有的对象，不修改资产。
    for kind in ("Fire", "Hit"):
        path = f"/Game/Audio/SFX/Weapons/Sword/{kind}/Cues/SC_Sword_{kind}"
        cue = unreal.EditorAssetLibrary.load_asset(path)
        if not cue or not unreal.DemoAudioAssetLibrary.validate_weapon_cue(cue):
            raise RuntimeError(f"Persisted Cue invalid: {path}")
    unreal.log("WEAPON_AUDIO_RELOAD_VALIDATION_SUCCESS")


if __name__ == "__main__":
    main()
