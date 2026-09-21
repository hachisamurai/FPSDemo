#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoEnemyHitAssetLibrary.generated.h"

class USkeletalMesh;
class UPhysicsAsset;

/** 编辑器专用受击资产桥接：源凸体从模型参考空间转换到真实 UE 骨局部空间；不进入运行时模块。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoEnemyHitAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /**
     * 离线生成入口：Mesh 借用既有网格；ManifestFilename 是厘米制 JSON 绝对路径；ModelName 选择清单型号；
     * PackagePath 为 /Game/Enemies/Breach/Physics 下的专属包名。同名幂等更新，成功返回资产并绑定网格，失败 nullptr。
     * 仅创建随骨运动的查询凸体，不生成约束或布娃娃；调用方负责保存两项资产。
     */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Hit Assets")
    static UPhysicsAsset* BuildEnemyHitAsset(USkeletalMesh* Mesh, const FString& ManifestFilename, const FString& ModelName, const FString& PackagePath);

    /**
     * Mesh 为借用资产；检查骨/查询通道/无模拟约束，并同步加载内存派生碰撞，逐个验证 Chaos 凸包、调试三角索引和穿心射线。
     * 不保存或标脏包；用于生成和新进程读盘复核。组件注册、世界过滤与动画跟随仍由玩法世界回归另外验证。
     */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Hit Assets")
    static bool ValidateEnemyHitAsset(USkeletalMesh* Mesh);
};
