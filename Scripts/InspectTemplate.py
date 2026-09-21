"""通过 UE Python 只读检查模板蓝图默认组件，避免猜测第一人称姿态。"""
import unreal

unreal.log("InspectTemplate: begin")
# 蓝图类和默认对象只用于读取，不保存或修改 .uasset。
blueprint_class = unreal.EditorAssetLibrary.load_blueprint_class('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
default_character = unreal.get_default_object(blueprint_class)
# 遍历类默认组件，输出姿态/动画引用供原生派生 Pawn 对齐。
components = default_character.get_components_by_class(unreal.SkeletalMeshComponent)
for component in components:
    unreal.log('TEMPLATE_COMPONENT ' + component.get_name() + ' transform=' + str(component.get_relative_transform()))
    unreal.log('TEMPLATE_MESH ' + str(component.get_editor_property('skeletal_mesh_asset')))
    unreal.log('TEMPLATE_ANIM ' + str(component.get_editor_property('anim_class')))
unreal.log("InspectTemplate: complete")
# 材质参数仅只读检查，用于确认原生变色调用实际对应资产中的字段。
material = unreal.load_asset('/Game/LevelPrototyping/Materials/M_Solid')
unreal.log('MATERIAL_PARAMETERS ' + str(unreal.MaterialEditingLibrary.get_vector_parameter_names(material)))
