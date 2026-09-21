"""Blender 后台生成已确认的四把战术武器；米制、+X 枪口、右手握把原点，输出见武器模型文档。"""
import bpy
import bmesh
import functools
import json
import math
from pathlib import Path
from mathutils import Vector, Matrix

# 固定项目输出，不操作用户已打开的 Blender 场景；本脚本仅在独立后台进程运行。
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Art/Weapons/Models"
# 线性色材质定义：颜色、金属度、粗糙度，共享槽位用于 UE 导入后保持同族风格。
PALETTE = {
    "Body": ((.035, .055, .069, 1), .65, .38),
    "Armor": ((.60, .65, .66, 1), .48, .36),
    "Steel": ((.19, .23, .27, 1), .85, .28),
    "Rubber": ((.013, .019, .023, 1), 0, .84),
    "Cyan": ((.10, .78, .71, 1), .25, .29),
    "Glass": ((.025, .19, .22, 1), .78, .17),
    "Bore": ((.004, .006, .009, 1), .1, .88),
}
# 材质由当前 Blender 文件拥有；零件列表只属于正在构建的武器集合。
MATS = {}
PARTS = []
GROUP = None


def traced(function):
    """function 为同步 Blender 操作；包装器保持调用日志，捕获生命周期至后台进程结束。"""
    print(f"[BreachModels] register {function.__name__}", flush=True)

    @functools.wraps(function)
    def wrapped(*args, **kwargs):
        """args/kwargs 只属于本调用；捕获 function 在 Blender 主线程同步执行，无异步对象引用。"""
        print(f"[BreachModels] call {function.__name__}", flush=True)
        return function(*args, **kwargs)
    return wrapped


@traced
def finish(obj, name, material, bevel=.0015, smooth=False):
    """obj 为新网格，name 是可编辑零件名；material 为共享槽，bevel 米，smooth 决定曲面平滑。"""
    obj.name = name
    # previous 是默认集合的借用引用，拷贝列表后迁移以免迭代期间失效。
    for previous in tuple(obj.users_collection):
        previous.objects.unlink(obj)
    GROUP.objects.link(obj)
    obj.data.materials.append(MATS[material])
    # polygon 属于此网格；圆柱用平滑侧面，硬表面仍由倒角加权法线维持平直。
    for polygon in obj.data.polygons:
        polygon.use_smooth = smooth
    if bevel:
        # modifier/normal 随源对象保存；导出副本应用，源场景仍可编辑倒角宽度。
        modifier = obj.modifiers.new("Machined bevel", "BEVEL")
        modifier.width = bevel
        modifier.segments = 2
        normal = obj.modifiers.new("Weighted face normals", "WEIGHTED_NORMAL")
        normal.keep_sharp = True
        normal.weight = 35
    PARTS.append(obj)
    return obj


@traced
def box(name, location, size, material="Body", bevel=.002, tilt=0):
    """name/location/size 定义米制长方形零件，material 选材质，bevel 米，tilt 为绕 Y 的度数。"""
    bpy.ops.mesh.primitive_cube_add(size=1, location=location)
    # obj 为当前进程新对象；先应用比例，保证倒角与导出尺寸使用米。
    obj = bpy.context.object
    obj.dimensions = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.rotation_euler.y = math.radians(tilt)
    return finish(obj, name, material, bevel)


@traced
def profile(name, points, width, material="Body", bevel=.002, y=0):
    """points 为 X/Z 侧面轮廓米坐标，width 沿 Y 挤出厚度，y 为侧板偏移；其余为名称/材质/倒角。"""
    # count/vertices/faces 仅构建期间使用，正反盖面与侧面形成封闭实体。
    count = len(points)
    vertices = [(x, y + side * width / 2, z) for side in (-1, 1) for x, z in points]
    faces = [tuple(range(count - 1, -1, -1)), tuple(range(count, count * 2))]
    for index in range(count):  # index 为轮廓顶点序号，环绕形成侧面。
        faces.append((index, (index + 1) % count, (index + 1) % count + count, index + count))
    return mesh_object(name, vertices, faces, material, bevel)


@traced
def mesh_object(name, vertices, faces, material, bevel=0, smooth=False):
    """vertices/faces 为本次几何缓冲；创建归源集合所有的 name 网格，统一重算法线并设置材质。"""
    # mesh/obj 是 Blender 数据块；bm 是仅用于法线修复的临时拓扑，结束立即释放。
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return finish(obj, name, material, bevel, smooth)


@traced
def cylinder(name, location, radius, depth, material="Steel", axis="X", vertices=24):
    """米制中心/radius/depth；axis 为 X/Y/Z，vertices 为圆周分段数，返回可编辑圆柱零件。"""
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=location)
    # obj 按指定轴旋转；小倒角保持环形边缘而不产生尖锐高光。
    obj = bpy.context.object
    if axis == "X":
        obj.rotation_euler.y = math.pi / 2
    elif axis == "Y":
        obj.rotation_euler.x = math.pi / 2
    return finish(obj, name, material, min(.0007, depth / 5), True)


@traced
def tube(name, x, z, length, outer, inner, material="Steel"):
    """沿 X 的空心枪口/镜筒；x/z 中心、length/outer/inner 均为米，四圈连接保留真实孔洞。"""
    # count 为圆周段数；vertices/faces 是此件临时缓冲，四圈依次为后外/前外/前内/后内。
    count = 32
    vertices = []
    faces = []
    for position, radius in ((x-length/2, outer), (x+length/2, outer), (x+length/2, inner), (x-length/2, inner)):
        # index/angle 只决定圆周采样，X 是武器瞄准轴。
        for index in range(count):
            angle = 2 * math.pi * index / count
            vertices.append((position, math.cos(angle)*radius, z+math.sin(angle)*radius))
    for ring in range(4):  # ring 为相邻圆环索引，最后闭合后端环。
        for index in range(count):
            faces.append((ring*count+index, ring*count+(index+1)%count, ((ring+1)%4)*count+(index+1)%count, ((ring+1)%4)*count+index))
    return mesh_object(name, vertices, faces, material, .0006, True)


@traced
def guard(x=.052, z=.013, scale=1):
    """x/z 是扳机护圈中心米，scale 为无量纲比例；真实镂空而不是画黑色方块。"""
    # segments/vertices/faces 为两个圆角矩形轮廓，沿 Y 挤出后内外壁闭合。
    segments = 12
    vertices = []
    faces = []
    for side in (-1, 1):
        for radius_x, radius_z in ((.037, .027), (.030, .020)):
            for index in range(segments):
                # angle 决定超椭圆采样，次方 .5 产生圆角矩形而非椭圆护圈。
                angle = 2*math.pi*index/segments
                vertices.append((x+math.copysign(abs(math.cos(angle))**.5, math.cos(angle))*radius_x*scale,
                                 side*.009*scale, z+math.copysign(abs(math.sin(angle))**.5, math.sin(angle))*radius_z*scale))
    for index in range(segments):
        # following 为环绕相邻点；四张面分别连接前、后和内外壁。
        following = (index+1)%segments
        faces.extend(((index,following,following+segments,index+segments),
                      (index+2*segments,index+3*segments,following+3*segments,following+2*segments),
                      (index,index+2*segments,following+2*segments,following),
                      (index+segments,following+segments,following+3*segments,index+3*segments)))
    mesh_object("Trigger guard", vertices, faces, "Body", .001)
    profile("Trigger", [(x-.006,z+.023),(x+.002,z+.023),(x+.008,z-.009),(x+.003,z-.017),(x-.004,z-.005)], .008, "Steel", .0005)


@traced
def grip(pistol=False):
    """pistol 切换紧凑握把；握持中心为坐标原点附近，模型不依赖动画骨骼。"""
    # points 为外壳侧轮廓，grip/insert 各自保留为可编辑零件。
    points = [(-.053,.050),(.016,.040),(-.006,-.097),(-.064,-.103),(-.081,-.084)] if pistol else [(-.040,.045),(.008,.029),(-.035,-.092),(-.083,-.070)]
    profile("Grip frame", points, .036 if pistol else .037, "Body")
    for side in (-1,1):  # side 为左右可见面，橡胶嵌件和握纹均贴合倾斜轮廓。
        profile("Grip rubber", [(-.047,.016),(-.003,.012),(-.024,-.078),(-.063,-.071)], .002, "Rubber", .001, side*.019)
        for index in range(6):  # index 为宽槽纹序号，不创建密集几何噪声。
            box("Grip traction", (-.023-index*.003,side*.0205,-.006-index*.010), (.028,.0018,.0018), "Steel", .0003, -12)
    # 长枪握把末端斜向后移，底板随轮廓定位并加厚接合，避免旧位置悬空。
    box("Grip heel",(-.036 if pistol else -.058,0,-.097 if pistol else -.087),(.069,.043,.018),"Body",.002, -7)


@traced
def rail(start, end, z):
    """start/end 是顶部导轨的 X 范围，z 是顶面米高度；齿距保持简洁可读。"""
    box("Rail bed",((start+end)/2,0,z), (end-start,.028,.009),"Body",.001)
    # count 是齿数上限，限制近景细节成本；index/x 是当前齿的位置。
    count = max(2, int((end-start)/.018))
    for index in range(count):
        x = start+(index+.5)*(end-start)/count
        box("Rail tooth",(x,0,z+.006),(.009,.035,.006),"Steel",.0006)


@traced
def screws(xs, y, z):
    """xs 为 X 位置列表，y/z 为侧板位置米；铆钉为非功能性外观细节。"""
    for x in xs:  # x 只在本次生成侧面螺钉时使用。
        cylinder("Recess screw",(x,y,z),.0042,.002,"Steel","Y",12)
        box("Screw inset",(x,y+math.copysign(.0012,y),z),(.004,.001,.0009),"Bore",0)


@traced
def magazine(x, bottom=-.15, curved=False):
    """x 为弹匣顶部中心，bottom 为底部 Z 米，curved 只改变步枪轮廓；不控制实际容量。"""
    # points 描述弹匣机匣接合与底部形状；面板条纹沿相同斜率排列。
    points = [(x-.028,.064),(x+.030,.064),(x+.036,-.045),(x+.069,bottom),(x+.010,bottom-.012),(x-.012,-.053)] if curved else [(x-.032,.065),(x+.032,.065),(x+.029,bottom),(x-.030,bottom)]
    profile("Detachable magazine",points,.045,"Body",.002)
    for side in (-1,1):
        for offset in (-.017,0,.017):
            box("Magazine pressed rib",(x+offset+(.014 if curved else 0),side*.023,(bottom+.035)/2),(.004,.003,.035-bottom),"Steel",.0008,-10 if curved else 0)
    box("Magazine floorplate",(x+(.035 if curved else 0),0,bottom),(.069,.052,.010),"Steel",.0015,-10 if curved else 0)


@traced
def stock(sniper=False, heavy=False):
    """sniper/ heavy 决定托腮板与厚重枪托；后托使用镂空外形避免整块盒子。"""
    cylinder("Stock extension",(-.17,0,.089),.013,.17,"Steel")
    profile("Stock upper",[(-.33,.134),(-.16,.127),(-.15,.093),(-.30,.085)],.040,"Body")
    profile("Stock lower",[(-.33,.016),(-.18,.083),(-.15,.083),(-.30,-.008)],.038,"Body")
    box("Shoulder butt",(-.324,0,.061),(.028,.053,.155),"Body",.005,-5)
    box("Butt pad",(-.341,0,.061),(.012,.057,.146),"Rubber",.003,-5)
    if sniper or heavy:
        profile("Cheek rest",[(-.316,.14),(-.205,.14),(-.18,.113),(-.31,.106)],.048,"Armor",.003)
    else:
        box("Stock latch",(-.212,0,.074),(.059,.042,.012),"Rubber",.002)


@traced
def sights(rear, front, z, scale=1):
    """rear/front 为 X 米位置，z 为导轨高度；scale 缩放瞄具细节，手枪使用低矮 .45 倍。"""
    # first 记录本次零件起点；只缩放新建瞄具，不影响枪身与其他调用的模型。
    first = len(PARTS)
    for x in (rear,front):
        box("Sight foot",(x,0,z+.008),(.021,.038,.016),"Body",.001)
        for side in (-1,1):
            box("Sight wing",(x,side*.014,z+.027),(.010,.006,.031),"Steel",.001,-5)
    box("Front sight post",(front,0,z+.025),(.005,.004,.025),"Body",.0005)
    box("Front cyan dot",(front-.003,0,z+.038),(.001,.003,.003),"Cyan",0)
    for piece in PARTS[first:]:  # piece 在同一同步构建过程内有效；保持前后安装位置不变。
        piece.scale *= scale
        piece.location.y *= scale
        piece.location.z = z+(piece.location.z-z)*scale


@traced
def pistol():
    """构建 26cm 紧凑手枪：套筒、单枪口、护圈与握把保留独立源零件。"""
    grip(True)
    guard(.050,.015,.79)
    profile("Pistol lower",[(-.07,.06),(-.068,.087),(.177,.082),(.162,.042),(.08,.037),(.02,.032)],.036,"Body")
    box("Slide",(.054,0,.107),(.241,.041,.044),"Body",.003)
    tube("Pistol muzzle",.182,.108,.024,.011,.0072)
    cylinder("Barrel interior",(.161,0,.108),.007,.002,"Bore")
    for side in (-1,1):
        profile("Slide armor",[(.012,.092),(.150,.092),(.165,.102),(.156,.126),(.025,.126),(.013,.117)],.0025,"Armor",.001,y=side*.021)
        for index in range(5):
            box("Slide serration",(-.025-index*.007,side*.022,.108),(.003,.003,.032),"Rubber",.0005,-12)
        box("Slide status strip",(-.060,side*.023,.108),(.004,.002,.024),"Cyan",.0004,-10)
        screws([-.036,.117],side*.020,.067)
    box("Ejection port",(.012,-.0218,.119),(.035,.001,.013),"Bore",.001)
    box("Underbarrel rail",(.123,0,.039),(.070,.029,.006),"Steel",.001)
    sights(-.047,.157,.132,.45)
    return {"muzzle_cm":[19.4,0,10.8],"grip_l_cm":[0,0,0],"sight_cm":[-4.7,0,14.5]}


@traced
def long_gun(kind):
    """kind 为 Rifle/Shotgun/Sniper，按概念图构建不同长度、宽度、镜具的同族外观。"""
    # heavy/sniper 控制展示轮廓；front/radius 均为米，不是射程或伤害配置。
    heavy = kind == "Shotgun"
    sniper = kind == "Sniper"
    front = .81 if sniper else .50 if heavy else .47
    radius = .027 if heavy else .010 if sniper else .011
    grip()
    guard()
    stock(sniper,sniper or heavy)
    profile("Receiver lower",[(-.102,.033),(-.099,.109),(.160,.105),(.177,.072),(.134,.030),(.020,.029)], .066 if heavy else .054,"Body",.003)
    box("Upper receiver",(.026,0,.113),(.257,.074 if heavy else .058,.057),"Body",.004)
    for side in (-1,1):
        profile("Receiver armor",[(-.092,.130),(-.075,.145),(.137,.143),(.163,.122),(.151,.087),(.083,.073),(-.074,.085)],.003,"Armor",.0015,side*(.038 if heavy else .030))
        box("Receiver inset",(-.024,side*(.040 if heavy else .032),.115),(.085,.002,.028),"Body",.002)
        box("Power signature",(-.029,side*(.042 if heavy else .034),.119),(.057,.0018,.005),"Cyan",.0006)
        screws([-.083,.071,.132],side*(.041 if heavy else .033),.096)
    box("Ejection recess",(.083,-(.042 if heavy else .034),.116),(.052,.002,.019),"Bore",.002)
    box("Port bolt",(.078,-(.044 if heavy else .036),.119),(.037,.002,.012),"Steel",.001)
    magazine(.112,-.057 if sniper else -.093 if heavy else -.161, kind=="Rifle")
    # hand_end/hand_width 控制护木包覆；狙击护木短于枪管，霰弹明显粗厚。
    hand_end = .48 if sniper else .397 if heavy else .335
    hand_width = .088 if heavy else .066
    box("Handguard spine",((.164+hand_end)/2,0,.103),(hand_end-.164,hand_width,.076 if heavy else .063),"Body",.006)
    for side in (-1,1):
        profile("Handguard plate",[(.170,.126),(hand_end-.014,.126),(hand_end,.111),(hand_end-.008,.080),(.190,.072),(.169,.085)], .004,"Armor",.0015,side*(hand_width/2+.001))
        for index in range(5 if sniper else 4):
            # x 为宽散热槽位置；黑色内壁加深轮廓，槽位不穿透主体以保持封闭网格。
            x = .193+index*(hand_end-.210)/(4 if sniper else 3)
            box("Vent cavity",(x,side*(hand_width/2+.0035),.113),(.029,.0018,.009),"Bore",.002)
            box("Vent lower",(x,side*(hand_width/2+.0035),.088),(.023,.0018,.006),"Bore",.001)
        box("Foregrip pad",((.19+hand_end)/2,side*(hand_width/2+.006),.061),((hand_end-.18)*.75,.008,.024),"Rubber",.003)
    cylinder("Exposed barrel",((hand_end+front)/2,0,.109),radius,front-hand_end+.015,"Steel")
    if heavy:
        tube("Wide muzzle",front-.010,.109,.039,.034,.024)
        cylinder("Deep bore",(front-.030,0,.109),.023,.003,"Bore")
    else:
        tube("Muzzle brake",front-.011,.109,.047,.017,.007)
        cylinder("Bore depth",(front-.033,0,.109),.0068,.002,"Bore")
        for side in (-1,1):
            for index in range(2):
                box("Brake side port",(front-.023+index*.018,side*.016,.109),(.010,.001,.011),"Bore",.002)
    rail(-.10,hand_end,.149)
    if sniper:
        # 各镜环/镜座分别可编辑；物镜朝 +X，目镜靠近肩部，整体不是实心黑盒。
        for x in (-.035,.120):
            box("Scope riser",(x,0,.183),(.025,.038,.065),"Body",.002)
            tube("Scope ring",x,.222,.017,.026,.020,"Body")
        cylinder("Scope central",(.052,0,.222),.022,.274,"Body")
        tube("Scope eyepiece",-.102,.222,.060,.029,.023,"Rubber")
        cylinder("Rear optic",(-.129,0,.222),.023,.001,"Glass")
        tube("Scope objective",.212,.222,.101,.039,.031,"Body")
        cylinder("Front optic",(.257,0,.222),.031,.001,"Glass")
        cylinder("Scope elevation",(.056,0,.256),.017,.027,"Steel","Z")
        cylinder("Scope windage",(.056,-.030,.222),.014,.022,"Steel","Y")
        cylinder("Bolt stem",(-.027,-.061,.092),.006,.055,"Steel","Y")
        # bolt knob 属于视觉操作件，保留独立网格供未来动作拆分。
        bpy.ops.mesh.primitive_uv_sphere_add(segments=16, ring_count=8, radius=.016, location=(-.027,-.091,.090))
        finish(bpy.context.object,"Bolt knob","Body",0,True)
    else:
        sights(-.073,hand_end-.020,.164)
    return {"muzzle_cm":[(front+(.0095 if heavy else .0125))*100,0,10.9],"grip_l_cm":[25,0,5],"sight_cm":[-10,0,22.2 if sniper else 19.0]}


@traced
def export_weapon(name):
    """name 为稳定武器型号；复制零件后应用倒角、合并、UV、轴转换并导出，原始零件不破坏。"""
    bpy.ops.object.select_all(action="DESELECT")
    # copies 是短期导出对象列表；数据独立复制，合并不会影响可编辑源集合。
    copies = []
    for original in PARTS:
        duplicate = original.copy()
        duplicate.data = original.data.copy()
        bpy.context.scene.collection.objects.link(duplicate)
        duplicate.select_set(True)
        copies.append(duplicate)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.convert(target="MESH")
    bpy.ops.object.join()
    # joined 只在 FBX 导出期间存在；原点=右手握持参考点，无导出平移补偿。
    joined = bpy.context.object
    joined.name = f"SM_Breach_{name}"
    bpy.context.scene.cursor.location = (0,0,0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.012)
    bpy.ops.object.mode_set(mode="OBJECT")
    joined.data.calc_loop_triangles()
    # triangles/bounds 为独立验收记录；UE 导入后再次比较尺寸，不把 FBX 成功当作正确比例。
    triangles = len(joined.data.loop_triangles)
    bounds = [[min(v.co[axis] for v in joined.data.vertices)*100 for axis in range(3)],
              [max(v.co[axis] for v in joined.data.vertices)*100 for axis in range(3)]]
    joined.data.transform(Matrix.Diagonal((1,-1,1,1)))
    joined.data.flip_normals()
    bpy.ops.export_scene.fbx(filepath=str(OUT/"FBX"/f"{joined.name}.fbx"),use_selection=True,object_types={"MESH"},
        use_mesh_modifiers=True,bake_anim=False,add_leaf_bones=False,axis_forward="-Y",axis_up="Z",
        apply_unit_scale=True,apply_scale_options="FBX_SCALE_UNITS",use_triangles=True,mesh_smooth_type="FACE")
    bpy.data.objects.remove(joined,do_unlink=True)
    return {"triangles":triangles,"bounds_cm":bounds}


@traced
def render_weapon(name, group, length):
    """name 输出 basename，group 为当前可见源集合，length 为取景宽度米；渲染真实几何而不是概念图。"""
    # scene/camera 为当前后台 Blender 对象；隐藏其他武器集合只影响预览，不改变导出。
    scene = bpy.context.scene
    for other in bpy.data.collections:
        if other.name.startswith("Weapon_"):
            other.hide_render = other != group
    bpy.ops.object.camera_add()
    camera = bpy.context.object
    camera.name = f"Camera_{name}"
    # target 取实际包围盒中心以保持四把枪都完整入镜；相机略偏枪口并高于武器。
    target = Vector((.04 if name=="Pistol" else .23 if name=="Sniper" else .07,0,.04))
    camera.location = target+Vector((length*.50,-length*1.6,length*.60))
    camera.rotation_euler = (target-camera.location).to_track_quat("-Z","Y").to_euler()
    camera.data.type = "ORTHO"
    # 正交 scale 是横向取景；手枪高度接近长度，16:10 画幅需额外空间，避免裁切握把。
    camera.data.ortho_scale = max(length*1.26, .47)
    scene.camera = camera
    scene.render.filepath = str(OUT/"Previews"/f"{name}.png")
    bpy.ops.render.render(write_still=True)


@traced
def main():
    """独立进程入口：生成源模型、四个FBX、真实几何预览及尺寸/枪口清单；不改概念图与玩法数据。"""
    global GROUP, PARTS
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/"FBX").mkdir(exist_ok=True)
    (OUT/"Previews").mkdir(exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    # scene 采用真实米单位；FBX 导出时带单位元数据，UE 以 1x 导入为厘米。
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.scale_length = 1
    for name,(color,metallic,roughness) in PALETTE.items():
        # material/shader 为共享 PBR 材质；美术参数随 .blend 保存，UE 用同一清单重建。
        material = bpy.data.materials.new(f"M_Breach_{name}")
        material.diffuse_color = color
        material.use_nodes = True
        shader = material.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Base Color"].default_value = color
        shader.inputs["Metallic"].default_value = metallic
        shader.inputs["Roughness"].default_value = roughness
        MATS[name] = material
    # entries 只包含可序列化元数据，字段定义随文档维护；不把 Blender 对象放进 JSON。
    entries = []
    for name,length in (("Pistol",.28),("Rifle",.86),("Shotgun",.89),("Sniper",1.19)):
        GROUP = bpy.data.collections.new(f"Weapon_{name}")
        scene.collection.children.link(GROUP)
        PARTS = []
        sockets = pistol() if name=="Pistol" else long_gun(name)
        stats = export_weapon(name)
        entries.append({"name":name,"asset":f"SM_Breach_{name}","preview_width_m":length,**sockets,**stats})
    (OUT/"weapons_manifest.json").write_text(json.dumps({"version":1,"units":"centimetres","palette":PALETTE,"weapons":entries},indent=2),encoding="utf-8")
    # Workbench 使用真实倒角与材质色快速检查几何；不依赖图像生成工具或外部纹理。
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 900
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.world = bpy.data.worlds.new("BreachStudio")
    scene.world.color = (.045,.065,.079)
    scene.display.shading.light = "STUDIO"
    scene.display.shading.studio_light = "paint.sl"
    scene.display.shading.color_type = "MATERIAL"
    scene.display.shading.show_shadows = True
    scene.display.shading.show_cavity = True
    scene.display.shading.cavity_type = "BOTH"
    scene.display.shading.curvature_ridge_factor = 1.25
    scene.display.shading.background_type = "WORLD"
    scene.view_settings.view_transform = "Standard"
    for entry in entries:
        render_weapon(entry["name"],bpy.data.collections[f"Weapon_{entry['name']}"],entry["preview_width_m"])
    # 保存时只显示步枪，避免四把原点重叠；其他集合可在 Outliner 切换显隐进行编辑。
    for group in bpy.data.collections:
        if group.name.startswith("Weapon_"):
            group.hide_viewport = group.name != "Weapon_Rifle"
            group.hide_render = group.name != "Weapon_Rifle"
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/"Breach_Weapons.blend"))
    print("BREACH_WEAPONS_BUILD_SUCCESS "+json.dumps(entries),flush=True)


if __name__ == "__main__":
    main()
