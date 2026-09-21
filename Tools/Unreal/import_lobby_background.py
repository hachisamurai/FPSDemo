"""导入用户提供的大厅原图；运行方式和资源约定见 Documentation/UI预览设计.md。"""
from pathlib import Path
import unreal


def main():
    """编辑器命令行入口；只更新指定背景资产，保留 PNG 原图及其他项目资源。"""
    unreal.log("[LobbyBackground] main called")
    # source 是项目内原图副本；固定相对路径让另一台电脑可重复导入，不依赖下载目录。
    source = Path(unreal.Paths.project_dir()).resolve() / "SourceAssets/UI/Lobby/T_LobbyBackground.png"
    if not source.is_file():
        raise RuntimeError(f"Lobby background source missing: {source}")
    # task 只在本次自动导入期间使用，明确目标名称，避免生成带日期/中文空格的资产路径。
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", "/Game/UI/Textures")
    task.set_editor_property("destination_name", "T_LobbyBackground")
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    # texture 为资产包拥有的 Texture2D；UI 原尺寸采样，无流送模糊，无额外有损压缩。
    texture = unreal.EditorAssetLibrary.load_asset("/Game/UI/Textures/T_LobbyBackground")
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError("Lobby background import did not produce Texture2D")
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("srgb", True)
    if not unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False):
        raise RuntimeError("Failed to save lobby background texture")
    unreal.log("LOBBY_BACKGROUND_IMPORT_SUCCESS /Game/UI/Textures/T_LobbyBackground")


main()
