"""用Editor新增Hell/Endless配置，保留现有三难度、敌人和关卡资产中的用户调参。"""
import json
from pathlib import Path
import unreal


def main():
    """Editor入口；读现有难度表，仅追加缺失Hell行，已有行和全新Endless表均不重置。"""
    unreal.log("[ChallengeTables] main called")
    root = Path(unreal.Paths.project_dir()).resolve()  # 当前工程，JSON默认值来源；不扫描其他目录。
    table = unreal.EditorAssetLibrary.load_asset("/Game/Data/DT_Difficulties")  # Editor拥有的现有难度资产。
    if not table:
        raise RuntimeError("Missing DT_Difficulties; cannot migrate without existing values")
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))  # 值副本保留全部原字段。
    if not any(row["Name"] == "Hell" for row in rows):  # 同步生成器不捕获UObject，不跨生命周期。
        defaults = json.loads((root / "Config/Combat/Difficulties.json").read_text(encoding="utf-8"))  # 新增行默认配置。
        rows.append(next(row for row in defaults if row["Name"] == "Hell"))
        if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)):
            raise RuntimeError("Difficulty migration failed")
        if not unreal.EditorAssetLibrary.save_loaded_asset(table):
            raise RuntimeError("Difficulty save failed")
    path = "/Game/Data/DT_Endless"  # 只创建缺失的新表，不覆盖用户已修改的成长倍率。
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        struct = unreal.load_object(None, "/Script/FPSDemo.DemoEndlessRow")  # 新原生行需先完整编译Editor。
        factory = unreal.DataTableFactory()  # 工厂短期拥有，表资产由Editor管理序列化。
        factory.set_editor_property("struct", struct)
        endless = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DT_Endless", "/Game/Data", unreal.DataTable, factory)
        if not endless or not unreal.DataTableFunctionLibrary.fill_data_table_from_json_file(endless, str(root / "Config/Combat/Endless.json"), struct):
            raise RuntimeError("Endless import failed")
        if not unreal.EditorAssetLibrary.save_loaded_asset(endless):
            raise RuntimeError("Endless save failed")
    unreal.log("CHALLENGE_TABLES_SUCCESS: existing combat tuning preserved")


main()
