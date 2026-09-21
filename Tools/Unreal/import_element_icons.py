"""导入六张透明状态/弹药图标及两个世界空间材质；使用与验证见 Documentation/元素图标.md。"""
from pathlib import Path
import unreal


# 项目内源图目录，不依赖生成工具的个人缓存；本进程仅读取 PNG。
SOURCE_DIR = Path(unreal.Paths.project_dir()).resolve() / "SourceAssets/UI/ElementIcons"
# 新资源独立目录，避免覆盖现有武器 Icon 与大厅背景。
TEXTURE_DIR = "/Game/UI/Textures/ElementIcons"
MATERIAL_DIR = "/Game/UI/Materials/ElementIcons"
# 固定六种语义；新增普通弹作为无元素效果的基础图标，顺序只用于导入，不驱动技能数值。
ICON_NAMES = ("T_Status_Fire", "T_Status_Ice", "T_Ammo_Normal", "T_Ammo_Fire", "T_Ammo_Ice", "T_Ammo_Piercing")


def import_icon(name):
    """name 为固定白名单中的纹理名；创建新资产或复用同类型旧资产，返回编辑器持有的 Texture2D。"""
    unreal.log(f"[ElementIcons] import_icon called name={name}")
    # source/path 分别为只读源文件与包路径；重复执行不覆盖已存在纹理的图像内容。
    source = SOURCE_DIR / f"{name}.png"
    path = f"{TEXTURE_DIR}/{name}"
    if not source.is_file():
        raise RuntimeError(f"Missing icon source: {source}")
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        # task 只在同步导入期间存活；禁止自动替换其他人已经创建的同名资产。
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", TEXTURE_DIR)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", False)
        task.set_editor_property("save", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    else:
        unreal.log(f"[ElementIcons] reuse existing texture; source pixels unchanged: {path}")
    # texture 归包所有；保留 RGBA 和原始非二次幂尺寸，避免自动裁掉透明边缘。
    texture = unreal.EditorAssetLibrary.load_asset(path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Expected Texture2D: {path}")
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("srgb", True)
    # Alpha 必须保留；边缘钳制避免世界平面 UV 临界点采到另一侧图形。
    texture.set_editor_property("compression_no_alpha", False)
    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    if not unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save texture: {path}")
    unreal.log(f"[ElementIcons] texture ready: {path}")
    return texture


def create_world_material(name, texture):
    """name 为新材质名，texture 为对应状态纹理；提供世界空间双面透明图像，不负责朝向/状态判定。"""
    unreal.log(f"[ElementIcons] create_world_material called name={name}")
    # path/material 为当前编辑器包；已有材质只验证类型，不重建其节点以免覆盖人工调色。
    path = f"{MATERIAL_DIR}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        material = unreal.EditorAssetLibrary.load_asset(path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f"Expected Material: {path}")
        unreal.log(f"[ElementIcons] existing material preserved: {path}")
        return material
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError(f"Failed to create material: {path}")
    # Unlit 保持状态颜色不受场景光照影响；透明混合保留抗锯齿，默认深度测试让墙体正常遮挡。
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    # sample 是材质拥有的纹理参数节点；RGB 不放大以避免过曝，Alpha 直接连接不透明度。
    sample = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    sample.set_editor_property("parameter_name", "IconTexture")
    sample.set_editor_property("texture", texture)
    if not unreal.MaterialEditingLibrary.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError(f"Cannot connect icon color: {path}")
    if not unreal.MaterialEditingLibrary.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError(f"Cannot connect icon alpha: {path}")
    unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save material: {path}")
    unreal.log(f"[ElementIcons] world material ready: {path}")
    return material


def main():
    """显式编辑器脚本入口：创建六张纹理、两个材质；不创建 GI、联网、修改关卡或虚构元素 Gameplay。"""
    unreal.log("[ElementIcons] main called")
    unreal.EditorAssetLibrary.make_directory(TEXTURE_DIR)
    unreal.EditorAssetLibrary.make_directory(MATERIAL_DIR)
    # name/texture 仅本轮借用；前两种额外生成世界材质，四种弹药图标直接供 HUD/UMG 使用。
    for name in ICON_NAMES:
        texture = import_icon(name)
        if name.startswith("T_Status_"):
            create_world_material(name.replace("T_", "M_") + "_World", texture)
    # 用白名单长度记录数量，后续新增图标时不会留下误导的成功统计。
    unreal.log(f"ELEMENT_ICONS_IMPORT_SUCCESS textures={len(ICON_NAMES)} world_materials=2")


if __name__ == "__main__":
    main()
