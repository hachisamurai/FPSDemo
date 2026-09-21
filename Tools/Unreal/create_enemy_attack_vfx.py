"""通过Unreal Editor创建红色发光材质，供飞行物、Boss前摇和全图释放球壳使用。"""
import unreal


def main():
    """仅创建本工具拥有的材质；重复调用复用资产，保存失败抛异常，不写二进制包。"""
    unreal.log("[EnemyAttackVFX] main")
    # path是稳定Cooker硬引用目标；material归Editor包持有，不跨进程保存Python对象。
    path = "/Game/VFX/EnemyAttacks/M_EnemyGlow"
    material = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if material and not isinstance(material, unreal.Material):
        raise TypeError(f"Wrong asset type: {path}")
    if not material:
        unreal.EditorAssetLibrary.make_directory("/Game/VFX/EnemyAttacks")
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_EnemyGlow", "/Game/VFX/EnemyAttacks", unreal.Material, unreal.MaterialFactoryNew())
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        material.set_editor_property("two_sided", True)
        # color为红色HDR发光，alpha不充当材质透明度；sphere壳轮廓由Fresnel控制。
        color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
        color.set_editor_property("constant", unreal.LinearColor(6.0, 0.025, 0.01, 1.0))
        # fresnel使正面透明、球体边缘发亮，避免大球壳把玩家屏幕完全染红。
        fresnel = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionFresnel, -400, 200)
        fresnel.set_editor_property("exponent", 3.0)
        fresnel.set_editor_property("base_reflect_fraction", 0.04)
        unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_property(fresnel, "", unreal.MaterialProperty.MP_OPACITY)
        unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Cannot save {path}")
    unreal.log("ENEMY_ATTACK_VFX_SUCCESS")


if __name__ == "__main__":
    main()
