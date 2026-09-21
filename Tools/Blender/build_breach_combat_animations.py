"""在已验证骨架上烘焙机械战斗动作；独立动画源不覆盖模型/绑定检查源，root 始终不位移。"""
import json
import math
from pathlib import Path
import bpy
from mathutils import Quaternion, Vector

# 项目内专属动画目录；所有距离为 Blender 厘米，24fps 与已有 FBX 管线一致。
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Art/Enemies/Animations"
FPS = 24
# 动作秒数向整数帧对齐；Area/Global 的爆发帧固定为 .9/2 秒附近，伤害由玩法时钟精确结算。
CLIPS = {"Idle":2, "Move":1, "Melee":.625, "Fire":.5, "Hit":.375, "Death":1.5,
         "Area":1.25, "Global":2.5, "Rise":1, "Hover":3, "Dive":.625, "Recovery":1, "Rage":1}


def rotate(rig, name, axis, degrees):
    """rig/name 指定当前借用骨；axis 为模型空间轴，degrees 为相对参考姿势的角度。"""
    print(f"[CALL] rotate {name}")
    joint = rig.pose.bones.get(name)  # 普通怪不存在 Boss 扩展骨，调用者可共用姿势函数。
    if joint is None:
        return
    basis = joint.bone.matrix_local.to_quaternion()  # 固定参考轴，避免不同骨长轴导致反转。
    # 同一帧多个轴旋转依次复合，死亡侧翻与前倾不能互相覆盖；pose入口每帧先清零。
    joint.rotation_quaternion = joint.rotation_quaternion @ basis.inverted() @ Quaternion(Vector(axis), math.radians(degrees)) @ basis


def translate(rig, name, offset):
    """offset 为模型空间厘米；只移动 body 等视觉骨，不改变游戏 root 和碰撞。"""
    print(f"[CALL] translate {name}")
    joint = rig.pose.bones[name]  # 借用当前帧骨对象，不跨场景持有。
    joint.location = joint.bone.matrix_local.to_quaternion().inverted() @ Vector(offset)


def pose(rig, kind, t):
    """kind 为动作键，t 为归一化时间[0,1]；每帧从参考姿势重算，避免跨动作残留。"""
    print(f"[CALL] pose {kind} {t:.3f}")
    for joint in rig.pose.bones:  # 当前全部姿势通道，刚性部件不允许缩放动画。
        joint.rotation_mode = "QUATERNION"
        joint.rotation_quaternion = (1,0,0,0)
        joint.location = (0,0,0)
        joint.scale = (1,1,1)
    wave = math.sin(t*math.tau)  # 循环的首尾完全一致，方便混合。
    recoil = math.sin(math.pi*min(t*3,1)) * max(0,1-t)  # 开火/受击快起慢落，不影响攻击时钟。
    wind = min(t*4,1) * max(0,min((1-t)*4,1))  # 升空/悬停的柔和展开包络。
    if kind in ("Idle","Move"):
        translate(rig,"body",(0,0,(1.6 if kind=="Idle" else .8)*wave))
        rotate(rig,"body",(0,1,0),-9 if kind=="Move" else 1.2*wave)
        rotate(rig,"core",(1,0,0),4*wave)
    if kind in ("Fire","Hit"):
        translate(rig,"body",(-3*recoil,0,0))
        rotate(rig,"body",(0,1,0),(-8 if kind=="Hit" else -3)*recoil)
    if kind=="Melee":
        # 普通近战仍为即时接触伤害：首帧已处于撞击延伸，后续回收，不新增隐形前摇。
        translate(rig,"body",(7*(1-t)**2,0,0))
    if kind=="Death":
        translate(rig,"body",(0,0,-22*t*t))
        rotate(rig,"body",(1,0,0),38*t)
        rotate(rig,"body",(0,1,0),-22*t)
    charge = 0.0  # 核心护板开启比例；攻击爆发后快速收回。
    if kind in ("Area","Global"):
        release = .9/CLIPS[kind] if kind=="Area" else 2/CLIPS[kind]  # 对齐原规则的释放时间比例。
        charge = t/release if t<release else max(0,1-(t-release)/(1-release))
        rotate(rig,"body",(0,1,0),-7*charge)
        rotate(rig,"core",(1,0,0),95*charge)
        rotate(rig,"halo",(1,0,0),-28*charge)
    if kind in ("Rise","Hover","Dive","Recovery","Rage"):
        charge = wind if kind=="Rage" else (.65 if kind in ("Hover","Dive") else (t if kind=="Rise" else 1-t)*.65)
        rotate(rig,"body",(0,1,0),-18*t if kind=="Rise" else (20 if kind=="Dive" else 4*wave))
        rotate(rig,"halo",(1,0,0),8*wave)
        rotate(rig,"core",(1,0,0),30*wave)
    for side,label in ((-1,"l"),(1,"r")):  # 左右符号只控制对称关节，不改变骨架层级。
        arm = 3*wave if kind=="Idle" else (12 if kind=="Move" else 22*charge)
        elbow = -8 if kind=="Move" else -25*charge
        if kind=="Melee": elbow=-65*(1-t)**2
        if kind=="Fire": elbow=-18*recoil
        if kind=="Death": arm=side*8*t; elbow=28*t
        rotate(rig,f"arm_{label}",(1,0,0),side*arm)
        rotate(rig,f"forearm_{label}",(0,1,0),elbow)
        rotate(rig,f"thruster_{label}",(0,1,0),14 if kind=="Move" else -8*charge)
        for index in range(1,4):  # Boss 三指展开；普通骨架缺失时由 rotate 安全忽略。
            rotate(rig,f"claw_{label}_{index:02d}",(0,0,1),(index-2)*20*charge)
    for name,axis,sign in (("shutter_top",(0,1,0),-1),("shutter_bottom",(0,1,0),1),
                           ("shutter_left",(0,0,1),-1),("shutter_right",(0,0,1),1)):
        rotate(rig,name,axis,sign*48*charge)


def build():
    """后台入口：读取模型源、创建独立动作源和 FBX；缺模型/骨架时直接失败。"""
    print("[CALL] build combat animations",flush=True)
    (OUT/"FBX").mkdir(parents=True,exist_ok=True)
    entries = []  # JSON 仅保存稳定资源名、时长、循环标识，详见模块文档。
    for model in ("Chaser","Warden"):
        bpy.ops.wm.open_mainfile(filepath=str(ROOT/f"Art/Enemies/Models/Breach_{model}_Rigged.blend"))
        rig = bpy.data.objects["Armature"]  # 当前模型的唯一骨架，场景切换后不复用引用。
        rig.animation_data_clear()
        scene = bpy.context.scene  # 烘焙使用原厘米场景，不重新导入网格造成轴变化。
        scene.render.fps = FPS
        for kind,seconds in CLIPS.items():
            if model=="Chaser" and kind in ("Area","Global","Rise","Hover","Dive","Recovery","Rage"): continue
            action = bpy.data.actions.new(f"A_Breach_{model}_{kind}")  # 每个动作 fake_user 保存在动画 .blend。
            action.use_fake_user = True
            rig.animation_data_create()
            rig.animation_data.action = action
            scene.frame_start = 1
            scene.frame_end = 1+round(seconds*FPS)
            for frame in range(1,scene.frame_end+1):  # 逐帧解析姿势，避免插值过冲穿过机械关节。
                scene.frame_set(frame)
                pose(rig,kind,(frame-1)/(scene.frame_end-1))
                for joint in rig.pose.bones:
                    joint.keyframe_insert(data_path="rotation_quaternion",frame=frame,group=joint.name)
                    joint.keyframe_insert(data_path="location",frame=frame,group=joint.name)
                    joint.keyframe_insert(data_path="scale",frame=frame,group=joint.name)
            bpy.ops.object.select_all(action="DESELECT")
            rig.select_set(True)
            bpy.context.view_layer.objects.active = rig
            bpy.ops.export_scene.fbx(filepath=str(OUT/"FBX"/(action.name+".fbx")),use_selection=True,
                object_types={"ARMATURE"},axis_forward="-Y",axis_up="Z",apply_unit_scale=True,
                apply_scale_options="FBX_SCALE_UNITS",add_leaf_bones=False,use_armature_deform_only=True,
                bake_anim=True,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,
                bake_anim_step=1,bake_anim_simplify_factor=0)
            entries.append({"model":model,"kind":kind,"name":action.name,"seconds":seconds,"loop":kind in ("Idle","Move")})
        rig.animation_data.action = bpy.data.actions[f"A_Breach_{model}_Idle"]
        scene.frame_end = 49
        scene.frame_set(1)
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT/f"Breach_{model}_Combat.blend"))
    (OUT/"combat_manifest.json").write_text(json.dumps(entries,indent=2),encoding="utf-8")
    print(f"BREACH_COMBAT_EXPORT_SUCCESS clips={len(entries)}",flush=True)


if __name__=="__main__":
    build()
