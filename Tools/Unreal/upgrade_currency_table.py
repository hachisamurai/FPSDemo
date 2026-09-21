"""兼容历史命令：双币表迁移已由整轮通关经济迁移工具接管，不再创建逐关金币字段。"""
from pathlib import Path
import runpy
import unreal


def main():
    """显式Editor入口，转到当前经济表迁移脚本；保留旧命令避免维护者误执行废弃字段导入。"""
    unreal.log('[CurrencyTable] main called; forwarding to upgrade_economy_tables.py')
    # script只定位本项目工具目录；新脚本同步执行并由Editor保存资产，不创建二进制伪资产。
    script = Path(unreal.Paths.project_dir()).resolve() / 'Tools' / 'Unreal' / 'upgrade_economy_tables.py'
    runpy.run_path(str(script), run_name='__main__')


main()
