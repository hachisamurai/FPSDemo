"""UE5.4 动作导入与薄AnimBP/Montage/配置生成；只写专属Animation目录，不改模型参考姿势。"""
import importlib.util
import json
from pathlib import Path
import unreal

# 复用已经验证的FBX轴/单位导入参数；模块不执行其main，因此不会重建骨骼或材质。
ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("enemy_mesh_import",ROOT/"Tools/Unreal/import_breach_enemies.py")
IMPORTER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IMPORTER)
IMPORTER.SOURCE = ROOT/"Art/Enemies/Animations"
DEST = "/Game/Enemies/Breach/Animation"


def validate(clip, asset, mesh):
    """clip清单/asset动作/mesh骨架匹配；验证真实UE采样的根不动、动作有变化和时长。"""
    unreal.log(f"[CALL] validate combat {clip['name']}")
    if abs(asset.get_play_length()-clip["seconds"])>.025: raise RuntimeError("Incorrect clip duration")
    options = unreal.AnimPoseEvaluationOptions()  # 当前网格参考姿势用于完整骨采样，非压缩数据推测。
    options.set_editor_property("optional_skeletal_mesh",mesh)
    reference = None  # 首次采样用于不变root旋转比较。
    moving = []  # body/forearm的实际变换，至少一个采样必须和首帧不同。
    for fraction in (0,.25,.5,.75,1):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(asset,clip["seconds"]*fraction,options)
        root = unreal.AnimPoseExtensions.get_bone_pose(pose,"root",unreal.AnimPoseSpaces.WORLD)
        if root.translation.length()>.05 or (root.scale3d-unreal.Vector(1,1,1)).length()>.01:
            raise RuntimeError("Root motion/scale is forbidden")
        if reference is None: reference=root.rotation
        rotation = root.rotation  # UE5.4 Python未导出Quat.dot，显式四分量点积兼容正负等价四元数。
        if abs(abs(rotation.x*reference.x+rotation.y*reference.y+rotation.z*reference.z+rotation.w*reference.w)-1)>.001:
            raise RuntimeError("Root rotation drift")
        moving.append(str(unreal.AnimPoseExtensions.get_bone_pose(pose,"forearm_l",unreal.AnimPoseSpaces.WORLD)))
    if len(set(moving))<2 and clip["kind"] not in ("Dive",): raise RuntimeError(f"Static clip {clip['name']}")
    unreal.log(f"ENEMY_COMBAT_CLIP_VALID {clip['name']} duration={asset.get_play_length()}")


def main():
    """Editor-Cmd入口；导入后使用原生建图桥接并保存依赖，任何异常禁止输出成功标记。"""
    unreal.log("[CALL] import combat main")
    unreal.load_module("AnimationBlueprintLibrary")
    unreal.load_module("AnimGraph")
    manifest = json.loads((IMPORTER.SOURCE/"combat_manifest.json").read_text(encoding="utf-8"))
    palette = json.loads((ROOT/"Art/Enemies/Models/enemy_rig_manifest.json").read_text(encoding="utf-8"))["palette"] # 原模型调色板是共享材质真值。
    IMPORTER.material("Amber",palette["Amber"]) # 新增EnemyTint实例参数，保留远程/侧翼/二阶段辨识。
    for model,skeleton_name in (("Chaser","Drone"),("Warden","Warden")):
        mesh = unreal.load_asset(f"/Game/Enemies/Breach/Meshes/SK_Breach_{model}")  # 已验证骨骼网格，绝不重导入。
        skeleton = unreal.load_asset(f"/Game/Enemies/Breach/Skeletons/SKEL_Breach_{skeleton_name}")
        clips = {}  # 当前骨架动作映射，原生桥接再次检查骨架兼容。
        for clip in manifest:
            if clip["model"]!=model: continue
            asset = IMPORTER.import_fbx(clip["name"]+".fbx",clip["name"],DEST+"/Sequences",skeleton,True)
            IMPORTER.save(asset)
            validate(clip,asset,mesh)
            clips[clip["kind"]]=asset
        if not unreal.DemoEnemyAnimationAuthoring.build(model,mesh,clips): raise RuntimeError(f"AnimBP build failed {model}")
        IMPORTER.save(skeleton)
    if not unreal.EditorAssetLibrary.save_directory(DEST,only_if_is_dirty=False,recursive=True): raise RuntimeError("Animation save failed")
    unreal.log("BREACH_COMBAT_IMPORT_SUCCESS")


if __name__=="__main__":
    main()
