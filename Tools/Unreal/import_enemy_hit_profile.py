"""分区伤害JSON只作为编辑源；导入可Cook DataAsset并严格校验两套骨架，运行时不读取项目JSON。"""
import json
import math
from pathlib import Path
import unreal

# 只允许项目专属配置路径，避免重建脚本写到任意用户资产。
ROOT = Path(__file__).resolve().parents[2]
PATH = "/Game/Data/Combat/DA_EnemyHitZones"


def main():
    """Editor-Cmd导入入口：先检查所有JSON字段，再构造临时对象验证，成功后写持久资产。"""
    unreal.log("[CALL] import_enemy_hit_profile main")
    data = json.loads((ROOT/"Config/Combat/EnemyHitZones.json").read_text(encoding="utf-8")) # 一次读取，不跨运行热更新。
    if data["Version"]!=1 or data["AssetPath"]!=PATH: raise RuntimeError("Unsupported hit profile version/path")
    rules = [] # 临时规则数组，写入之前完成类型/枚举/倍率验证。
    for entry in data["Regions"]:
        value = entry["DamageMultiplier"] # 有限无单位倍率，配置UIClamp不替代源JSON验证。
        if not isinstance(value,(float,int)) or not math.isfinite(value) or value<0 or value>100:
            raise RuntimeError("Invalid regional multiplier")
        rule = unreal.DemoEnemyHitZoneRule() # 反射UStruct值，保存到DataAsset而不是Python对象引用。
        rule.set_editor_property("region",getattr(unreal.DemoEnemyHitRegion,entry["Region"].upper()))
        rule.set_editor_property("damage_multiplier",float(value))
        rule.set_editor_property("bone_names",entry["BoneNames"])
        rules.append(rule)
    candidate = unreal.new_object(unreal.DemoEnemyHitProfile) # 临时配置允许失败而不改已保存资产。
    candidate.set_editor_property("regions",rules)
    candidate.set_editor_property("non_hittable_bones",data["NonHittableBones"])
    if not candidate.validate(): raise RuntimeError("Invalid or overlapping hit rules")
    for model in ("Chaser","Warden"):
        mesh = unreal.load_asset(f"/Game/Enemies/Breach/Meshes/SK_Breach_{model}") # 已建骨架引用，仅校验不重导入。
        if not candidate.validate_skeleton(mesh): raise RuntimeError(f"Unmapped bones in {model}")
    asset = unreal.load_asset(PATH) # 幂等更新专用资产；不存在时由工厂创建正确派生类。
    if not asset:
        factory = unreal.DataAssetFactory() # 同步Editor创建对象，工厂不存入资产。
        factory.set_editor_property("data_asset_class",unreal.DemoEnemyHitProfile)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DA_EnemyHitZones","/Game/Data/Combat",unreal.DemoEnemyHitProfile,factory)
    if not isinstance(asset,unreal.DemoEnemyHitProfile): raise RuntimeError("Hit profile asset type mismatch")
    asset.set_editor_property("regions",rules)
    asset.set_editor_property("non_hittable_bones",data["NonHittableBones"])
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=False): raise RuntimeError("Hit profile save failed")
    unreal.log("DEMO_HIT_PROFILE_IMPORT_SUCCESS")


if __name__=="__main__":
    main()
