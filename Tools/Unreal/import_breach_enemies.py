"""UE5.4 资产工具：导入两套机械敌人骨骼网格、共享材质及诊断动作；不替换关卡中的玩法敌人。"""
import json
from pathlib import Path
import unreal

# 专属资产路径便于幂等重导入；诊断动画放在 QA 下，避免误当作生产攻击动作。
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Art/Enemies/Models"
DEST = "/Game/Enemies/Breach"


def save(asset):
    """asset 为本次 Editor 拥有的 UObject；检查写包结果，失败不输出成功标记。"""
    unreal.log(f"[EnemyImport] save {asset.get_path_name()}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Save failed {asset.get_path_name()}")


def material(name, values):
    """name/values 来自清单的线性 RGBA、金属度和粗糙度；共享材质始终启用 SkeletalMesh 用途。"""
    unreal.log(f"[EnemyImport] material {name}")
    # asset 仅在专用命名空间重建，运行时无需外部纹理。
    asset = unreal.load_asset(f"{DEST}/Materials/M_Enemy_{name}")
    if not asset:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(f"M_Enemy_{name}",f"{DEST}/Materials",unreal.Material,unreal.MaterialFactoryNew())
    if not isinstance(asset,unreal.Material): raise RuntimeError(f"Material failed {name}")
    # 运行时CDO已硬引用模型，启动时部分表达式可能被Root；按输入节点幂等更新，禁止清空Root对象触发UE断言。
    unreal.MaterialEditingLibrary.set_material_usage(asset,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    # 灯带使用实例参数保留三种战术角色/狂暴辨识；装甲仍共享灰白材质，避免整机染色。
    color = unreal.MaterialEditingLibrary.get_material_property_input_node(asset,unreal.MaterialProperty.MP_BASE_COLOR)
    expected_color = unreal.MaterialExpressionVectorParameter if name=="Amber" else unreal.MaterialExpressionConstant3Vector # 专属材质节点类型契约。
    if not isinstance(color,expected_color):
        color = unreal.MaterialEditingLibrary.create_material_expression(asset,expected_color,-300,0)
    color.set_editor_property("default_value" if name=="Amber" else "constant",unreal.LinearColor(*values[0]))
    if name=="Amber": color.set_editor_property("parameter_name","EnemyTint")
    unreal.MaterialEditingLibrary.connect_material_property(color,"",unreal.MaterialProperty.MP_BASE_COLOR)
    for index,(prop,value) in enumerate(((unreal.MaterialProperty.MP_METALLIC,values[1]),(unreal.MaterialProperty.MP_ROUGHNESS,values[2]))):
        # node 随资产图保存；index 仅布局，不依赖表达式顺序生成材质槽。
        node = unreal.MaterialEditingLibrary.get_material_property_input_node(asset,prop)
        if not isinstance(node,unreal.MaterialExpressionConstant):
            node = unreal.MaterialEditingLibrary.create_material_expression(asset,unreal.MaterialExpressionConstant,-300,130+index*100)
        node.set_editor_property("r",value)
        unreal.MaterialEditingLibrary.connect_material_property(node,"",prop)
    if name == "Amber":
        unreal.MaterialEditingLibrary.connect_material_property(color,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(asset)
    save(asset)
    return asset


def import_fbx(filename, name, folder, skeleton=None, animation=False):
    """filename 为项目内 FBX，name/folder 为专用资产标识，skeleton 为既有骨架；animation 切换只导入诊断动作，网格重导保留受击资产。"""
    unreal.log(f"[EnemyImport] import_fbx {filename} animation={animation}")
    # existing_mesh 只在网格分支借用既有资产；受击PhysicsAsset由另一生成器拥有，不能被FBX默认选项清空。
    existing_mesh = None if animation else unreal.load_asset(f"{folder}/{name}")
    # preserved_physics 显式传回FBX导入器；首次建模尚无受击资产时允许None，后续流程单独创建。
    preserved_physics = existing_mesh.get_editor_property("physics_asset") if isinstance(existing_mesh, unreal.SkeletalMesh) else None
    task = unreal.AssetImportTask()  # 临时自动化任务；只有当前专用路径允许覆盖。
    task.filename = str(SOURCE / "FBX" / filename)
    task.destination_name = name
    task.destination_path = folder
    task.automated = True
    task.replace_existing = True
    task.save = False
    options = unreal.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_as_skeletal = True
    options.import_mesh = not animation
    options.import_animations = animation
    options.import_materials = False
    options.import_textures = False
    options.create_physics_asset = False  # 不自动生成布娃娃；已存在的自定义分区查询资产必须保留。
    if preserved_physics:
        options.set_editor_property("physics_asset", preserved_physics)
        unreal.log(f"ENEMY_HIT_ASSET_PRESERVE mesh={name} physics={preserved_physics.get_path_name()}")
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION if animation else unreal.FBXImportType.FBXIT_SKELETAL_MESH
    if skeleton: options.skeleton = skeleton
    # data 在两个导入分支中均继承 FBX 场景变换参数；单位厘米、导入缩放 1。
    data = options.anim_sequence_import_data if animation else options.skeletal_mesh_import_data
    data.convert_scene = True
    data.convert_scene_unit = True
    data.force_front_x_axis = False
    data.import_uniform_scale = 1
    if animation:
        data.set_editor_property("import_bone_tracks",True)
        data.set_editor_property("use_default_sample_rate",False)
        data.set_editor_property("custom_sample_rate",24)
        options.set_editor_property("override_animation_name",name)
    else:
        # 骨骼 FBX 参数在 UE5.4 部分只提供 Editor 反射属性，必须走 set_editor_property。
        # 专属制作骨架以 Blender 为真值；轴心调整必须同步参考姿势，并重新导入随附 QA 动作。
        data.set_editor_property("update_skeleton_reference_pose",True)
        data.set_editor_property("use_t0_as_ref_pose",False)
        data.set_editor_property("import_morph_targets",False)
        data.set_editor_property("normal_import_method",unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    unreal.log(f"ENEMY_IMPORTED_PATHS {task.imported_object_paths}")
    asset = unreal.load_asset(f"{folder}/{name}")
    expected = unreal.AnimSequence if animation else unreal.SkeletalMesh
    if not isinstance(asset,expected): raise RuntimeError(f"Import/type failure {folder}/{name}")
    if preserved_physics:
        # imported_physics 仅用于导入后断言：不偷偷换回另一份碰撞，失败应让维护者检查导入器/资产契约。
        imported_physics = asset.get_editor_property("physics_asset")
        if not imported_physics or imported_physics.get_path_name() != preserved_physics.get_path_name():
            raise RuntimeError(f"FBX reimport lost preserved PhysicsAsset {name}")
    return asset


def create_probe(mesh):
    """mesh 为已导入骨骼网格；在临时 Editor 世界生成无游戏逻辑探针，调用者必须销毁。"""
    unreal.log(f"[EnemyImport] create_probe {mesh.get_name()}")
    # actor/component 由临时编辑器关卡持有，不保存关卡，也不启动 GI 或敌人行为。
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.SkeletalMeshActor,unreal.Vector())
    component = actor.get_component_by_class(unreal.SkeletalMeshComponent)
    component.set_skinned_asset_and_update(mesh)
    return actor,component


def validate_mesh(entry, mesh, component):
    """entry 为清单、mesh/component 为探针；检查骨名/父子/坐标/缩放与几何包围盒，防止隐藏的轴和单位错误。"""
    unreal.log(f"[EnemyImport] validate_mesh {entry['name']}")
    # root/aim_yaw 仅作为层级父骨，其余交付骨均应真正影响几何；读取 UE LOD0 权重验证 FBX 导入结果。
    if not unreal.DemoSkeletalAssetLibrary.validate_rigid_skin(mesh,len(entry["bones"])-2):
        raise RuntimeError(f"Imported rigid skin invalid {entry['name']}")
    actual_count = component.get_num_bones()  # 完整导入骨数，包括无几何的 root 与控制父骨。
    if actual_count != len(entry["bones"]):
        names = [str(component.get_bone_name(index)) for index in range(actual_count)]
        raise RuntimeError(f"Bone count mismatch {entry['name']} expected={len(entry['bones'])} actual={names}")
    for bone in entry["bones"]:
        if component.get_bone_index(bone["name"]) < 0: raise RuntimeError(f"Missing bone {bone['name']}")
        parent = str(component.get_parent_bone(bone["name"]))
        if parent != (bone["parent"] or "None"): raise RuntimeError(f"Parent mismatch {bone['name']} {parent}")
        transform = component.get_socket_transform(bone["name"],unreal.RelativeTransformSpace.RTS_COMPONENT)
        expected = unreal.Vector(*bone["head_cm"])
        if (transform.translation-expected).length() > .15:
            raise RuntimeError(f"Bone axis/unit mismatch {bone['name']}: {transform.translation} != {expected}")
        if (transform.scale3d-unreal.Vector(1,1,1)).length() > .005:
            raise RuntimeError(f"Non-unit imported bone scale {bone['name']}: {transform.scale3d}")
    # imported_bounds 为FBX原始渲染几何范围；组件有PhysicsAsset后可采用物理包围盒，不能再用组件Bounds验证源网格。
    imported_bounds = mesh.get_imported_bounds()
    origin = imported_bounds.origin  # 网格模型空间包围盒中心，厘米；不依赖探针或当前动画姿势。
    extent = imported_bounds.box_extent  # 原始渲染半尺寸，厘米；包含导出几何而不是简化受击形状。
    bounds = (origin-extent,origin+extent)
    for actual,expected in zip(bounds,entry["bounds_cm"]):
        if (actual-unreal.Vector(*expected)).length() > .30:
            raise RuntimeError(f"Mesh bounds mismatch {entry['name']}: {bounds} vs {entry['bounds_cm']}")
    unreal.log(f"ENEMY_SKELETON_VALID {entry['name']} bones={actual_count} bounds={bounds}")


def sockets(entry, skeleton, component, mesh):
    """按导入后的骨参考变换计算 Socket 局部位置/朝向，避免 Blender/UE 骨轴约定差异。"""
    unreal.log(f"[EnemyImport] sockets {entry['name']}")
    # Skeleton.Sockets 在 Python 中只读受保护；编辑器桥接按真实参考骨架计算相对变换，创建后验证世界位置。
    for name,definition in entry["sockets"].items():
        transform = unreal.Transform(location=definition["world_cm"])
        if not unreal.DemoSkeletalAssetLibrary.set_skeleton_socket(mesh,name,definition["bone"],transform):
            raise RuntimeError(f"Socket creation failed {name}")
        actual = component.get_socket_transform(name,unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        if not component.does_socket_exist(name) or (actual-unreal.Vector(*definition["world_cm"])).length() > .15:
            raise RuntimeError(f"Socket validation failed {name}: {actual}")
    save(skeleton)


def validate_animation(entry, animation, mesh):
    """采样导入后的诊断动作：根始终不动、比例恒等、关节确实变化并回到中立；不验证正式战斗时序。"""
    unreal.log(f"[EnemyImport] validate_animation {entry['name']}")
    options = unreal.AnimPoseEvaluationOptions()  # 离线 Editor 采样，指定已导入网格作为骨架评估上下文。
    options.optional_skeletal_mesh = mesh
    # poses 来自 0/1/2/3/4 秒；创建过程不依赖游戏 Tick 或屏幕可见性。
    poses = [unreal.AnimPoseExtensions.get_anim_pose_at_time(animation,time,options) for time in (0,1,2,3,4)]
    first_root = unreal.AnimPoseExtensions.get_bone_pose(poses[0],"root",unreal.AnimPoseSpaces.WORLD)
    core_positions = []  # 同一骨架 body 空间的核心轴心，展开检查中应稳定而非绕机体偏心公转。
    for pose in poses:
        root = unreal.AnimPoseExtensions.get_bone_pose(pose,"root",unreal.AnimPoseSpaces.WORLD)
        if root.translation.length() > .05 or (root.scale3d-unreal.Vector(1,1,1)).length() > .005:
            raise RuntimeError(f"Diagnostic root moved/scaled: {root}")
        # 与参考根旋转比较而不是假设根骨坐标轴等于模型轴；FBX 骨方向合法但可能带固定旋转。
        a,b = root.rotation,first_root.rotation
        if abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w) < .99999:
            raise RuntimeError("Diagnostic animation rotates navigation root")
        body = unreal.AnimPoseExtensions.get_bone_pose(pose,"body",unreal.AnimPoseSpaces.WORLD)
        core = unreal.AnimPoseExtensions.get_socket_pose(pose,"CoreFX",unreal.AnimPoseSpaces.WORLD)
        core_positions.append(unreal.MathLibrary.inverse_transform_location(body,core.translation))
    if max((value-core_positions[0]).length() for value in core_positions) > .1:
        raise RuntimeError("CoreFX drifts from actual reactor axis")
    first = unreal.AnimPoseExtensions.get_bone_pose(poses[0],"forearm_l",unreal.AnimPoseSpaces.WORLD)
    moved = unreal.AnimPoseExtensions.get_bone_pose(poses[1],"forearm_l",unreal.AnimPoseSpaces.WORLD)
    last = unreal.AnimPoseExtensions.get_bone_pose(poses[4],"forearm_l",unreal.AnimPoseSpaces.WORLD)
    if (first.translation-moved.translation).length() < 2:
        raise RuntimeError("Imported rig animation is static")
    if (first.translation-last.translation).length() > .1:
        raise RuntimeError("Diagnostic animation does not return to rest")
    unreal.log(f"ENEMY_ANIMATION_VALID {entry['name']} length={animation.sequence_length} forearmTravel={(first.translation-moved.translation).length()}")


def main():
    """完整资产导入/验证入口；任一异常立即终止，不改现有 DemoEnemy、战斗数据表或游戏地图。"""
    unreal.log("[EnemyImport] main")
    unreal.load_module("SkeletalMeshEditor")
    unreal.load_module("AnimationBlueprintLibrary")
    # manifest 为只读源定义，palette 为当前 Editor 已保存材质映射。
    manifest = json.loads((SOURCE/"enemy_rig_manifest.json").read_text(encoding="utf-8"))
    # 独立新进程复核模式只读取已保存资产，确认不是依赖未保存的 Editor 内存状态。
    validate_only = "-BreachValidateOnly" in unreal.SystemLibrary.get_command_line()
    palette = {} if validate_only else {name:material(name,values) for name,values in manifest["palette"].items()}
    for entry in manifest["enemies"]:
        skeleton_path = f"{DEST}/Skeletons/{entry['skeleton']}"
        skeleton = unreal.load_asset(skeleton_path)
        mesh = unreal.load_asset(f"{DEST}/Meshes/{entry['mesh']}") if validate_only else import_fbx(entry["mesh"]+".fbx",entry["mesh"],f"{DEST}/Meshes",skeleton)
        if not isinstance(mesh,unreal.SkeletalMesh): raise RuntimeError(f"Missing mesh {entry['mesh']}")
        skeleton = mesh.get_editor_property("skeleton")
        if not validate_only and skeleton.get_path_name().split(".")[0] != skeleton_path:
            if not unreal.EditorAssetLibrary.rename_asset(skeleton.get_path_name(),skeleton_path):
                raise RuntimeError("Cannot name Skeleton asset")
        # 每个槽按名称绑定，避免 exporter 合并顺序改变时把护板和灯光材质交换。
        slots = list(mesh.get_editor_property("materials"))
        for slot in slots:
            key = str(slot.material_slot_name).removeprefix("M_Enemy_")
            if key not in manifest["palette"]: raise RuntimeError(f"Unknown material slot {key}")
            if validate_only:
                if not slot.material_interface or slot.material_interface.get_name() != f"M_Enemy_{key}":
                    raise RuntimeError(f"Material not persisted {entry['name']} {key}")
            else: slot.material_interface = palette[key]
        if not validate_only: mesh.set_editor_property("materials",slots)
        editor = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
        if (not validate_only and not editor.regenerate_lod(mesh,3,False,False)) or editor.get_lod_count(mesh)!=3:
            raise RuntimeError(f"LOD generation failed {entry['name']}")
        if not validate_only:
            save(mesh)
            save(skeleton)
        actor,component = create_probe(mesh)
        try:
            validate_mesh(entry,mesh,component)
            if not validate_only: sockets(entry,skeleton,component,mesh)
            for name,definition in entry["sockets"].items():
                if not component.does_socket_exist(name): raise RuntimeError(f"Socket not persisted {name}")
                actual = component.get_socket_transform(name,unreal.RelativeTransformSpace.RTS_COMPONENT).translation
                if (actual-unreal.Vector(*definition["world_cm"])).length()>.15: raise RuntimeError(f"Saved Socket mismatch {name}")
        finally:
            unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(actor)
        animation_name = f"A_Breach_{entry['name']}_RigCheck"
        animation = unreal.load_asset(f"{DEST}/QA/{animation_name}") if validate_only else import_fbx(animation_name+".fbx",animation_name,f"{DEST}/QA",skeleton,True)
        if not isinstance(animation,unreal.AnimSequence): raise RuntimeError(f"Missing animation {animation_name}")
        if not validate_only: save(animation)
        validate_animation(entry,animation,mesh)
    unreal.log("BREACH_ENEMIES_VALIDATE_SUCCESS" if validate_only else "BREACH_ENEMIES_IMPORT_SUCCESS")


if __name__ == "__main__":
    main()
