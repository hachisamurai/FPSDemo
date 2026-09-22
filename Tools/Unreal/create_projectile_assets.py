"""通过UE5.4 Editor创建子弹BP和发光拖迹材质；重跑只补空引用，保留已有美术/数值。"""
import unreal


def save_asset(asset):
    """asset为本Editor持有的UObject；由Editor写包，失败立即终止。"""
    unreal.log(f"[ProjectileAssets] save_asset {asset.get_path_name()}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Save failed: {asset.get_path_name()}")


def create_tracer_material():
    """Editor生成无光照加法材质；TracerOpacity由独立表现Actor淡出，不改变碰撞寿命。"""
    unreal.log("[ProjectileAssets] create_tracer_material")
    # path/folder固定在子弹Materials分类；已有美术材质不会被重建或覆盖。
    folder = "/Game/Weapons/Projectiles/Materials"
    path = f"{folder}/M_Projectile_Tracer"
    material = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None  # Editor持有资产。
    if material:
        if not isinstance(material, unreal.Material):
            raise TypeError(f"Tracer material type mismatch: {path}")
        return material
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Projectile_Tracer", folder, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError(f"Tracer material creation failed: {path}")
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", True)
    # color是可由材质实例覆盖的线性HDR颜色；亮度帮助2cm高速实体的路径在暗/亮背景被识别。
    color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property("parameter_name", "TracerColor")
    color.set_editor_property("default_value", unreal.LinearColor(12.0, 3.0, 0.2, 1.0))
    # opacity必须保留此参数名；每颗表现Actor私有MID在停弹后短暂线性淡出，不修改共享材质。
    opacity = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -400, 200)
    opacity.set_editor_property("parameter_name", "TracerOpacity")
    opacity.set_editor_property("default_value", 1.0)
    if not unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Failed to connect tracer emissive color")
    if not unreal.MaterialEditingLibrary.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("Failed to connect tracer opacity")
    unreal.MaterialEditingLibrary.recompile_material(material)
    save_asset(material)
    return material


def configure_tracer(bp, material):
    """bp为项目子弹蓝图；只补空的拖迹材质，保留用户Mesh、速度、尺寸及显式关闭表现的选择。"""
    unreal.log(f"[ProjectileAssets] configure_tracer {bp.get_path_name()}")
    cdo = unreal.get_default_object(bp.generated_class())  # 借用默认对象；完整Config读改写只改变一个引用。
    config = cdo.get_editor_property("config")  # 含运动和碰撞数据，禁止用全新默认结构替换。
    if config.get_editor_property("tracer_material"):
        unreal.log(f"PROJECTILE_TRACER_PRESERVED {bp.get_path_name()}")
        return
    config.set_editor_property("tracer_material", material)
    cdo.set_editor_property("config", config)
    save_asset(bp)
    unreal.log(f"PROJECTILE_TRACER_BOUND {bp.get_path_name()} material={material.get_path_name()}")


def create_projectile(name, speed):
    """name是BP文件名；speed为首次初始化cm/s，已有BP的完整Config一律保留。"""
    unreal.log(f"[ProjectileAssets] create_projectile {name}")
    # folder/path是运行时硬引用的稳定包路径，不操作uasset二进制格式。
    folder = "/Game/Weapons/Projectiles/Blueprints"
    path = f"{folder}/{name}"
    # bp由Editor资产系统持有；只对尚不存在的包应用建议默认速度。
    bp = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if bp:
        if not isinstance(bp, unreal.Blueprint) or not isinstance(unreal.get_default_object(bp.generated_class()), unreal.DemoProjectileBase):
            raise TypeError(f"Projectile blueprint parent mismatch: {path}")
    else:
        # factory为短期创建工具，生成类CDO接收原生无碰撞Mesh和基础运动默认值。
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.DemoProjectileBase)
        bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Blueprint, factory)
        if not bp:
            raise RuntimeError(f"Projectile creation failed: {path}")
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        # cdo/config为类默认配置；首次只改速度，不复制武器伤害或弹药元素参数。
        cdo = unreal.get_default_object(bp.generated_class())
        config = cdo.get_editor_property("config")
        config.set_editor_property("initial_speed", speed)
        cdo.set_editor_property("config", config)
        save_asset(bp)
    unreal.log(f"PROJECTILE_ASSET {path} speed={unreal.get_default_object(bp.generated_class()).get_editor_property('config').initial_speed}")
    return bp


def bind_weapon(weapon_name, projectile):
    """weapon_name是已存在武器BP；projectile为本次Editor生成/校验的子弹BP，只补默认关联。"""
    unreal.log(f"[ProjectileAssets] bind_weapon {weapon_name}")
    # path/bp必须指向现有武器，禁止为了绑定子弹重建武器或覆盖用户美术。
    path = f"/Game/Weapons/Blueprints/{weapon_name}"
    bp = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(bp, unreal.Blueprint) or not isinstance(unreal.get_default_object(bp.generated_class()), unreal.DemoWeaponBase):
        raise TypeError(f"Missing or incompatible weapon blueprint: {path}")
    # cdo/config借用现有全量结构；唯一可修改字段是projectile_class。
    cdo = unreal.get_default_object(bp.generated_class())
    config = cdo.get_editor_property("config")
    current = config.get_editor_property("projectile_class")  # 已有用户自定义子弹类优先保留。
    if current and current != unreal.DemoProjectileBase.static_class():
        unreal.log(f"PROJECTILE_BIND_PRESERVED {path} class={current.get_path_name()}")
        return
    config.set_editor_property("projectile_class", projectile.generated_class())
    cdo.set_editor_property("config", config)
    save_asset(bp)
    unreal.log(f"PROJECTILE_BOUND {path} class={projectile.generated_class().get_path_name()}")


def main():
    """创建分类目录、发光材质与四个数据BP；仅Editor执行，重复运行只补尚未配置的引用。"""
    unreal.log("[ProjectileAssets] main")
    for folder in ("Blueprints", "Meshes", "Materials", "VFX"):  # 分类目录预留给后续美术，不生成占位二进制资源。
        unreal.EditorAssetLibrary.make_directory(f"/Game/Weapons/Projectiles/{folder}")
    tracer_material = create_tracer_material()  # Editor创建的真实材质，由四个子弹CDO硬引用供Cook收集。
    # definitions分别为武器名、子弹BP名和首版建议速度；武器伤害/弹匣/射速不在此配置。
    definitions = (("Pistol", "Pistol", 18000.0), ("Rifle", "Rifle", 30000.0),
                   ("Shotgun", "ShotgunPellet", 16000.0), ("Sniper", "Sniper", 60000.0))
    for weapon, projectile_name, speed in definitions:  # 每项独立保存，可安全重跑补齐上次失败后的缺项。
        projectile = create_projectile(f"BP_Projectile_{projectile_name}", speed)
        configure_tracer(projectile, tracer_material)
        bind_weapon(f"BP_Weapon_{weapon}", projectile)
    unreal.log("PROJECTILE_ASSETS_SUCCESS")


if __name__ == "__main__":
    main()
