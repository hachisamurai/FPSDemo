"""原UE蒙皮上的左手接触作者：分段转腕/收指/绕行，并导出原生烘焙用厘米轨迹。"""
import bpy
import json
import math
import sys
import hashlib
from pathlib import Path
import numpy as np
from mathutils import Matrix, Quaternion, Vector
from mathutils.bvhtree import BVHTree

# 工具只修改工作台副本和自己生成的作者文件；游戏读取烘焙Sequence，不读取JSON。
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Art/Player/Animations'
sys.path.insert(0, str(Path(__file__).parent))
import build_fp_reload_workbench as WB
from validate_fp_reload_skin import ray_inside


@WB.traced
def matrix(value):
    """value是UE参考骨变换，xyzw四元数转换为数学矩阵，不反转坐标轴。"""
    # JSON参考骨本身已在UE厘米空间；Quaternion构造顺序为wxyz。
    q = value['rotation']
    result = Quaternion((q[3], q[0], q[1], q[2])).to_matrix().to_4x4()
    result.translation = Vector(value['translation'])
    return result


@WB.traced
def smooth(value):
    """value是夹紧前区间进度，三次曲线让接触阶段首尾速度为零。"""
    value = max(0, min(1, value))
    return value * value * (3 - 2 * value)


@WB.traced
def context(model, manifest, reference):
    """读取model原UE待机；保存局部姿态与骨轴标定，供每帧无累积重建。"""
    # arm/gun只属于本Blender进程；shared原mesh与骨架数据不写入。
    arm = bpy.data.objects[f'Arms_{model}']
    gun = bpy.data.objects[f'Gun_{model}']
    WB.assign_action(arm, bpy.data.actions[f'FinalUE_{model}_Idle'])
    WB.assign_action(gun, None)
    for bone in gun.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    WB.set_frame(1)
    mapping = Matrix(next(row['ue_to_blender'] for row in manifest['models'] if row['model'] == model))
    # UE最终待机样本仅覆盖IK骨，指骨继续使用原模板local；从父骨重建其实际组件空间。
    snapshot = json.loads((OUT/'FinalUE/current_ue_workbench_manifest.json').read_text(encoding='utf-8'))
    sample = next(row for row in snapshot['animations'] if row['model']==model and row['variant']=='Idle')['samples'][0]
    ue_world = {}
    for row in reference['bones']:
        name = row['name']
        ue_world[name] = (ue_world.get(row['parent'], Matrix.Identity(4)) @ matrix(row['local']))
        if name in sample['component_bones']:
            ue_world[name] = Matrix(sample['component_bones'][name]['matrix4x4_column_vector'])
    # 每骨FBX轴修正是常量；不能直接把UE的局部X轴当Blender骨轴。
    correction = {bone.name:(mapping @ ue_world[bone.name]).inverted() @ arm.matrix_world @ bone.matrix for bone in arm.pose.bones if bone.name in ue_world}
    basis = {bone.name:bone.matrix_basis.copy() for bone in arm.pose.bones}
    arm.animation_data.action = None
    return {'model':model,'arm':arm,'gun':gun,'map':mapping,'ue':ue_world,'correction':correction,'basis':basis,
            'hand_rotation':matrix(next(row for row in reference['bones'] if row['name']=='hand_l')['component']).to_quaternion(), 'finger_axes':finger_axes(reference)}


@WB.traced
def finger_axes(reference):
    """由模板各手指的弯曲平面求局部屈曲轴；返回原UE骨局部单位轴，保持骨长。"""
    bones = {row['name']:row for row in reference['bones']}  # 同一只左手的只读参考。
    axes = {}
    for finger in ('index','middle','ring','pinky','thumb'):
        names = [f'{finger}_{index:02d}_l' for index in (1,2,3)]
        points = [Vector(bones[name]['component']['translation']) for name in names]
        normal = (points[1]-points[0]).cross(points[2]-points[1]).normalized()
        for name in names:
            axes[name] = matrix(bones[name]['component']).to_quaternion().inverted() @ normal
    return axes


@WB.traced
def solve_arm(ctx, wrist, rotation):
    """wrist/rotation为枪局部厘米/四元数；双骨IK无伸缩，肩根不动，越界明确失败。"""
    arm, gun = ctx['arm'],ctx['gun']
    gunmap = gun.matrix_world @ WB.GUN_BLENDER_TO_UE
    a,b,c = [arm.matrix_world @ arm.pose.bones[name].matrix for name in ('upperarm_l','lowerarm_l','hand_l')]
    target = gunmap @ wrist
    shoulder,previous_elbow,previous_hand = a.translation,b.translation,c.translation
    upper_length,lower_length = (previous_elbow-shoulder).length,(previous_hand-previous_elbow).length
    direction=target-shoulder
    distance=direction.length
    if distance >= upper_length+lower_length-.02 or distance<=abs(upper_length-lower_length)+.02:
        raise RuntimeError(f'Left hand unreachable {ctx["model"]}: {distance}/{upper_length+lower_length} wrist={list(wrist)}')
    direction.normalize()
    pole=ctx['map'].to_3x3() @ Vector((30,-18,-28))
    bend=(pole-direction*pole.dot(direction)).normalized()
    along=(upper_length**2+distance**2-lower_length**2)/(2*distance)
    elbow=shoulder+direction*along+bend*math.sqrt(max(0,upper_length**2-along**2))
    a_new=(previous_elbow-shoulder).rotation_difference(elbow-shoulder).to_matrix().to_4x4() @ a
    b_new=(previous_hand-previous_elbow).rotation_difference(target-elbow).to_matrix().to_4x4() @ b
    a_new.translation,b_new.translation=shoulder,elbow
    hand=gunmap @ rotation.to_matrix().to_4x4() @ ctx['correction']['hand_l']
    hand.translation=target
    for name,world in (('upperarm_l',a_new),('lowerarm_l',b_new),('hand_l',hand)):
        arm.pose.bones[name].matrix=arm.matrix_world.inverted() @ world
        bpy.context.view_layer.update()


@WB.traced
def pose_fingers(ctx, amounts):
    """amounts是每指三关节的附加屈曲角度；基于骨局部轴转动，禁止缩小手掌或骨长。"""
    rotations = {}
    for finger,degrees in amounts.items():
        for index,angle in enumerate(degrees,1):
            name=f'{finger}_{index:02d}_l'
            axis=ctx['finger_axes'][name]
            delta=Quaternion(axis,math.radians(angle))
            # FBX轴共轭只影响作者预览，导出记录仍为UE原骨局部增量。
            correction=ctx['correction'][name].to_quaternion()
            bone=ctx['arm'].pose.bones[name]
            bone.matrix = bone.matrix @ (correction.inverted() @ delta @ correction).to_matrix().to_4x4()
            bpy.context.view_layer.update()
            rotations[name]=[delta.x,delta.y,delta.z,delta.w]
    return rotations


@WB.traced
def gun_trees(ctx):
    """真实零件三角面按刚性骨缓存；每帧反变换顶点，避免把空心整枪当实心盒。"""
    result=[]
    for obj in bpy.data.collections[f'FP_{ctx["model"]}'].objects:
        if obj.type!='MESH' or not obj.name.startswith(('Reload_','Magazine internal','Charging handle')):
            continue
        mesh=obj.data
        mesh.calc_loop_triangles()
        vertices=[vertex.co.copy() for vertex in mesh.vertices]
        owner=next((group.name for group in obj.vertex_groups if group.name in ctx['gun'].pose.bones),'body')
        tree=BVHTree.FromPolygons(vertices,[list(triangle.vertices) for triangle in mesh.loop_triangles],all_triangles=True)
        bounds=np.array(vertices)
        result.append((obj.name,owner,tree,bounds.min(0),bounds.max(0)))
    return result


@WB.traced
def intersections(ctx, trees):
    """左臂、掌、指真实蒙皮顶点与全部枪件检查；输出最大深度与具体部件，不覆盖旧枪托结论。"""
    graph=bpy.context.evaluated_depsgraph_get()
    obj=bpy.data.objects[f'SK_UE_Arms_{ctx["model"]}']
    evaluated=obj.evaluated_get(graph)
    mesh=evaluated.to_mesh()
    groups={group.index:group.name for group in obj.vertex_groups}
    if 'left_vertex_indices' not in ctx:
        ctx['left_vertex_indices']=[v.index for v in obj.data.vertices if v.groups and groups[max(v.groups,key=lambda item:item.weight).group].endswith('_l')]
    indices=ctx['left_vertex_indices']  # 顶点归属只取决于原蒙皮，帧间缓存不缓存变形结果。
    coordinates=np.empty(len(mesh.vertices)*3,dtype=np.float32)  # 批量读取本帧真实变形网格，避免逐点Python矩阵开销。
    mesh.vertices.foreach_get('co',coordinates)
    transform=ctx['gun'].matrix_world.inverted() @ evaluated.matrix_world
    points=coordinates.reshape((-1,3))[indices] @ np.array(transform.to_3x3()).T+np.array(transform.translation)
    hits=[]
    for name,owner,tree,low,high in trees:
        inverse=(ctx['gun'].pose.bones[owner].matrix @ ctx['gun'].data.bones[owner].matrix_local.inverted()).inverted()
        local=points @ np.array(inverse.to_3x3()).T+np.array(inverse.translation)
        possible=np.flatnonzero(np.all((local>=low)&(local<=high),axis=1))
        depths=[]
        worst=None  # 最深点记录主蒙皮骨，避免把手指穿入误判为前臂问题。
        for index in possible:
            point=Vector(local[index])
            nearest,normal,unused,depth=tree.find_nearest(point)
            if depth>.02 and (point-nearest).dot(normal)<0 and ray_inside(tree,point):
                depths.append(float(depth))
                if worst is None or depth>worst['depth_cm']:
                    vertex=obj.data.vertices[indices[index]]
                    worst={'bone':groups[max(vertex.groups,key=lambda item:item.weight).group], 'point_gun_blender':[float(value) for value in points[index]],'depth_cm':float(depth)}
        if depths:
            hits.append({'piece':name,'vertices':len(depths),'max_depth_cm':max(depths),'worst':worst})
    evaluated.to_mesh_clear()
    return hits


@WB.traced
def reset(ctx):
    """恢复保存的原UE待机局部值，后续接触试作不会累加上次指骨旋转。"""
    for name,value in ctx['basis'].items():
        ctx['arm'].pose.bones[name].matrix_basis=value
    bpy.context.view_layer.update()


@WB.traced
def preview(ctx,label):
    """近距离左侧与第一人称真网格预览，镜头不改变实体检查输入。"""
    camera=bpy.data.objects['WorkbenchCamera']
    scene=bpy.context.scene
    for view in ('Contact','FirstPerson'):
        if view=='Contact':
            target=ctx['gun'].matrix_world @ WB.GUN_BLENDER_TO_UE @ Vector((6,0,-4))
            camera.location=target+ctx['map'].to_3x3() @ Vector((30,-95,12))
            camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
            camera.data.type='ORTHO'; camera.data.ortho_scale=56; camera.data.clip_start=.5
        else:
            camera.location=ctx['map'] @ Vector((10,0,147))
            camera.rotation_euler=(ctx['map'].to_3x3() @ Vector((1,0,0))).to_track_quat('-Z','Y').to_euler()
            camera.data.type='PERSP'; camera.data.lens=18; camera.data.sensor_width=36; camera.data.clip_start=10
        scene.camera=camera
        scene.render.resolution_percentage=65
        scene.render.filepath=str(OUT/'LeftHandPreviews'/f'{ctx["model"]}_{label}_{view}.png')
        bpy.ops.render.render(write_still=True)


@WB.traced
def sample_contact(ctx, settings, variant, phase):
    """分段接触轨迹：外侧绕行、张手、握匣、松手、机械操作、回握；返回枪局部姿态。"""
    # contact值只有本帧生命周期；活动零件位置/朝向使用刚性骨当前真实变换。
    contacts={}
    for label,source in settings.items():
        deform=Matrix.Identity(4)
        owner='magazine' if label=='Magazine' else 'bolt' if label=='Mechanism' and ctx['model'] in ('Rifle','Shotgun') else None
        if owner:
            deform=WB.GUN_BLENDER_TO_UE @ ctx['gun'].pose.bones[owner].matrix @ ctx['gun'].data.bones[owner].matrix_local.inverted() @ WB.GUN_BLENDER_TO_UE
        contacts[label]=(deform @ Vector(source['wrist_cm']),deform.to_quaternion() @ Quaternion(source['delta_wxyz']) @ ctx['hand_rotation'],source['fingers'])
    if variant in ('Idle','Fire'):
        return contacts['Support']
    empty=variant=='Empty'
    reach,detach,seat=(.08,.16,.57) if empty else (.10,.20,.68)
    opened={finger:[-15,-18,-10] for finger in ('index','middle','ring','pinky')}
    opened['thumb']=[-40,-20,0]
    # 每个键为phase/接触基准/外移厘米/张开权重；外移只用于过渡，正式抓握保持零偏移。
    keys=[(0,'Support',(0,0,0),0),(reach*.45,'Support',(0,-4,-2),1),
          (reach,'Magazine',(0,-4,-1),1),(detach-.03,'Magazine',(0,0,0),0),
          (seat,'Magazine',(0,0,0),0),(seat+.04,'Magazine',(0,-4,0),1)]
    if not empty:
        keys.extend([(seat+.12,'Support',(0,-4,-1),1),(.92,'Support',(0,0,0),0),(1,'Support',(0,0,0),0)])
    elif ctx['model']=='Sniper':
        keys.extend([(.66,'Support',(0,-3,-1),1),(.71,'Support',(0,0,0),0),(1,'Support',(0,0,0),0)])
    else:
        start,end=(.74,.80) if ctx['model']=='Pistol' else (.68,.82 if ctx['model']=='Rifle' else .84)
        # 离匣后先在机匣外侧抬手，再靠近拉机柄；直线抬手会让拇指切穿扳机护圈/机匣。
        keys.extend([(start-.04,'Mechanism',(0,-9,-4),1),(start-.025,'Mechanism',(0,-4,0),1),(start,'Mechanism',(0,0,0),0),
                     (end,'Mechanism',(0,0,0),0),(end+.04,'Mechanism',(0,-4,0),1),
                     (.96,'Support',(0,-2,-1),1),(1,'Support',(0,0,0),0)])
    first,second=keys[-2:]
    for index in range(len(keys)-1):
        if keys[index][0]<=phase<=keys[index+1][0]:
            first,second=keys[index:index+2]
            break
    alpha=smooth((phase-first[0])/max(second[0]-first[0],.001))
    values=[]
    for key in (first,second):
        point,rotation,fingers=contacts[key[1]]
        amount={finger:[a*(1-key[3])+b*key[3] for a,b in zip(angles,opened[finger])] for finger,angles in fingers.items()}
        values.append((point+Vector(key[2]),rotation,amount))
    fingers={finger:[a*(1-alpha)+b*alpha for a,b in zip(values[0][2][finger],values[1][2][finger])] for finger in opened}
    return values[0][0].lerp(values[1][0],alpha),values[0][1].slerp(values[1][1],alpha),fingers


@WB.traced
def load_mechanical(ctx):
    """从更新后的独立枪械Blend追加动作，原工作台机械快照重命名保留以便对照。"""
    model=ctx['model']
    names=[f'A_Breach_{model}_Reload_{variant}' for variant in ('Tactical','Empty')]
    for name in names:
        previous=bpy.data.actions.get(name)
        if previous:
            previous.name='BeforeLeftContact_'+name
    with bpy.data.libraries.load(str(ROOT/f'Art/Weapons/Animations/Breach_{model}_Reload.blend'),link=False) as (source,target):
        target.actions=names
    for action in target.actions:
        action.use_fake_user=True  # 两种动作都要跨保存保留，不能仅保留最后播放的Empty。
    return {variant:bpy.data.actions[f'A_Breach_{model}_Reload_{variant}'] for variant in ('Tactical','Empty')}


@WB.traced
def build_tracks(ctx,settings,entry,trees):
    """按原动画60Hz逐帧生成接触轨迹；检查动态真实蒙皮并保留可编辑对照Action。"""
    model=ctx['model']
    mechanical=load_mechanical(ctx)
    tracks,records={},[]
    for variant in ('Idle','Fire','Tactical','Empty'):
        seconds=1 if variant=='Idle' else .18 if variant=='Fire' else entry['clips'][0]['seconds']
        frames=round(seconds*60)
        # 现有真实UE右臂/锚点由旧序列提供；左臂所有局部骨每帧从参考复制，不继承旧伸手路径。
        old=bpy.data.actions[f'FinalUE_{model}_{variant if variant in ("Tactical","Empty") else "Idle"}']
        action=bpy.data.actions.new(f'LeftContact_{model}_{variant}')
        action.use_fake_user=True
        samples=[]
        for frame in range(frames+1):
            phase=frame/frames
            WB.assign_action(ctx['arm'],old)
            WB.assign_action(ctx['gun'],mechanical.get(variant))
            if variant not in mechanical:
                for bone in ctx['gun'].pose.bones:
                    bone.matrix_basis=Matrix.Identity(4)
            # UE FBX导出尾部多一帧重复保持，不能按整个Action范围缩放时间；源机械和UE动作以真实60Hz秒对齐。
            WB.set_frame(float(old.frame_range[0])+frame)
            ctx['arm'].animation_data.action=None
            for name,basis in ctx['basis'].items():
                if name.endswith('_l'):
                    ctx['arm'].pose.bones[name].matrix_basis=basis
            bpy.context.view_layer.update()
            wrist,rotation,fingers=sample_contact(ctx,settings,variant,phase)
            solve_arm(ctx,wrist,rotation)
            deltas=pose_fingers(ctx,fingers)
            samples.append({'wrist_cm':list(wrist),'rotation_xyzw':[rotation.x,rotation.y,rotation.z,rotation.w],'finger_delta_xyzw':deltas})
            # Fire与Idle握姿相同，仅首帧验证；八段换弹检查每个实际采样帧。
            if variant in ('Tactical','Empty') or frame==0:
                hits=intersections(ctx,trees)
                records.append({'model':model,'variant':variant,'phase':phase,'hits':hits})
            WB.assign_action(ctx['arm'],action)
            for bone in ctx['arm'].pose.bones:
                bone.keyframe_insert(data_path='location',frame=frame+1)
                bone.keyframe_insert(data_path='rotation_quaternion',frame=frame+1)
                bone.keyframe_insert(data_path='scale',frame=frame+1)
            if variant=='Tactical' and min(abs(phase-target) for target in (.1,.17,.32,.6,.72,.92))<.5/frames:
                preview(ctx,f'{variant}_{frame:03d}')
        tracks[variant]={'frames':frames,'seconds':seconds,'samples':samples}
    return tracks,records


@WB.traced
def main():
    """先标定静态掌匣接触并输出真实穿插报告；命令行--static只运行快速接触验证。"""
    bpy.ops.wm.open_mainfile(filepath=str(OUT/'Breach_FP_Reload_Workbench.blend'))
    (OUT/'LeftHandPreviews').mkdir(exist_ok=True)
    manifest=json.loads((OUT/'workbench_manifest.json').read_text(encoding='utf-8'))
    reference=json.loads((OUT/'Reference/arms_reference.json').read_text(encoding='utf-8'))
    mechanical=json.loads((ROOT/'Art/Weapons/Animations/weapon_animation_manifest.json').read_text(encoding='utf-8'))
    profile=json.loads((OUT/'left_hand_contacts.json').read_text(encoding='utf-8'))
    groups={model:bpy.data.collections[f'FP_{model}'] for model in WB.MODELS}
    results=[]  # 每个静态握点以及整段动作的真实穿插数据分别保存，不能互相替代。
    tracks,records={},[]
    for model in WB.MODELS:
        WB.show_group(groups,model)
        ctx=context(model,manifest,reference)
        entry=next(row for row in mechanical['weapons'] if row['model']==model)
        trees=gun_trees(ctx)
        for label in ('Support','Magazine','Mechanism'):
            reset(ctx)
            contact=profile['weapons'][model][label]
            rotation=Quaternion(contact['delta_wxyz']) @ ctx['hand_rotation']
            solve_arm(ctx,Vector(contact['wrist_cm']),rotation)
            pose_fingers(ctx,contact['fingers'])
            hits=intersections(ctx,trees)
            results.append({'model':model,'contact':label,'hits':hits})
            preview(ctx,label)
        if '--static' not in sys.argv:
            tracks[model],model_records=build_tracks(ctx,profile['weapons'][model],entry,trees)
            records.extend(model_records)
    (OUT/'left_hand_static_diagnostic.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print('LEFT_CONTACT_STATIC_COMPLETE',json.dumps(results),flush=True)
    if tracks:
        report={'version':1,'fps':60,'units':'centimetres','profile_sha256':hashlib.sha256((OUT/'left_hand_contacts.json').read_bytes()).hexdigest(),
                'mechanical_manifest_sha256':hashlib.sha256((ROOT/'Art/Weapons/Animations/weapon_animation_manifest.json').read_bytes()).hexdigest(),'weapons':tracks}
        (OUT/'left_hand_contact_tracks.json').write_text(json.dumps(report,separators=(',',':')),encoding='utf-8')
        (OUT/'left_hand_timeline_diagnostic.json').write_text(json.dumps({'samples':records,'sample_count':len(records),
            'failed_samples':sum(bool(row['hits']) for row in records),'max_depth_cm':max((hit['max_depth_cm'] for row in records for hit in row['hits']),default=0)},indent=2),encoding='utf-8')
        bpy.context.preferences.filepaths.save_version=0
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Breach_FP_LeftHand_Contacts.blend'))
        print('LEFT_CONTACT_TRACKS_COMPLETE',len(records),flush=True)


if __name__=='__main__':
    main()
