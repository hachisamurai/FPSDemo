"""只读导出左手重设计后的真实UE动作，保留此前FinalUE枪托修正快照。"""
import importlib.util
from pathlib import Path
import unreal

# 本入口只覆盖LeftContactUE专属快照；基础导出器对所有读取的Content包进行前后哈希核验。
ROOT=Path(__file__).resolve().parents[2]
SPEC=importlib.util.spec_from_file_location('left_contact_export',ROOT/'Tools/Unreal/export_fp_reload_workbench.py')
EXPORT=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT)
unreal.log('[CALL] ExportLeftContactWorkbench')
EXPORT.OUTPUT=ROOT/'Art/Player/Animations/LeftContactUE'
# 追加15个真实指骨，后续可验证转腕和张合不只存在于Blender候选。
EXPORT.SAMPLE_BONES=EXPORT.SAMPLE_BONES+tuple(f'{finger}_{joint:02d}_l' for finger in ('index','middle','ring','pinky','thumb') for joint in (1,2,3))
EXPORT.main()
(EXPORT.OUTPUT/'README.md').write_text('# 左手接触重设计 UE 快照\n\n由 export_fp_left_contact_workbench.py 只读导出。12套真实UE动作含原始手臂网格和全部左手指骨的采样数据。\n这是最新动作输入；是否通过全枪实体检查请查看相邻 left_hand_final_ue_diagnostic.json，不能沿用此前仅枪托的通过结论。\n',encoding='utf-8')
