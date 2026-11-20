// 职责：弹药目录拥有的纯视觉样式与防御性解析。使用：发射时冻结AmmoType后取值，不读取当前装配，不参与GE/购买/伤害。
#pragma once

#include "CoreMinimal.h"
#include "DemoAmmoVisualStyle.generated.h"

class APawn;
class UMeshComponent;
class UMaterialInterface;
class UFXSystemAsset;
class UFXSystemComponent;
class UDemoAmmoCatalog;
struct FDemoProjectileConfig;

/** 一个目录项的一份纯视觉数据；复制发射事实只传AmmoType，客户端从射手目录取相同样式并复制到本地实例。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoAmmoVisualStyle
{
    GENERATED_BODY()
    // 线性HDR弹种色，RGB各0..64且至少一项>0；A为1。普通暖白，不占用火焰橙红。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") FLinearColor TracerColor = FLinearColor(10.f,7.f,3.f,1.f);
    // 无单位核心亮度0.1..8，只乘本实例材质CoreIntensity，不改变色相或Gameplay。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual", meta=(ClampMin="0.1",ClampMax="8")) float CoreIntensity = 1.f;
    // 弹种横截面倍率0.25..2，叠乘枪型宽度后最终夹在0.1..50cm，不作用碰撞球。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual", meta=(ClampMin="0.25",ClampMax="2")) float WidthScale = 1.f;
    // 弹种长度倍率0.25..2，叠乘枪型长度后最终不超过1000cm，只覆盖已飞路径。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual", meta=(ClampMin="0.25",ClampMax="2")) float LengthScale = 1.f;
    // 弹种淡出倍率0.25..2，最终夹在0..1秒；只影响独立视觉Actor。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual", meta=(ClampMin="0.25",ClampMax="2")) float FadeScale = 1.f;
    // 材质图案0干净/1余烬断点/2冰晶闪点/3针形；非复制，本弹冻结后不随装配更新。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual", meta=(ClampMin="0",ClampMax="3")) uint8 Shape = 0;
    // 弹体MID线性HDR色，RGB各0..64且至少一项>0；共享材质/BP覆盖不写入。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") FLinearColor MeshTint = FLinearColor(10.f,7.f,3.f,1.f);
    // 可空目录强引用的预留拖尾字段；本批保持空且不消费，不替代现有Config.TrailEffect的Niagara路径。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TObjectPtr<UFXSystemAsset> TrailEffect = nullptr;
    // 可空目录强引用；墙/设施及穿出点的短闪，由独立表现Actor拥有并接受阶段清理。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TObjectPtr<UFXSystemAsset> ImpactEffect = nullptr;
};

/** 每次创建亮条计算出的只读表现值；无UObject、非复制，不允许作为弹速/伤害/碰撞输入。 */
struct FPSDEMO_API FDemoAmmoTracerSettings
{
    FLinearColor Color = FLinearColor(10.f,7.f,3.f,1.f); // 弹种线性HDR色相，弹射增强不改本值。
    float CoreIntensity = 1.f; // 弹种核心强度乘可选弹射增强，无单位。
    float Width = 3.f; // 最终横截面cm，0.1..50，不是物理半径。
    float Length = 120.f; // 最终最大视觉长度cm，1..1000，仍要受本段已飞距离限制。
    float FadeSeconds = .06f; // 最终独立视觉淡出World秒，0..1。
};

namespace DemoAmmoVisuals
{
    /** 仅为目录首次创建/作者脚本与回退提供代码基线，0..3；非法Index返回普通，运行期正常只读目录。 */
    FPSDEMO_API FDemoAmmoVisualStyle Default(uint8 Index = 0);
    /** 检查纯视觉值范围，不调用目录Gameplay Validate，不允许坏外观阻断射击。 */
    FPSDEMO_API bool IsValid(const FDemoAmmoVisualStyle& Style);
    /** Shooter可晚到/为空，AmmoType必须是发射冻结值；无目录/坏配置/越界Warning并返回内置普通，不读Selected。 */
    FPSDEMO_API FDemoAmmoVisualStyle Resolve(const APawn* Shooter, uint8 AmmoType);
    /** 只读目录解析；Catalog可空/CDO，坏项身份/样式/越界均Warning回退普通，供Resolve及无World合同共用。 */
    FPSDEMO_API FDemoAmmoVisualStyle ResolveCatalog(const UDemoAmmoCatalog* Catalog, uint8 AmmoType);
    /** 纯函数组合枪型与冻结弹种；bRicochet只改宽/长/核心/淡出，Config只读，供生产及合同测试共用。 */
    FPSDEMO_API FDemoAmmoTracerSettings TracerSettings(const FDemoProjectileConfig& Config, const FDemoAmmoVisualStyle& Style, bool bRicochet);
    /** 为Mesh每槽创建本实例MID；VisualMaterial可空，优先作为可着色基底，仅修改实例参数，不写BP或共享材质。 */
    FPSDEMO_API void ApplyMesh(UMeshComponent* Mesh, const FDemoAmmoVisualStyle& Style, UMaterialInterface* VisualMaterial = nullptr);
    /** 为本地FX组件设置弹种参数及各槽MID；CoreBoost无单位1..8，支持CPU Cascade与Niagara，不修改系统资产。 */
    FPSDEMO_API void ApplyEffect(UFXSystemComponent* Effect, const FDemoAmmoVisualStyle& Style, float CoreBoost = 1.f);
}
