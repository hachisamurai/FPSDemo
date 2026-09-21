"""导出模板手臂参考与握持姿势，用于离线制作；不修改模板资源或关卡。"""
import json
from pathlib import Path
import unreal

# 本工具独占的美术参考目录；工程根由脚本位置解析，不依赖Editor工作目录。
OUTPUT = Path(__file__).resolve().parents[2] / "Art/Player/Animations/Reference"


def transform_data(value):
    """value为本帧借用Transform；返回厘米位置、四元数和缩放的JSON值副本。"""
    unreal.log("[CALL] FPReference.transform_data")
    return {"translation": [value.translation.x, value.translation.y, value.translation.z],
            "rotation": [value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w],
            "scale": [value.scale3d.x, value.scale3d.y, value.scale3d.z]}


def main():
    """Editor-Cmd同步导出入口；临时Actor在finally销毁，不保存当前世界。"""
    unreal.log("[CALL] FPReference.main")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    unreal.load_module("AnimationBlueprintLibrary")
    # mesh/idle仅借用模板资产；导出不会更换Skeleton或重导入源文件。
    mesh = unreal.load_asset("/Game/FirstPersonArms/Character/Mesh/SK_Mannequin_Arms")
    idle = unreal.load_asset("/Game/FirstPersonArms/Animations/FP_Rifle_Idle")
    # pose为完整骨架在待机首帧的离线值快照，actor仅提供父骨与Socket反射。
    options = unreal.AnimPoseEvaluationOptions(optional_skeletal_mesh=mesh)
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(idle, 0.0, options)
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector())
    component = actor.get_component_by_class(unreal.SkeletalMeshComponent)
    component.set_skinned_asset_and_update(mesh)
    try:
        # bones持久化结构与动画首帧，供制作器校验导出资产的坐标和父子关系。
        bones = []
        for index in range(component.get_num_bones()):
            # name/parent仅用于本轮采样；None表示根骨。
            name = str(component.get_bone_name(index))
            parent = str(component.get_parent_bone(name))
            bones.append({"name": name, "parent": parent,
                          "local": transform_data(unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.LOCAL)),
                          "component": transform_data(unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD))})
        # socket为待机姿势下真实挂点，用来保持新动画的枪械空间兼容。
        socket = unreal.AnimPoseExtensions.get_socket_pose(pose, "GripPoint", unreal.AnimPoseSpaces.WORLD)
        (OUTPUT / "arms_reference.json").write_text(json.dumps({"bones": bones, "grip": transform_data(socket)}, indent=2), encoding="utf-8")
        for asset in (mesh, idle):
            # task/options为一次同步FBX导出对象，禁用弹窗并只写指定参考目录。
            task = unreal.AssetExportTask()
            task.object = asset
            task.filename = str(OUTPUT / (asset.get_name() + ".fbx"))
            task.automated = True
            task.prompt = False
            task.replace_identical = True
            task.options = unreal.FbxExportOption()
            if not unreal.Exporter.run_asset_export_task(task):
                raise RuntimeError(f"Reference export failed: {asset.get_name()}")
    finally:
        unreal.get_editor_subsystem(unreal.EditorActorSubsystem).destroy_actor(actor)
    unreal.log("FP_REFERENCE_EXPORT_SUCCESS")


if __name__ == "__main__":
    main()
