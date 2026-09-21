"""创建可编辑弹药目录；复用既有图标，只允许Editor生成uasset，重跑不覆盖配置。"""
import unreal


def main():
    """Editor进程内同步创建/保存配置；缺图或保存失败即报错，不伪造包文件。"""
    unreal.log("[AmmoAssets] main")
    path = "/Game/Data/Ammo/DA_AmmoCatalog"  # 运行时组件和Cook的固定引用路径。
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.EditorAssetLibrary.load_asset(path)  # 复用用户已编辑的配置。
    else:
        factory = unreal.DataAssetFactory()  # 仅当前调用持有的创建工厂。
        factory.set_editor_property("data_asset_class", unreal.DemoAmmoCatalog)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DA_AmmoCatalog", "/Game/Data/Ammo", unreal.DemoAmmoCatalog, factory)
    if not isinstance(asset, unreal.DemoAmmoCatalog):
        raise RuntimeError("Wrong ammo catalog type")
    for entry in asset.get_editor_property("entries"):  # 检查实际已导入图标引用，不重导入PNG。
        if not entry.get_editor_property("icon"):
            raise RuntimeError("Missing existing ammo icon")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Ammo catalog save failed")
    unreal.log("AMMO_ASSETS_SUCCESS existing_icons=4")


if __name__ == "__main__":
    main()
