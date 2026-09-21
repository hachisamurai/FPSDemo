"""UE5.4 Editor资产初始化：武器BP和Enhanced Input；已有同名资产保留用户配置。"""
import unreal


def create_data_asset(name, asset_class):
    """name为工具拥有的资产名，asset_class为InputAction/Context；返回资产与是否新建。"""
    unreal.log(f"[WeaponAssets] create_data_asset {name}")
    # path为稳定运行时引用；asset由Editor包持有，不缓存跨进程对象。
    path = f"/Game/Weapons/Input/{name}"
    asset = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset:
        if not isinstance(asset, asset_class):
            raise TypeError(f"Asset class mismatch: {path}")
        return asset, False
    # 两种输入类型均继承DataAsset，使用引擎标准工厂生成真实包。
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Weapons/Input", asset_class, factory)
    if not asset:
        raise RuntimeError(f"Creation failed: {path}")
    return asset, True


def save(asset):
    """asset为当前Editor持有的UObject；写包失败立即终止，不自行生成uasset字节。"""
    unreal.log(f"[WeaponAssets] save {asset.get_path_name()}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Save failed: {asset.get_path_name()}")


def create_weapon(name, parent_class, overrides):
    """name/parent_class定义派生BP，overrides仅应用于首次创建，避免重跑覆盖美术/数值配置。"""
    unreal.log(f"[WeaponAssets] create_weapon {name}")
    # path是装备组件软类引用的目标；bp通过Editor生成，不处理原始二进制格式。
    path = f"/Game/Weapons/Blueprints/{name}"
    bp = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if bp:
        # UE5.4没有暴露Blueprint.ParentClass，检查生成类CDO的原生继承关系。
        if not isinstance(bp, unreal.Blueprint) or not isinstance(unreal.get_default_object(bp.generated_class()), parent_class):
            raise TypeError(f"Blueprint parent mismatch: {path}")
    else:
        # factory为本次创建的临时工厂；生成类的CDO保存Config供所有新实例复制。
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)
        bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, "/Game/Weapons/Blueprints", unreal.Blueprint, factory)
        if not bp:
            raise RuntimeError(f"Blueprint creation failed: {path}")
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        # cdo/config为当次类默认对象及结构体副本；写回整个结构体以通知Editor属性变更。
        cdo = unreal.get_default_object(bp.generated_class())
        config = cdo.get_editor_property("config")
        for field, value in overrides.items():  # field/value为首次默认值，单位/范围见FDemoWeaponConfig。
            config.set_editor_property(field, value)
        cdo.set_editor_property("config", config)
    save(bp)
    # 保存后读取配置日志；进程外运行测试再次加载，验证默认值真正持久化。
    config = unreal.get_default_object(bp.generated_class()).get_editor_property("config")
    unreal.log(f"WEAPON_ASSET {name} rpm={config.rounds_per_minute} capacity={config.magazine_capacity} scope={config.supports_scope}")


def main():
    """生成分类资产；既有资产仅校验并保存，不重置用户编辑的绑定或Config。"""
    unreal.log("[WeaponAssets] main")
    unreal.EditorAssetLibrary.make_directory("/Game/Weapons/Input")
    unreal.EditorAssetLibrary.make_directory("/Game/Weapons/Blueprints")
    # bindings为首次初始化的真实键名；使用Started，不在资产添加Hold等延迟触发器。
    bindings = (("Fire", "LeftMouseButton"), ("Reload", "R"), ("Aim", "RightMouseButton"),
                ("EquipPrimary", "One"), ("EquipSecondary", "Two"), ("CyclePrimary", "B"))
    # context/created用于区分首次生成与已有自定义映射。
    context, created = create_data_asset("IMC_Combat", unreal.InputMappingContext)
    for action_name, key_name in bindings:  # 每个Action独立资产，方便后续重绑定。
        action, new_action = create_data_asset(f"IA_{action_name}", unreal.InputAction)
        if new_action:
            action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
        save(action)
        if created:
            # FKey在UE5.4没有带参Python构造器，通过反射设置其Name字段。
            key = unreal.Key()
            key.set_editor_property("key_name", key_name)
            context.map_key(action, key)
    save(context)
    create_weapon("BP_Weapon_Rifle", unreal.DemoHitscanWeapon, {})
    create_weapon("BP_Weapon_Pistol", unreal.DemoHitscanWeapon, {
        "display_name": unreal.Text("手枪"), "fire_mode": unreal.DemoFireMode.SEMI_AUTOMATIC,
        "rounds_per_minute": 240.0, "base_damage": 20.0, "reload_seconds": 1.2,
    })
    create_weapon("BP_Weapon_Shotgun", unreal.DemoShotgunWeapon, {})
    create_weapon("BP_Weapon_Sniper", unreal.DemoSniperWeapon, {})
    unreal.log("WEAPON_ASSETS_SUCCESS")


if __name__ == "__main__":
    main()
