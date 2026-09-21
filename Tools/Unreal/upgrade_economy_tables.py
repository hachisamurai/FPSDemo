"""Editor迁移难度通关金币配置，保留实际资产中已有战斗参数；禁止手写uasset。"""
import json
import unreal


def main():
    """显式Editor入口：加载现有表，只补缺失奖励并移除废弃逐关金币字段，失败抛出异常。"""
    unreal.log('[EconomyTable] main called')
    # defaults按固定难度键定义首次迁移值；后续重跑保留策划已经编辑的非负奖励。
    defaults = {'Easy': 500, 'Normal': 750, 'Hard': 1000}
    for name, row_type in (('Difficulties', 'DemoDifficultyRow'), ('Levels', 'DemoLevelRow')):  # 只处理经济相关的两张既有表。
        table = unreal.EditorAssetLibrary.load_asset(f'/Game/Data/DT_{name}')  # Editor拥有的资产，同步借用。
        struct = unreal.load_object(None, f'/Script/FPSDemo.{row_type}')  # 编译后的原生反射结构。
        if not table or not struct or table.get_editor_property('row_struct') != struct:
            raise RuntimeError(f'Build Editor first; invalid {name} table/struct')
        rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))  # 保留实际表值，不覆盖用户调参。
        if name == 'Difficulties':
            if {row['Name'] for row in rows} != set(defaults):
                raise RuntimeError('Expected Easy/Normal/Hard rows')
            for row in rows:  # row只引用当前导出的字典，先完整校验再写入资产。
                if row.get('VictoryGoldReward', -1) == -1:
                    row['VictoryGoldReward'] = defaults[row['Name']]
                if type(row['VictoryGoldReward']) is not int or not 0 <= row['VictoryGoldReward'] <= 1000000:
                    raise RuntimeError(f"Invalid reward in {row['Name']}")
        else:
            for row in rows:  # 删除旧导出可能残留的逐关字段，运行时不再读取。
                row.pop('ClearGoldReward', None)
        if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, json.dumps(rows), struct):
            raise RuntimeError(f'Failed refill {name}')
        if not unreal.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False):
            raise RuntimeError(f'Failed save {name}')
    unreal.log('ECONOMY_TABLE_UPGRADE_SUCCESS: difficulty victory gold; per-stage gold removed')


main()
