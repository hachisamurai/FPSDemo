"""独立复核UE重新导出的左手动作：全枪真实表面、原蒙皮、双臂枪托、实际转腕和指骨变化。"""
import bpy
import json
import sys
import math
from pathlib import Path
from mathutils import Matrix

# 只更新工作台、诊断和预览；不写Content，不重新求解UE导出的动作。
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Art/Player/Animations'
sys.path.insert(0,str(Path(__file__).parent))
import author_fp_left_hand_contacts as CONTACT
import import_final_fp_workbench as FINAL
import validate_fp_reload_skin as STOCK
WB=CONTACT.WB


@WB.traced
def main():
    """逐帧应用真实UE动作并验证；通过与失败均完整保存，不能用候选报告替代。"""
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'Breach_FP_LeftHand_Contacts.blend'))
    manifest=json.loads((OUT/'workbench_manifest.json').read_text(encoding='utf-8'))
    reference=json.loads((OUT/'Reference/arms_reference.json').read_text(encoding='utf-8'))
    source=json.loads((OUT/'LeftContactUE/current_ue_workbench_manifest.json').read_text(encoding='utf-8'))
    groups={model:bpy.data.collections[f'FP_{model}'] for model in WB.MODELS}
    records,stock_records,checks,actions=[],[],[],{}
    for model in WB.MODELS:
        WB.show_group(groups,model)
        ctx=CONTACT.context(model,manifest,reference)
        CONTACT.load_mechanical(ctx)  # 按最新机械源显式加载两种动作，避免工作台当前播放状态影响资源保活。
        trees=CONTACT.gun_trees(ctx)
        for entry in (row for row in source['animations'] if row['model']==model):
            variant=entry['variant']
            action,check=FINAL.import_verified_action(entry,'LeftContactUE','LeftContactUE')
            checks.append(check)
            actions[(model,variant)]=action
            intervals=round(entry['seconds']*60)  # UE FBX尾部多一帧保持；真实片长来自UE清单，不能用额外尾帧缩放机械时间。
            phases=[0] if variant=='Idle' else sorted(set([frame/intervals for frame in range(intervals+1)]+[row['phase'] for row in entry['samples']]))
            for phase in phases:
                WB.assign_action(ctx['arm'],action)
                WB.assign_action(ctx['gun'],None if variant=='Idle' else bpy.data.actions[f'A_Breach_{model}_Reload_{variant}'])
                if variant=='Idle':
                    for bone in ctx['gun'].pose.bones:
                        bone.matrix_basis=Matrix.Identity(4)
                sample_frame=float(action.frame_range[0])+phase*intervals  # 同一秒同时求值手臂和机械，非各自归一化FBX范围。
                WB.set_frame(sample_frame)
                hits=CONTACT.intersections(ctx,trees)
                records.append({'model':model,'variant':variant,'phase':phase,'hits':hits})
                # 相同新动作也复核两条手臂与枪托，防止左手修正引入右臂回归。
                if model!='Pistol':
                    stock_records.append(STOCK.evaluate_pose(model,variant,phase,'LeftContactUE',sample_frame))
            for phase in ([0] if variant=='Idle' else [.10,.17,.32,.60,.74,.92]):
                WB.assign_action(ctx['arm'],action)
                WB.assign_action(ctx['gun'],None if variant=='Idle' else bpy.data.actions[f'A_Breach_{model}_Reload_{variant}'])
                WB.set_frame(float(action.frame_range[0])+phase*intervals)
                CONTACT.preview(ctx,f'UE_{variant}_{round(phase*100):02d}')
    report={'scope':'Actual UE FBX, all left skin vertices vs all rigid gun triangle surfaces; both arms vs stock; not continuous triangle CCD',
            'contact_noise_cm':.02,'source_unchanged':source['source_content_unchanged'],'reference_checks':checks,
            'sample_count':len(records),'failed_samples':sum(bool(row['hits']) for row in records),
            'max_depth_cm':max((hit['max_depth_cm'] for row in records for hit in row['hits']),default=0),
            'stock_sample_count':len(stock_records),'stock_failed_samples':sum(bool(row['penetrating_vertices']) for row in stock_records),
            'samples':records,'stock_samples':stock_records}
    (OUT/'left_hand_final_ue_diagnostic.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    # 打开工作台默认显示最终UE步枪换弹握匣阶段，便于用户直接检查转腕/指骨。
    WB.show_group(groups,'Rifle')
    WB.assign_action(bpy.data.objects['Arms_Rifle'],actions[('Rifle','Tactical')])
    WB.assign_action(bpy.data.objects['Gun_Rifle'],bpy.data.actions['A_Breach_Rifle_Reload_Tactical'])
    WB.set_frame(float(actions[('Rifle','Tactical')].frame_range[0])+.32*round(1.4*60))
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Breach_FP_LeftHand_Contacts.blend'))
    # 页面只使用UE_前缀结果，不把作者候选照片展示为UE验收结果。
    page=['<!doctype html><meta charset="utf-8"><title>左手换弹接触修正</title><style>body{background:#14212b;color:#dbe8ec;font:16px system-ui;margin:24px}main{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}img{width:100%}figure{margin:0}h2{color:#82ddd5}</style><h1>左手换弹接触修正 · UE 导出动作</h1><p>左侧接触与第一人称几何预览；Blender诊断材质。实际游戏画面见换弹视频。</p>']
    for model in WB.MODELS:
        page.append(f'<h2>{model}</h2><main>')
        for phase,label in ((10,'张手转腕'),(17,'握匣'),(32,'抽出'),(60,'插回'),(74,'松手'),(92,'回握')):
            file=f'{model}_UE_Tactical_{phase:02d}_Contact.png'
            page.append(f'<figure><a href="{file}"><img src="{file}"></a><figcaption>{label}</figcaption></figure>')
        page.append('</main>')
    (OUT/'LeftHandPreviews/index.html').write_text('\n'.join(page),encoding='utf-8')
    print(f'LEFT_CONTACT_FINAL_UE_COMPLETE samples={len(records)} failed={report["failed_samples"]} stock_failed={report["stock_failed_samples"]}',flush=True)


if __name__=='__main__':
    main()
