"""独立 Blender 进程制作 Chaser/Warden 模型、刚性蒙皮骨架与诊断动作；不修改游戏敌人逻辑。"""
import bpy
import bmesh
import functools
import json
import math
from pathlib import Path
from mathutils import Vector, Quaternion

# 所有形状参数以米书写，实际场景使用厘米和 unit_scale=.01，避免骨架导入出现 100 倍根缩放。
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Art/Enemies/Models"
PALETTE = {
    "Frame": ((.045,.060,.072,1), .65,.34),
    "Armor": ((.62,.66,.66,1), .38,.32),
    "Steel": ((.24,.29,.32,1), .85,.27),
    "Rubber": ((.012,.017,.020,1), .05,.78),
    "Amber": ((1.0,.12,.009,1), .2,.26),
    "Dark": ((.006,.011,.015,1), .25,.58),
}
# 当前模型的同步构建缓存；源对象保留独立零件，权重归属只写一个骨骼组。
PARTS = []
BONES = []
MATS = {}
RIG = None
CURRENT_BONE = "body"


def traced(function):
    """function 为同步主线程操作；闭包仅随后台进程存在，用于记录所有构建函数调用。"""
    print(f"[EnemyBuild] register {function.__name__}", flush=True)
    @functools.wraps(function)
    def wrapped(*args, **kwargs):
        """args/kwargs 为本次参数，function 为进程内捕获的函数；无异步对象或线程切换。"""
        print(f"[EnemyBuild] call {function.__name__}", flush=True)
        return function(*args, **kwargs)
    return wrapped


@traced
def point(value):
    """value 为逻辑 UE 米坐标；映射 Blender 厘米及反向 Y，FBX 导入后恢复 UE +X前/+Y右/+Z上。"""
    # 离线构建保留逐次调用日志，重定向到构建文件，不影响游戏日志与帧率。
    return Vector((value[0]*100, -value[1]*100, value[2]*100))


@traced
def finish(obj, name, material, bevel=.006, bone=None, smooth=False):
    """obj 是新网格；name/材质/米制倒角定义外观，bone 为空使用 CURRENT_BONE，所有点刚性归属。"""
    obj.name = name
    obj.data.materials.append(MATS[material])
    # group 由源网格持有，1.0 权重保证金属不软弯；组名必须存在于最终骨架。
    group = obj.vertex_groups.new(name=bone or CURRENT_BONE)
    group.add(list(range(len(obj.data.vertices))), 1.0, "REPLACE")
    for polygon in obj.data.polygons:  # polygon 为当前对象的面，仅影响曲面视觉。
        polygon.use_smooth = smooth
    if bevel:
        # modifier 的倒角保持可编辑；先于 Armature 生效，导出副本才应用。
        modifier = obj.modifiers.new("Machined edges", "BEVEL")
        modifier.width = bevel*100
        modifier.segments = 2
        normal = obj.modifiers.new("Weighted normals", "WEIGHTED_NORMAL")
        normal.keep_sharp = True
    PARTS.append(obj)
    return obj


@traced
def box(name, location, size, material="Frame", bevel=.006, bone=None):
    """location/size 为米制中心和 XYZ 尺寸，返回独立可编辑金属件；其余参数传递 finish。"""
    bpy.ops.mesh.primitive_cube_add(size=1, location=point(location))
    obj = bpy.context.object  # 当前新建零件，应用比例让倒角宽度保持一致。
    obj.dimensions = Vector(size)*100
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    return finish(obj,name,material,bevel,bone)


@traced
def mesh(name, vertices, faces, material="Frame", bevel=.006, bone=None, smooth=False):
    """vertices 为逻辑米坐标，faces 为拓扑；创建封闭零件并重算法线，避免 Y 反射反转表面。"""
    data = bpy.data.meshes.new(name)  # Blender 拥有的数据块，临时 bmesh 用完释放。
    data.from_pydata([point(vertex) for vertex in vertices], [], faces)
    data.update()
    bm = bmesh.new()
    bm.from_mesh(data)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(data)
    bm.free()
    obj = bpy.data.objects.new(name,data)
    bpy.context.scene.collection.objects.link(obj)
    return finish(obj,name,material,bevel,bone,smooth)


@traced
def hull(name, front, back, outline, material="Armor", bevel=.008, bone=None):
    """front/back 为 X 米位置，outline 是 Y/Z 轮廓；沿前后挤出的倒角装甲板。"""
    count = len(outline)  # 环上的点数；顶点顺序由 bmesh 统一修正。
    vertices = [(x,y,z) for x in (front,back) for y,z in outline]
    faces = [tuple(range(count)), tuple(range(count,count*2))]
    for index in range(count):
        faces.append((index,(index+1)%count,(index+1)%count+count,index+count))
    return mesh(name,vertices,faces,material,bevel,bone)


@traced
def cylinder(name, location, radius, length, material="Steel", axis="X", bone=None, segments=24):
    """location/radius/length 为米，axis 控制轴向；单个金属轴或后坐机构使用一根骨骼。"""
    bpy.ops.mesh.primitive_cylinder_add(vertices=segments,radius=radius*100,depth=length*100,location=point(location))
    obj = bpy.context.object  # 圆柱默认为 Z 轴，旋转后绑定仍保留米制中心。
    if axis == "X": obj.rotation_euler.y = math.pi/2
    elif axis == "Y": obj.rotation_euler.x = math.pi/2
    return finish(obj,name,material,min(.004,length/8),bone,True)


@traced
def beam(name, start, end, width, depth, material="Steel", bone=None):
    """start/end 为逻辑米端点，width/depth 为横截面米尺寸；朝向由两点确定，用于机械臂和支撑。"""
    a,b = point(start),point(end)  # 转换后的厘米端点；仅构建期间使用。
    bpy.ops.mesh.primitive_cube_add(size=1,location=(a+b)/2)
    obj = bpy.context.object
    obj.dimensions = (width*100,depth*100,(b-a).length)
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    obj.rotation_euler = (b-a).to_track_quat("Z","Y").to_euler()
    return finish(obj,name,material,.004,bone)


@traced
def ring(name, center, radius, width, thickness, material="Steel", axis="X", bone=None, start=0, end=360):
    """米制 center/radius、径向 width、轴向 thickness；可创建完整圈或指定角度的封闭分段。"""
    # steps 控制弧段细分，每圈 64 段上限；vertices/faces 为当前环的临时缓冲。
    steps = max(3,round((end-start)/360*64))
    vertices,faces = [],[]
    for index in range(steps+1):
        angle = math.radians(start+(end-start)*index/steps)
        for axial, radial in ((-thickness/2,radius-width/2),(-thickness/2,radius+width/2),(thickness/2,radius+width/2),(thickness/2,radius-width/2)):
            if axis == "X":
                vertices.append((center[0]+axial,center[1]+math.cos(angle)*radial,center[2]+math.sin(angle)*radial))
            else:
                vertices.append((center[0]+math.cos(angle)*radial,center[1]+math.sin(angle)*radial,center[2]+axial))
    for index in range(steps):
        for side in range(4):
            faces.append((index*4+side,index*4+(side+1)%4,(index+1)*4+(side+1)%4,(index+1)*4+side))
    faces.extend(((0,1,2,3),tuple(range(steps*4,steps*4+4))))
    return mesh(name,vertices,faces,material,.002,bone,True)


@traced
def bone(name, head, tail, parent):
    """name 为稳定导出骨名，head/tail 为逻辑米坐标，parent 为空仅 root；缓存至 armature 编辑模式创建。"""
    BONES.append({"name":name,"head":head,"tail":tail,"parent":parent})


@traced
def thruster(name, center, size, parent="body"):
    """center 为米制喷口机构中心，size 为比例；创建独立推进器骨骼，橙灯不代表真实游戏伤害。"""
    x,y,z = center  # 当前喷口的逻辑坐标米。
    bone(name,(x,y,z+.10*size),(x,y,z-.12*size),parent)
    cylinder("Thruster housing",center,.11*size,.20*size,"Frame","Z",name)
    ring("Nozzle rim",(x,y,z-.11*size),.085*size,.021*size,.035*size,"Steel","Z",name)
    cylinder("Nozzle dark interior",(x,y,z-.10*size),.070*size,.018*size,"Dark","Z",name)
    ring("Nozzle amber ring",(x,y,z-.125*size),.065*size,.012*size,.010*size,"Amber","Z",name)
    box("Thruster armor",(x+.07*size,y,z+.025*size),(.11*size,.20*size,.12*size),"Armor",.009*size,name)


@traced
def fastener(center, size=.012, parent="body"):
    """center 为前向面螺钉坐标米，size 为半径，parent 为所属装甲骨骼；造型不影响碰撞。"""
    cylinder("Recess bolt",center,size,.006,"Steel","X",parent,12)
    box("Bolt slot",(center[0]+.004,center[1],center[2]),(.002,size*.95,size*.20),"Dark",0,parent)


@traced
def base_bones():
    """共同前五根骨骼定义：root 固定，body 悬浮，core/瞄准有独立通道；单位逻辑米。"""
    bone("root",(0,0,0),(0,0,.12),None)
    bone("body",(0,0,.02),(0,0,.25),"root")
    bone("core",(.24,0,.03),(.40,0,.03),"body")
    bone("aim_yaw",(.22,0,.15),(.22,0,.24),"body")
    bone("aim_pitch",(.26,0,.15),(.38,0,.15),"aim_yaw")


@traced
def chaser():
    """构建已确认的楔形撞击者；双臂骨架为后续远程/侧翼同骨架变体预留，不生成未授权新怪。"""
    base_bones()
    hull("Reactor chassis",.26,-.25,[(-.40,.27),(.40,.27),(.39,.02),(.18,-.20),(-.18,-.20),(-.39,.02)],"Frame",.020)
    hull("Central impact armor",.35,.25,[(-.32,.31),(.32,.31),(.27,.10),(.14,-.17),(-.14,-.17),(-.27,.10)],"Armor",.012)
    # 下方反应器部分露出，核心骨可旋转；装甲与传感器分别由各自骨驱动。
    cylinder("Lower reactor",(.24,0,-.15),.11,.13,"Steel","X","core")
    ring("Core power ring",(.315,0,-.15),.074,.011,.01,"Amber","X","core")
    box("Sensor recess",(.365,0,.12),(.029,.286,.077),"Dark",.009,"aim_pitch")
    box("Sensor steel bezel",(.381,0,.117),(.018,.259,.040),"Steel",.004,"aim_pitch")
    box("Sensor amber slit",(.392,0,.118),(.007,.231,.021),"Amber",.002,"aim_pitch")
    for side,label in ((-1,"l"),(1,"r")):  # side 使用 UE 左负右正，几何转换统一反射至 Blender。
        bone(f"armor_{label}",(.05,side*.33,.26),(.17,side*.43,.21),"body")
        bone(f"fin_{label}",(-.17,side*.28,.25),(-.30,side*.36,.25),"body")
        hull("Shoulder armor",.25,-.20,[(side*.33,.29),(side*.48,.25),(side*.52,.02),(side*.34,.035)],"Armor",.012,f"armor_{label}")
        box("Rear service hatch",(-.259,side*.22,.10),(.025,.26,.24),"Armor",.010,f"fin_{label}")
        for index in range(3):  # index 是背侧散热槽序号，仅作低成本凹槽嵌件。
            box("Rear ventilation",(-.274,side*.22,.05+index*.044),(.011,.18,.013),"Dark",.003,f"fin_{label}")
        box("Upper antenna base",(-.08,side*.27,.31),(.075,.065,.055),"Frame",.006)
        box("Short sensor mast",(-.08,side*.27,.365),(.036,.033,.095),"Steel",.003)
        fastener((.365,side*.28,.245),.009)
        fastener((.362,side*.13,-.105),.008)
        # 三段 FK：上臂、前臂、撞击板；胶接位置位于圆形轴心，不把关节当可变形橡胶。
        shoulder = (0,side*.49,.105)
        elbow = (.005,side*.60,-.14)
        wrist = (.18,side*.65,-.34)
        bone(f"arm_{label}",shoulder,elbow,"body")
        bone(f"forearm_{label}",elbow,wrist,f"arm_{label}")
        bone(f"weapon_{label}",wrist,(.30,side*.65,-.40),f"forearm_{label}")
        cylinder("Shoulder bearing",shoulder,.090,.12,"Steel","Y",f"arm_{label}")
        cylinder("Shoulder black hub",(0,side*.56,.105),.062,.018,"Frame","Y",f"arm_{label}")
        beam("Upper arm housing",shoulder,elbow,.085,.115,"Frame",f"arm_{label}")
        beam("Arm piston",(.065,side*.49,.04),(.067,side*.59,-.105),.018,.035,"Steel",f"arm_{label}")
        cylinder("Elbow bearing",elbow,.068,.095,"Steel","Y",f"forearm_{label}")
        beam("Forearm frame",elbow,wrist,.085,.085,"Frame",f"forearm_{label}")
        beam("Forearm armor",(.045,side*.61,-.17),(.174,side*.65,-.29),.105,.10,"Armor",f"forearm_{label}")
        box("Impact pad chassis",(.225,side*.65,-.37),(.22,.22,.21),"Frame",.018,f"weapon_{label}")
        hull("Impact face",.357,.320,[(side*.535,-.27),(side*.76,-.28),(side*.75,-.46),(side*.56,-.48)],"Armor",.013,f"weapon_{label}")
        box("Impact rubber ridge",(.365,side*.65,-.38),(.01,.145,.043),"Rubber",.004,f"weapon_{label}")
        fastener((.37,side*.70,-.31),.010,f"weapon_{label}")
        thruster(f"thruster_{label}",(-.10,side*.235,-.28),1.0)
    return {"name":"Chaser","mesh":"SK_Breach_Chaser","skeleton":"SKEL_Breach_Drone",
            "sockets":{"Muzzle_L":("weapon_l",(.38,-.65,-.35)),"Muzzle_R":("weapon_r",(.38,.65,-.35)),
                       "CoreFX":("core",(.34,0,-.15)),"HealthBar":("body",(0,0,.60)),
                       "ThrusterFX_L":("thruster_l",(-.10,-.235,-.41)),"ThrusterFX_R":("thruster_r",(-.10,.235,-.41))}}


@traced
def warden():
    """构建双臂、四瓣核心护板和分段背环的 Boss；共享第 5/10 关后续外观变体骨架。"""
    base_bones()
    hull("Warden inner chassis",.39,-.47,[(-.67,.85),(.67,.85),(.80,.44),(.58,-.35),(-.58,-.35),(-.80,.44)],"Frame",.035)
    # reactor 几何属于 core，静止外圈属于 body；护板展开时核心不会被拉伸。
    cylinder("Core cavity",(.405,0,.24),.55,.16,"Dark","X","body",48)
    ring("Reactor outer rim",(.51,0,.24),.50,.085,.13,"Steel",bone="body")
    cylinder("Core dark hub",(.48,0,.24),.42,.12,"Frame","X","core",48)
    ring("Reactor luminous ring",(.558,0,.24),.31,.034,.020,"Amber",bone="core")
    ring("Inner reactor rim",(.58,0,.24),.24,.045,.032,"Steel",bone="core")
    cylinder("Reactor energy lens",(.60,0,.24),.194,.035,"Amber","X","core",48)
    cylinder("Reactor center",(.624,0,.24),.089,.013,"Armor","X","core",32)
    for angle in range(0,360,45):  # rad/y/z 是辐射状机械细节，核心旋转时一起运动。
        rad = math.radians(angle)
        y,z = math.cos(rad)*.38,.24+math.sin(rad)*.38
        box("Core radiator block",(.566,y,z),(.032,.046,.046),"Steel",.004,"core")
    # 四瓣护板围绕边缘铰链开合；默认中间留出能量窗口，运动检查会逐瓣打开。
    for label,angle in (("top",90),("right",0),("bottom",270),("left",180)):
        rad = math.radians(angle)
        head = (.48,math.cos(rad)*.54,.24+math.sin(rad)*.54)
        bone(f"shutter_{label}",head,(head[0]+.13,head[1],head[2]),"body")
        outline = []  # 两段圆弧形成独立梯形扇面，中央不交叉。
        for radius, angles in ((.565,(-40,-20,20,40)),(.165,(37,0,-37))):
            for offset in angles:
                a = math.radians(angle+offset)
                outline.append((math.cos(a)*radius,.24+math.sin(a)*radius))
        hull("Core shutter "+label,.676,.585,outline,"Armor",.012,f"shutter_{label}")
        fastener((.691,head[1]*.85,.24+(head[2]-.24)*.85),.017,f"shutter_{label}")
    # 额部传感器与两侧封闭颈甲将中央躯干组织成可读的机器人轮廓。
    hull("Crown armor",.48,.29,[(-.32,.89),(.32,.89),(.27,.70),(-.27,.70)],"Armor",.021)
    box("Crown sensor recess",(.497,0,.775),(.02,.23,.054),"Dark",.006,"aim_pitch")
    box("Crown sensor amber",(.510,0,.775),(.009,.17,.015),"Amber",.002,"aim_pitch")
    bone("halo",(-.52,0,.60),(-.52,0,.85),"body")
    ring("Halo structural ring",(-.56,0,.60),1.17,.12,.13,"Frame",bone="halo")
    ring("Halo metallic rail",(-.483,0,.60),1.18,.033,.026,"Steel",bone="halo")
    # 六个可动画分段各有同一中心枢轴，可做缓慢转动/呼吸，不需要对整环软蒙皮。
    for index in range(6):
        name = f"halo_segment_{index+1:02d}"
        bone(name,(-.52,0,.60),(-.38,0,.60),"halo")
        ring("Halo armor segment",(-.468,0,.60),1.165,.095,.056,"Armor",bone=name,start=index*60+5,end=index*60+55)
        ring("Halo charged strip",(-.432,0,.60),1.147,.024,.012,"Amber",bone=name,start=index*60+10,end=index*60+50)
    beam("Halo central support",(-.52,0,.70),(-.52,0,1.84),.14,.12,"Frame","halo")
    box("Halo crown lock",(-.47,0,1.72),(.17,.24,.30),"Steel",.012,"halo")
    for side,label in ((-1,"l"),(1,"r")):
        # 骨骼头尾即实际 FK 旋转轴；肩甲归 shoulder，不跟着前臂弯曲。
        shoulder = (.0,side*.94,.56)
        elbow = (.04,side*1.24,.03)
        wrist = (.34,side*1.37,-.36)
        palm = (.52,side*1.39,-.53)
        bone(f"shoulder_{label}",(-.05,side*.66,.62),shoulder,"body")
        bone(f"arm_{label}",shoulder,elbow,f"shoulder_{label}")
        bone(f"forearm_{label}",elbow,wrist,f"arm_{label}")
        bone(f"wrist_{label}",wrist,palm,f"forearm_{label}")
        hull("Heavy shoulder armor",.35,-.30,[(side*.68,.91),(side*1.12,.94),(side*1.25,.67),(side*1.16,.34),(side*.78,.35)],"Armor",.030,f"shoulder_{label}")
        box("Shoulder dark vent",(.368,side*1.035,.67),(.040,.125,.31),"Dark",.015,f"shoulder_{label}")
        for index in range(4):
            box("Shoulder vent fin",(.396,side*1.035,.54+index*.079),(.022,.09,.019),"Steel",.003,f"shoulder_{label}")
        fastener((.388,side*.85,.78),.018,f"shoulder_{label}")
        cylinder("Boss shoulder bearing",shoulder,.15,.22,"Steel","Y",f"arm_{label}",32)
        beam("Upper arm truss",shoulder,elbow,.15,.19,"Frame",f"arm_{label}")
        beam("Upper arm piston",(.14,side*.99,.44),(.18,side*1.21,.08),.040,.055,"Steel",f"arm_{label}")
        cylinder("Boss elbow ring",elbow,.14,.20,"Steel","Y",f"forearm_{label}",32)
        cylinder("Boss elbow cover",(.04,side*1.36,.03),.09,.035,"Frame","Y",f"forearm_{label}",24)
        beam("Forearm mechanism",elbow,wrist,.17,.16,"Frame",f"forearm_{label}")
        beam("Forearm white plate",(.13,side*1.27,-.035),(.37,side*1.37,-.30),.22,.23,"Armor",f"forearm_{label}")
        cylinder("Wrist swivel",wrist,.13,.19,"Steel","Y",f"wrist_{label}",24)
        beam("Claw palm",wrist,palm,.22,.25,"Frame",f"wrist_{label}")
        for index,offset in enumerate((-.15,0,.15)):
            # 三指独立刚性件，张合用于抓握/砸击；避免手指绑定到整只手后无法展开。
            finger = f"claw_{label}_{index+1:02d}"
            start = (.49,side*1.39+offset,-.51)
            joint = (.72,side*1.39+offset*1.35,-.78)
            end = (.78,side*1.39+offset*.60,-.89)
            bone(finger,start,joint,f"wrist_{label}")
            cylinder("Claw hinge",start,.060,.072,"Steel","Y",finger,20)
            beam("Claw armored finger",start,joint,.105,.090,"Armor",finger)
            beam("Claw tip",joint,end,.085,.085,"Frame",finger)
        thruster(f"thruster_{label}",(-.11,side*.43,-.47),1.65)
        beam("Halo lower brace",(-.45,side*.38,-.05),(-.56,side*.82,-.22),.10,.13,"Steel","body")
        box("Rear service armor",(-.49,side*.35,.36),(.045,.46,.57),"Armor",.025)
    return {"name":"Warden","mesh":"SK_Breach_Warden","skeleton":"SKEL_Breach_Warden",
            "sockets":{"Muzzle_L":("wrist_l",(.67,-1.39,-.52)),"Muzzle_R":("wrist_r",(.67,1.39,-.52)),
                       "CoreFX":("core",(.72,0,.24)),"HealthBar":("body",(0,0,2.12)),
                       "ThrusterFX_L":("thruster_l",(-.11,-.43,-.70)),"ThrusterFX_R":("thruster_r",(-.11,.43,-.70))}}


@traced
def create_rig(name):
    """name 为型号；按 BONES 定义建立单根骨架，所有源零件挂接同一 Armature 并保留可编辑修改器。"""
    global RIG
    bpy.ops.object.select_all(action="DESELECT")
    # Armature 名称用于 UE FBX 导入器识别对象容器，不额外导出一层多余控制根骨。
    data = bpy.data.armatures.new(f"Rig_{name}")
    RIG = bpy.data.objects.new("Armature",data)
    bpy.context.scene.collection.objects.link(RIG)
    bpy.context.view_layer.objects.active = RIG
    RIG.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for entry in BONES:  # entry 是逻辑坐标定义，先父后子。
        item = data.edit_bones.new(entry["name"])
        item.head, item.tail = point(entry["head"]),point(entry["tail"])
        if entry["parent"]: item.parent = data.edit_bones[entry["parent"]]
        item.use_connect = False
        item.use_deform = True
    bpy.ops.object.mode_set(mode="OBJECT")
    RIG.show_in_front = True
    data.display_type = "OCTAHEDRAL"
    for obj in PARTS:  # 源网格归场景所有；父对象恒等，不引入世界/局部坐标补偿。
        obj.parent = RIG
        modifier = obj.modifiers.new("Rigid skin", "ARMATURE")
        modifier.object = RIG
    # 骨骼组名/每点权重是硬性导出约束，不能依赖自动热权重猜测金属件归属。
    for obj in PARTS:
        if len(obj.vertex_groups) != 1 or obj.vertex_groups[0].name not in data.bones:
            raise RuntimeError(f"Invalid rigid binding: {obj.name}")
        for vertex in obj.data.vertices:
            if len(vertex.groups)!=1 or abs(vertex.groups[0].weight-1)>1e-6:
                raise RuntimeError(f"Unweighted or blended rigid vertex: {obj.name}/{vertex.index}")
    print(f"ENEMY_RIG_VALID {name} bones={len(data.bones)} parts={len(PARTS)}",flush=True)


@traced
def set_rotation(name, world_axis, degrees):
    """name 为关节名，world_axis 为 Blender 世界旋转轴，degrees 为诊断角度；转为该骨的局部 FK 四元数。"""
    joint = RIG.pose.bones.get(name)  # 不存在说明骨架与诊断动作不匹配，应拒绝导出。
    if joint is None: raise RuntimeError(f"Missing joint {name}")
    basis = joint.bone.matrix_local.to_quaternion()
    joint.rotation_mode = "QUATERNION"
    joint.rotation_quaternion = basis.inverted() @ Quaternion(Vector(world_axis),math.radians(degrees)) @ basis


@traced
def diagnostic_action(name):
    """创建 4 秒关节检查动作：中立→展开→中立→反向→中立。仅验证蒙皮，不作为正式攻击动画。"""
    RIG.animation_data_create()
    action = bpy.data.actions.new(f"A_Breach_{name}_RigCheck")  # 动作随 .blend 保存，专用 QA FBX 导出。
    RIG.animation_data.action = action
    for frame,factor in ((1,0),(25,1),(49,0),(73,-.65),(97,0)):
        for joint in RIG.pose.bones:  # 重置所有通道，确保 root 始终恒等且循环精确回到中立。
            joint.rotation_mode = "QUATERNION"
            joint.rotation_quaternion = (1,0,0,0)
            joint.location = (0,0,0)
            joint.scale = (1,1,1)
        set_rotation("body",(1,0,0),4*factor)
        set_rotation("core",(1,0,0),40*factor)
        set_rotation("aim_yaw",(0,0,1),15*factor)
        for side,label in ((-1,"l"),(1,"r")):
            set_rotation(f"arm_{label}",(1,0,0),side*18*factor)
            set_rotation(f"forearm_{label}",(0,1,0),-30*factor)
            set_rotation(f"thruster_{label}",(0,1,0),12*factor)
            if name == "Chaser":
                set_rotation(f"weapon_{label}",(0,1,0),15*factor)
                set_rotation(f"armor_{label}",(1,0,0),side*8*factor)
            else:
                set_rotation(f"wrist_{label}",(0,1,0),10*factor)
                for index in range(1,4):
                    set_rotation(f"claw_{label}_{index:02d}",(0,0,1),(index-2)*18*factor)
        if name == "Warden":
            # 护板绕外侧铰链向前翻开；负向帧用较小开合值，避免闭合穿入核心。
            opening = abs(factor)
            set_rotation("shutter_top",(0,1,0),-48*opening)
            set_rotation("shutter_bottom",(0,1,0),48*opening)
            set_rotation("shutter_left",(0,0,1),-48*opening)
            set_rotation("shutter_right",(0,0,1),48*opening)
            set_rotation("halo",(1,0,0),8*factor)
        for joint in RIG.pose.bones:
            # 插值只生成诊断序列；位置/比例通道也烘焙，以便检测多余根位移或 100 倍缩放。
            joint.keyframe_insert(data_path="rotation_quaternion",frame=frame,group=joint.name)
            joint.keyframe_insert(data_path="location",frame=frame,group=joint.name)
            joint.keyframe_insert(data_path="scale",frame=frame,group=joint.name)
    bpy.context.scene.frame_start = 1
    bpy.context.scene.frame_end = 97
    bpy.context.scene.render.fps = 24
    bpy.context.scene.frame_set(1)
    return action


@traced
def export(entry):
    """entry 为型号/挂点定义；独立合并副本输出骨骼 FBX 和诊断动作 FBX，源零件与修改器不破坏。"""
    bpy.context.scene.frame_set(1)
    bpy.ops.object.select_all(action="DESELECT")
    copies = []  # 导出副本的父子关系只在本次函数内存在，结束删除。
    for original in PARTS:
        obj = original.copy()
        obj.data = original.data.copy()
        bpy.context.scene.collection.objects.link(obj)
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        for modifier in list(obj.modifiers):
            if modifier.type == "ARMATURE": obj.modifiers.remove(modifier)
            else: bpy.ops.object.modifier_apply(modifier=modifier.name)
        copies.append(obj)
        obj.select_set(False)
    for obj in copies: obj.select_set(True)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    joined = bpy.context.object  # 导出统一网格，材质槽和所有命名权重组保留。
    joined.name = entry["mesh"]
    bpy.context.scene.cursor.location = (0,0,0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.008)
    bpy.ops.object.mode_set(mode="OBJECT")
    modifier = joined.modifiers.new("Export skin","ARMATURE")
    modifier.object = RIG
    joined.data.calc_loop_triangles()
    entry["triangles"] = len(joined.data.loop_triangles)
    # 包围盒转回逻辑 UE 厘米；manifest 与 UE 实际导入边界/骨轴逐项比较。
    vertices = [(vertex.co.x,-vertex.co.y,vertex.co.z) for vertex in joined.data.vertices]
    entry["bounds_cm"] = [[min(value[axis] for value in vertices) for axis in range(3)],
                          [max(value[axis] for value in vertices) for axis in range(3)]]
    entry["bones"] = [{"name":item["name"],"parent":item["parent"],"head_cm":[value*100 for value in item["head"]]} for item in BONES]
    entry["parts"] = len(PARTS)
    # Socket 附在正确骨上，并保存局部平移/旋转；UE 侧通过骨参考姿势检查转换，而非假设骨轴同世界轴。
    sockets = {}
    for name,(parent,location) in entry["sockets"].items():
        transform = RIG.data.bones[parent].matrix_local.inverted()
        local = transform @ point(location)
        sockets[name] = {"bone":parent,"world_cm":[v*100 for v in location],"blender_bone_local_cm":list(local)}
    entry["sockets"] = sockets
    RIG.select_set(True)
    bpy.context.view_layer.objects.active = RIG
    bpy.ops.export_scene.fbx(filepath=str(OUT/"FBX"/f"{entry['mesh']}.fbx"),use_selection=True,
        object_types={"MESH","ARMATURE"},axis_forward="-Y",axis_up="Z",apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS",add_leaf_bones=False,use_armature_deform_only=True,
        bake_anim=False,use_mesh_modifiers=True,use_triangles=True,mesh_smooth_type="FACE")
    bpy.data.objects.remove(joined,do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT")
    RIG.select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(OUT/"FBX"/f"A_Breach_{entry['name']}_RigCheck.fbx"),use_selection=True,
        object_types={"ARMATURE"},axis_forward="-Y",axis_up="Z",apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_UNITS",add_leaf_bones=False,use_armature_deform_only=True,
        bake_anim=True,bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_step=1,
        bake_anim_simplify_factor=0)
    # 诊断严格检查 root 不动；所有金属零件每顶点恰有一个骨权重。
    for frame in (1,25,49,73,97):
        bpy.context.scene.frame_set(frame)
        if RIG.pose.bones["root"].location.length > 1e-6 or abs(RIG.pose.bones["root"].rotation_quaternion.w-1)>1e-6:
            raise RuntimeError("Diagnostic action moves root")
    bpy.context.scene.frame_set(1)
    print(f"ENEMY_EXPORT_VALID {entry['name']} triangles={entry['triangles']} bones={len(BONES)}",flush=True)


@traced
def studio(entry):
    """entry 提供实模包围盒；工作台渲染与武器预览一致，输出中立、关节展开和独立骨架视图。"""
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 1440
    scene.render.resolution_y = 1200
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.world = bpy.data.worlds.new("EnemyStudio")
    scene.world.color = (.046,.063,.078)
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "BOTH"
    scene.display.shading.background_type = "WORLD"
    scene.view_settings.view_transform = "Standard"
    # 相机从逻辑前方 +X 与右方 +Y 观察；neutral/pose/rig 三图保持同一机位便于比较。
    bounds = entry["bounds_cm"]
    center = Vector(((bounds[0][0]+bounds[1][0])/2,0,(bounds[0][2]+bounds[1][2])/2))
    span = max(bounds[1][1]-bounds[0][1],(bounds[1][2]-bounds[0][2])*1.2)
    bpy.ops.object.camera_add(location=center+Vector((span*2.8,-span*1.35,span*.8)))
    camera = bpy.context.object
    camera.name = "PreviewCamera"
    camera.rotation_euler = (center-camera.location).to_track_quat("-Z","Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = span*1.22
    camera.data.clip_end = 10000
    scene.camera = camera
    for frame,suffix in ((1,""),(25,"_RigCheck")):
        scene.frame_set(frame)
        scene.render.filepath = str(OUT/"Previews"/f"{entry['name']}{suffix}.png")
        bpy.ops.render.render(write_still=True)
    scene.frame_set(1)
    # 骨架图用独立几何线段表示真实骨位置；辅助对象不参与 FBX、不写入模型权重。
    markers = []
    for item in BONES:
        a,b = point(item["head"]),point(item["tail"])
        bpy.ops.mesh.primitive_cylinder_add(vertices=8,radius=span*.004,depth=(b-a).length,location=(a+b)/2)
        obj = bpy.context.object
        obj.name = "QA_Bone_"+item["name"]
        obj.rotation_euler = (b-a).to_track_quat("Z","Y").to_euler()
        obj.data.materials.append(MATS["Amber"] if item["parent"] else MATS["Armor"])
        markers.append(obj)
        bpy.ops.mesh.primitive_uv_sphere_add(segments=12,ring_count=6,radius=span*.010,location=a)
        obj = bpy.context.object
        obj.name = "QA_Joint_"+item["name"]
        obj.data.materials.append(MATS["Steel"])
        markers.append(obj)
    for obj in PARTS: obj.hide_render = True
    scene.render.filepath = str(OUT/"Previews"/f"{entry['name']}_Skeleton.png")
    bpy.ops.render.render(write_still=True)
    for obj in markers: bpy.data.objects.remove(obj,do_unlink=True)
    for obj in PARTS: obj.hide_render = False
    bpy.ops.object.select_all(action="DESELECT")
    RIG.select_set(True)
    bpy.context.view_layer.objects.active = RIG
    # 打开文件时直接看到骨架和主体，滚轮尺度以厘米场景为准。
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type == "VIEW_3D":
                area.spaces.active.region_3d.view_distance = span*2
                area.spaces.active.region_3d.view_location = center
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/f"Breach_{entry['name']}_Rigged.blend"))


@traced
def main():
    """独立后台入口：分别保存两套可编辑骨骼源、FBX、预览和诊断清单；不操作用户已打开的 Blender 场景。"""
    global PARTS,BONES,MATS,RIG,CURRENT_BONE
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/"FBX").mkdir(exist_ok=True)
    (OUT/"Previews").mkdir(exist_ok=True)
    entries = []  # 只序列化资产与验证数据，Blender 对象不跨 factory reset 保存。
    for name in ("Chaser","Warden"):
        bpy.ops.wm.read_factory_settings(use_empty=True)
        PARTS,BONES,MATS,RIG = [],[],{},None
        CURRENT_BONE = "body"
        bpy.context.scene.unit_settings.system = "METRIC"
        bpy.context.scene.unit_settings.scale_length = .01
        for key,(color,metallic,roughness) in PALETTE.items():
            material = bpy.data.materials.new("M_Enemy_"+key)
            material.diffuse_color = color
            material.use_nodes = True
            shader = material.node_tree.nodes.get("Principled BSDF")
            shader.inputs["Base Color"].default_value = color
            shader.inputs["Metallic"].default_value = metallic
            shader.inputs["Roughness"].default_value = roughness
            if key == "Amber":
                shader.inputs["Emission Color"].default_value = color
                shader.inputs["Emission Strength"].default_value = 2
            MATS[key] = material
        entry = chaser() if name=="Chaser" else warden()
        create_rig(name)
        diagnostic_action(name)
        export(entry)
        studio(entry)
        entries.append(entry)
    (OUT/"enemy_rig_manifest.json").write_text(json.dumps({"version":1,"units":"centimetres","palette":PALETTE,"enemies":entries},indent=2),encoding="utf-8")
    print("BREACH_ENEMIES_BUILD_SUCCESS "+json.dumps([{key:item[key] for key in ("name","parts","triangles")} for item in entries]),flush=True)


if __name__ == "__main__":
    main()
