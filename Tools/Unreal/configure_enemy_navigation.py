"""UE Editor制作三战斗区导航；只更新专用Bounds体积，不重导入或重建用户白模。"""
import unreal

# 固定目标地图，避免从当前编辑器选择推断并覆盖其他关卡。
MAP_PATH = "/Game/Whitebox/Maps/L_ThreeSector_Whitebox"


def main():
    """加载既有地图，原生桥接构建校验后由Editor保存；失败保留磁盘原地图。"""
    unreal.log("[CALL] configure_enemy_navigation.main")
    # Editor持有子系统与World，此脚本同步运行，不捕获跨帧对象。
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.load_level(MAP_PATH):
        raise RuntimeError("Navigation map could not be loaded")
    # 桥接使用真正BrushBuilder和Recast构建，而非直接写入资产二进制。
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not unreal.DemoNavigationAssetLibrary.configure_navigation(world):
        raise RuntimeError("Navigation build/validation failed; map not saved")
    if not levels.save_current_level():
        raise RuntimeError("Navigation map save failed")
    unreal.log("ENEMY_NAVIGATION_ASSETS_SAVED")


main()
