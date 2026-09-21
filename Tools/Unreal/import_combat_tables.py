"""将 Config/Combat JSON 写入原生 DataTable；字段与维护规则见 Documentation/战役配置与难度.md。"""
import json
from pathlib import Path
import unreal


def main():
    """Editor/Commandlet 入口；校验源文件和行类型后原位更新资产，失败抛错保留诊断。"""
    unreal.log("[CombatImport] main called")
    # root 是当前项目绝对路径；tools 借用编辑器资产服务，不缓存跨进程对象。
    root = Path(unreal.Paths.project_dir()).resolve()
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    # entries 明确限定本工具拥有的三项资产，绝不遍历删除其他 Content/Data 内容。
    entries = (("Enemies", "DemoEnemyRow", None), ("Difficulties", "DemoDifficultyRow", 3), ("Levels", "DemoLevelRow", 10))
    for source, row_type, count in entries:  # 源文件基名、USTRUCT 名、固定行数；None 允许扩展怪物模板。
        unreal.log(f"[CombatImport] import {source}, struct={row_type}")
        # source_path/payload 为此次导入的 UTF-8 源文件及解析行；失败发生在更新资产前。
        source_path = root / "Config" / "Combat" / f"{source}.json"
        payload = json.loads(source_path.read_text(encoding="utf-8"))
        if not payload or (count is not None and len(payload) != count) or len({row["Name"] for row in payload}) != len(payload):
            raise RuntimeError(f"Invalid rows in {source_path}")
        # struct 是运行模块 UHT 注册类型；工厂只在资产不存在时创建一次。
        struct = unreal.load_object(None, f"/Script/FPSDemo.{row_type}")
        if not struct:
            raise RuntimeError(f"Build FPSDemoEditor first: missing {row_type}")
        # asset_path/table 只引用当前受管理资产；每次保存的资产均有完整原生行结构。
        asset_path = f"/Game/Data/DT_{source}"
        table = unreal.EditorAssetLibrary.load_asset(asset_path) if unreal.EditorAssetLibrary.does_asset_exist(asset_path) else None
        if table and table.get_editor_property("row_struct") != struct:
            raise RuntimeError(f"Refusing to replace wrong row type: {asset_path}")
        if not table:
            factory = unreal.DataTableFactory()  # 临时工厂，资产由编辑器包持有。
            factory.set_editor_property("struct", struct)
            table = tools.create_asset(f"DT_{source}", "/Game/Data", unreal.DataTable, factory)
        if not table or not unreal.DataTableFunctionLibrary.fill_data_table_from_json_file(table, str(source_path), struct):
            raise RuntimeError(f"DataTable import failed: {asset_path}")
        if len(unreal.DataTableFunctionLibrary.get_data_table_row_names(table)) != len(payload):
            raise RuntimeError(f"Row count mismatch: {asset_path}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False):
            raise RuntimeError(f"Saving failed: {asset_path}")
        unreal.log(f"[CombatImport] saved {asset_path} rows={len(payload)}")
    unreal.log("COMBAT_IMPORT_SUCCESS")


main()
