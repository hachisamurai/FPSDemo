"""导入四枪骨骼/机械动作/原创声音，烘焙成对手臂动作并建立真实Linked Layer蓝图。"""
import json
import hashlib
from pathlib import Path
import unreal

# 目录明确限定为新动画资产及四个既有武器BP的表现字段；不修改玩法数值和存档。
ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Art/Weapons/Animations"
DEST = "/Game/Weapons/Animations"
MESHES = "/Game/Weapons/Meshes/Breach/Skeletal"
AUDIO = "/Game/Audio/SFX/Weapons/Breach/Reload"


def save(asset):
    """asset由Editor包持有；所有保存失败阻止最终成功标记。"""
    unreal.log(f"[CALL] FPImport.save {asset.get_path_name()}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Save failed {asset.get_path_name()}")


def import_fbx(name, directory, skeleton=None):
    """name是manifest白名单中的模型/动作名；有skeleton时只导入60Hz机械动画。"""
    unreal.log(f"[CALL] FPImport.import_fbx {name}")
    # task/options由本次同步调用拥有，保存由后续材质/挂点校验后执行。
    task = unreal.AssetImportTask()
    task.filename = str(SOURCE / "FBX" / (name + ".fbx"))
    task.destination_name = name
    task.destination_path = directory
    task.automated = True
    task.replace_existing = True
    task.save = False
    options = unreal.FbxImportUI()
    options.automated_import_should_detect_type = False
    options.import_as_skeletal = True
    options.import_mesh = skeleton is None
    options.import_animations = skeleton is not None
    options.import_materials = False
    options.import_textures = False
    options.create_physics_asset = False
    options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION if skeleton else unreal.FBXImportType.FBXIT_SKELETAL_MESH
    if skeleton:
        options.skeleton = skeleton
    # data控制坐标系与采样率；制作源已处理Y镜像，沿用已验证的UE/Blender轴契约。
    data = options.anim_sequence_import_data if skeleton else options.skeletal_mesh_import_data
    data.convert_scene = True
    data.convert_scene_unit = True
    data.force_front_x_axis = False
    data.import_uniform_scale = 1.0
    if skeleton:
        data.set_editor_property("import_bone_tracks", True)
        data.set_editor_property("use_default_sample_rate", False)
        data.set_editor_property("custom_sample_rate", 60)
        options.set_editor_property("override_animation_name", name)
    else:
        data.set_editor_property("update_skeleton_reference_pose", True)
        data.set_editor_property("use_t0_as_ref_pose", False)
        data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
    task.options = options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    asset = unreal.load_asset(f"{directory}/{name}")  # 借用刚导入资产，类型不符不允许继续生成引用。
    if not isinstance(asset, unreal.AnimSequence if skeleton else unreal.SkeletalMesh):
        raise RuntimeError(f"FBX import failed {name}")
    return asset


def import_sounds():
    """原创WAV使用标准SoundFactory，保持短单声道一次性音效；返回稳定事件键映射。"""
    unreal.log("[CALL] FPImport.import_sounds")
    manifest = json.loads((ROOT / "Art/Audio/Reload/reload_audio_manifest.json").read_text(encoding="utf-8"))  # 声学规格真值。
    sounds = {}  # 只在当前导入脚本持有；最终由子AnimBP AnimSet硬引用保活/Cook。
    for entry in manifest:
        # task/factory仅用于指定专属音频目录，不创建全局音频混音规则。
        task = unreal.AssetImportTask()
        task.filename = str(ROOT / "Art/Audio/Reload" / (entry["name"] + ".wav"))
        task.destination_name = entry["name"]
        task.destination_path = AUDIO
        task.automated = True
        task.replace_existing = True
        task.save = False
        task.factory = unreal.SoundFactory()
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        sound = unreal.load_asset(f"{AUDIO}/{entry['name']}")  # 当前SoundWave用于规格验证和事件配置。
        if not isinstance(sound, unreal.SoundWave) or abs(sound.duration - entry["seconds"]) > .002:
            raise RuntimeError(f"Invalid reload audio {entry['name']}")
        sound.set_editor_property("looping", False)
        save(sound)
        sounds[(entry["model"], entry["event"])]=sound
    return sounds


def mesh_setup(entry, mesh):
    """根据清单检查实际骨架与边界，绑定既有材质并创建Muzzle/握持Socket。"""
    unreal.log(f"[CALL] FPImport.mesh_setup {entry['model']}")
    # slots保存模型角色，不假设FBX导入顺序；全部复用现有Breach材质。
    slots = list(mesh.get_editor_property("materials"))
    for slot in slots:
        material = unreal.load_asset(f"/Game/Weapons/Materials/Breach/{slot.material_slot_name}")  # 当前材质资产只增加骨骼用途。
        if not material:
            raise RuntimeError(f"Unknown weapon material {slot.material_slot_name}")
        # 已启用骨骼用途的共享材质只读复用，避免每枪重复保存同包、无谓触发着色器重编译和Windows文件占用。
        if not material.get_editor_property("used_with_skeletal_mesh"):
            unreal.MaterialEditingLibrary.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
            save(material)
        slot.material_interface = material
    mesh.set_editor_property("materials", slots)
    # actor/component仅探测实际导入坐标，不保存或启动玩法World。
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector())
    component = actor.get_component_by_class(unreal.SkeletalMeshComponent)
    component.set_skinned_asset_and_update(mesh)
    try:
        if component.get_num_bones() != len(entry["bones"]):
            raise RuntimeError(f"Unexpected bone count {entry['model']}")
        for bone in entry["bones"]:
            # actual为模型空间厘米坐标；单独验证parent避免轴对了但层级错误。
            actual = component.get_socket_transform(bone["name"], unreal.RelativeTransformSpace.RTS_COMPONENT)
            if str(component.get_parent_bone(bone["name"])) != (bone["parent"] or "None") or (actual.translation - unreal.Vector(*bone["head_cm"])).length() > .2:
                raise RuntimeError(f"Bone import mismatch {entry['model']}/{bone['name']}: {actual}")
        for name, socket in entry["sockets"].items():
            if not unreal.DemoSkeletalAssetLibrary.set_skeleton_socket(mesh, name, socket["bone"], unreal.Transform(location=socket["world_cm"])):
                raise RuntimeError(f"Socket failed {name}")
        # bounds来自渲染几何；枪械无物理碰撞，不以PhysicsAsset代替模型范围。
        bounds = mesh.get_imported_bounds()
        for actual, expected in zip((bounds.origin-bounds.box_extent, bounds.origin+bounds.box_extent), entry["bounds_cm"]):
            if (actual-unreal.Vector(*expected)).length() > .3:
                raise RuntimeError(f"Mesh unit/axis mismatch {entry['model']}: {actual} vs {expected}")
    finally:
        unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(actor)
    save(mesh)
    save(mesh.get_editor_property("skeleton"))


def configure(entry, mesh, clips, sounds, support):
    """给纯配置子类写入一份AnimSet，再迁移武器BP外观字段；数值/解锁/弹药配置原样保留。"""
    unreal.log(f"[CALL] FPImport.configure {entry['model']}")
    model = entry["model"]  # 四枪稳定标识，资源与配置一一对应。
    settings = unreal.DemoWeaponAnimationSet()  # CDO配置副本，不含运行时对象或计时状态。
    # BlueprintReadOnly只禁止运行蓝图写入；离线作者通过EditAnywhere反射字段设置配置。
    settings.set_editor_property("idle_pose", unreal.load_asset(f"{DEST}/Arms/A_FP_{model}_Idle"))
    settings.set_editor_property("fire_montage", unreal.load_asset(f"{DEST}/Montages/AM_FP_{model}_Fire"))
    settings.set_editor_property("support_hand_grip", unreal.Vector(*support))
    settings.set_editor_property("support_hand_ik_alpha", 0.0 if model == "Pistol" else 1.0)
    settings.set_editor_property("magazine_presentation_mesh", None)  # 实际弹匣骨与手臂对齐，不叠加第二个模型。
    for clip in entry["clips"]:
        # pair只有表现资源；秒数仍从武器Config读取，变速使用归一化阶段。
        pair = unreal.DemoReloadAnimationPair()
        pair.set_editor_property("arms_montage", unreal.load_asset(f"{DEST}/Montages/AM_FP_{model}_Reload_{clip['variant']}"))
        pair.set_editor_property("weapon_sequence", clips[clip["variant"]])
        pair.set_editor_property("magazine_detach_phase", clip["stages"]["detach"])
        pair.set_editor_property("magazine_attach_phase", clip["stages"]["attach"])
        events = []  # 动画manifest是声音时刻唯一真值；顺序和存在性在C++再次验证。
        for event in clip["sound_events"]:
            sound_event = unreal.DemoReloadSoundEvent()  # 独立结构体值，生命周期归pair保存。
            sound_event.set_editor_property("normalized_time", event["phase"])
            sound_event.set_editor_property("sound", sounds[(model, event["name"])])
            sound_event.set_editor_property("volume_multiplier", .75)
            events.append(sound_event)
        pair.set_editor_property("sound_events", events)
        settings.set_editor_property("empty_reload" if clip["variant"] == "Empty" else "tactical_reload", pair)
    if not unreal.DemoFPAnimationAuthoring.configure_layer(model, settings):
        raise RuntimeError(f"Cannot configure weapon layer {model}")
    # bp/cdo/config仅同步借用，不生成角色、不修改任何已有运行武器实例。
    bp = unreal.load_asset(f"/Game/Weapons/Blueprints/BP_Weapon_{model}")
    cdo = unreal.get_default_object(bp.generated_class())
    config = cdo.get_editor_property("config")
    config.set_editor_property("static_mesh", None)
    config.set_editor_property("mesh", mesh)
    config.set_editor_property("attach_socket", "ik_hand_gun")
    config.set_editor_property("attach_offset", unreal.Transform())
    config.set_editor_property("weapon_anim_layer_class", unreal.load_asset(f"{DEST}/ABP_FP_{model}").generated_class())
    config.set_editor_property("idle_animation", None)
    config.set_editor_property("fire_animation", None)
    config.set_editor_property("reload_animation", None)
    cdo.set_editor_property("config", config)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    save(bp)


def main():
    """制作完整资产闭包；每步失败抛错，结束标记只在所有图、配置和依赖保存成功后输出。"""
    unreal.log("[CALL] FPImport.main")
    unreal.load_module("SkeletalMeshEditor")
    unreal.load_module("AnimationBlueprintLibrary")
    unreal.load_module("AudioEditor")
    # manifest来源于真实Blender导出；arms引用模板，新增动作写入独立武器目录。
    manifest = json.loads((SOURCE / "weapon_animation_manifest.json").read_text(encoding="utf-8"))
    # 右腕/肘从UE手臂和Breach握把联合标定，制作配置只在此离线读取，游戏不依赖JSON文件。
    pose_profiles = json.loads((ROOT / "Art/Player/Animations/fp_pose_profiles.json").read_text(encoding="utf-8"))
    # 接触配置和轨迹必须来自同一轮Blender制作，防止修改握点后忘记重烘焙而导入旧轨迹。
    contacts_path = ROOT / "Art/Player/Animations/left_hand_contacts.json"
    contacts = json.loads(contacts_path.read_text(encoding="utf-8"))
    contact_tracks = json.loads((ROOT / "Art/Player/Animations/left_hand_contact_tracks.json").read_text(encoding="utf-8"))
    if contact_tracks["profile_sha256"] != hashlib.sha256(contacts_path.read_bytes()).hexdigest() or contact_tracks["mechanical_manifest_sha256"] != hashlib.sha256((SOURCE / "weapon_animation_manifest.json").read_bytes()).hexdigest():
        raise RuntimeError("Left-hand contact tracks are stale; run author_fp_left_hand_contacts.py before import")
    if pose_profiles["version"] != 1 or pose_profiles["units"] != "centimetres":
        raise RuntimeError("Unsupported FP pose profile units/version")
    arms = unreal.load_asset("/Game/FirstPersonArms/Character/Mesh/SK_Mannequin_Arms")
    sounds = import_sounds()
    prepared = []  # 图构建前保留四枪对象供后续统一配置，任一烘焙失败不切换运行BP。
    for entry in manifest["weapons"]:
        mesh = import_fbx(entry["mesh"], MESHES)
        mesh_setup(entry, mesh)
        clips = {}  # 当前枪两种动作，使用独立骨架，不能跨枪混用。
        for clip in entry["clips"]:
            sequence = import_fbx(clip["name"], DEST + "/Mechanical", mesh.get_editor_property("skeleton"))
            if abs(sequence.get_play_length() - clip["seconds"]) > .02:
                raise RuntimeError(f"Mechanical duration mismatch {clip['name']}")
            save(sequence)
            clips[clip["variant"]] = sequence
        # Support接触点同时驱动Idle烘焙与主图支撑IK；更新此处避免换弹结束时被旧腕点拉回弹匣内部。
        support = contacts["weapons"][entry["model"]]["Support"]["wrist_cm"]
        if not unreal.DemoFPSequenceAuthoring.build(entry["model"], arms, mesh, clips, unreal.Vector(*support),
                unreal.Vector(*entry["magazine_grip_cm"]), unreal.Vector(*entry["bolt_grip_cm"]),
                unreal.Vector(*pose_profiles["right_wrist_weapon_cm"]), unreal.Vector(*pose_profiles["right_elbow_from_shoulder_cm"]),
                pose_profiles["sniper_bolt_reach_clearance_cm"], entry["clips"][0]["seconds"]):
            raise RuntimeError(f"Arms bake failed {entry['model']}")
        prepared.append((entry, mesh, clips, support))
    if not unreal.DemoFPAnimationAuthoring.build(arms, unreal.load_asset(f"{DEST}/Arms/A_FP_Pistol_Idle")):
        raise RuntimeError("Linked animation graph build failed")
    for entry, mesh, clips, support in prepared:
        configure(entry, mesh, clips, sounds, support)
    save(arms.get_editor_property("skeleton"))
    for directory in (DEST, MESHES, AUDIO):
        if not unreal.EditorAssetLibrary.save_directory(directory, only_if_is_dirty=False, recursive=True):
            raise RuntimeError(f"Save directory failed {directory}")
    unreal.log("FP_WEAPON_ANIMATION_IMPORT_SUCCESS weapons=4 pairs=8 sounds=21")


if __name__ == "__main__":
    main()
