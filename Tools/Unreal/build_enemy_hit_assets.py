"""UE5.4 命令行生成骨骼分区受击 PhysicsAsset；只改专属物理资产及两网格的 PhysicsAsset 引用。"""
import json
from pathlib import Path
import unreal

# 几何清单由 Blender 源零件生成，已保持厘米和 UE 模型参考空间；物理资产不存放游戏数值。
ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "Art/Enemies/Models/enemy_hit_geometry.json"
DEST = "/Game/Enemies/Breach"


def main():
    """完整生成或 -BreachValidateHitOnly 读盘检查；由主代理在 Editor 编译成功后运行。"""
    unreal.log("[CALL] main build_enemy_hit_assets")
    # validate_only 为新进程只读模式，确认保存引用不依赖前一进程内存状态。
    validate_only = "-BreachValidateHitOnly" in unreal.SystemLibrary.get_command_line()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))  # 当前磁盘清单，不隐式改模型或动画。
    for entry in manifest["models"]:
        # mesh 为 Editor 借用资产，名字由清单限定到本项目两套已交付模型。
        mesh = unreal.load_asset(f"{DEST}/Meshes/{entry['mesh']}")
        if not isinstance(mesh, unreal.SkeletalMesh):
            raise RuntimeError(f"Missing SkeletalMesh {entry['mesh']}")
        if not validate_only:
            # asset 保存后再保存网格引用，任一步失败不宣称生成成功。
            asset = unreal.DemoEnemyHitAssetLibrary.build_enemy_hit_asset(
                mesh, str(MANIFEST), entry["name"], f"{DEST}/Physics/PHYS_Breach_{entry['name']}")
            if not asset:
                raise RuntimeError(f"PhysicsAsset generation failed {entry['name']}")
            for target in (asset, mesh):  # 各自由 Editor 持有；必须保存关联的两个包。
                if not unreal.EditorAssetLibrary.save_loaded_asset(target, only_if_is_dirty=False):
                    raise RuntimeError(f"Save failed {target.get_path_name()}")
        if not unreal.DemoEnemyHitAssetLibrary.validate_enemy_hit_asset(mesh):
            raise RuntimeError(f"PhysicsAsset validation failed {entry['name']}")
    unreal.log("BREACH_HIT_ASSETS_VALIDATE_SUCCESS" if validate_only else "BREACH_HIT_ASSETS_BUILD_SUCCESS")


if __name__ == "__main__":
    main()
