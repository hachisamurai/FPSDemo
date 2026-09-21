"""Editor创建独立蓝色俯冲材质；字段与玩法见Documentation/Boss升空俯冲.md。"""
import unreal


def main():
    """仅管理本工具的单个材质；重复运行复用，类型错误拒绝覆盖。"""
    unreal.log("[BossDiveVFX] main")
    # path是运行时硬引用的资产路径；asset仅本次Editor调用借用。
    path = "/Game/VFX/EnemyAttacks/Dive/M_BossDiveBlue"
    asset = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if asset and not isinstance(asset, unreal.Material):
        raise TypeError(f"Wrong asset type: {path}")
    if not asset:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_BossDiveBlue", "/Game/VFX/EnemyAttacks/Dive", unreal.Material, unreal.MaterialFactoryNew())
        asset.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
        asset.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        asset.set_editor_property("two_sided", True)
        # color为HDR蓝色；edge让球壳中心透明，避免蓄力完全遮挡Boss。
        color = unreal.MaterialEditingLibrary.create_material_expression(asset, unreal.MaterialExpressionConstant3Vector, -400, 0)
        color.set_editor_property("constant", unreal.LinearColor(0.025, 1.2, 6.0, 1.0))
        edge = unreal.MaterialEditingLibrary.create_material_expression(asset, unreal.MaterialExpressionFresnel, -400, 200)
        edge.set_editor_property("exponent", 3.0)
        edge.set_editor_property("base_reflect_fraction", 0.02)
        unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        unreal.MaterialEditingLibrary.connect_material_property(edge, "", unreal.MaterialProperty.MP_OPACITY)
        unreal.MaterialEditingLibrary.recompile_material(asset)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Save failed: {path}")
    unreal.log("BOSS_DIVE_VFX_SUCCESS")


main()
