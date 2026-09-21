"""只调整Boss出现关卡；保留既有怪数、难度、攻击/成长数值，由Editor保存真实DataTable资产。"""
import json
from pathlib import Path
import unreal


def main():
    """Editor命令入口；十关战役仅Level10留Boss，不对其他表做全量重导入。"""
    unreal.log("[EncounterTables] main called")
    root = Path(unreal.Paths.project_dir()).resolve()  # 当前项目用于写出迁移前诊断快照，不读取其他项目。
    table = unreal.EditorAssetLibrary.load_asset("/Game/Data/DT_Levels")  # 既有Editor资产，禁止直接写二进制uasset。
    if not table:
        raise RuntimeError("Missing DT_Levels; cannot migrate encounter cadence")
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))  # 包含全部用户字段的独立值副本。
    backup = root / "Saved/Tests/EncounterLevelsBefore.json"  # 首次迁移原值，重跑不能覆盖初始诊断备份。
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        backup.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding="utf-8")
    if {row["Name"] for row in rows} != {f"Level{number:02d}" for number in range(1, 11)}:
        raise RuntimeError("Expected exactly Level01..Level10; original asset unchanged")
    for row in rows:  # 同步修改值副本，仅BossRow允许变更，其他参数保留。
        number = int(row["Name"][5:])  # 合法固定行名提取关号，不依赖DataTable内部排序。
        if number == 10 and row["BossRow"] in ("", "None"):
            raise RuntimeError("Level10 requires an authored BossRow; original asset unchanged")
        if number % 5 == 0 and row["EnemyCount"] < 3:
            raise RuntimeError("Every fifth level needs at least 3 minions; original asset unchanged")
        if number != 10:
            row["BossRow"] = "None"
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows)):
        raise RuntimeError("Encounter table migration failed")
    if not unreal.EditorAssetLibrary.save_loaded_asset(table):
        raise RuntimeError("Encounter table save failed")
    actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))  # Editor导回确认实际写入内容与预期完全一致。
    if actual != rows:
        raise RuntimeError("Encounter table roundtrip differs from requested values")
    unreal.log("ENCOUNTER_TABLES_SUCCESS: Boss only on Level10; existing tuning preserved")


main()
