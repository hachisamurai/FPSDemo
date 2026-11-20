// 职责：武器身份/资源目录与项目配置。用法：由DemoWeaponCatalog解析；软加载目录通过原生强引用保活，不在配置CDO缓存反射硬引用。
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/StrongObjectPtr.h" // 原生引用器追踪加载后的资产，兼容打包时DisregardForGC的配置CDO。
#include "DemoWeaponDefinition.generated.h"

class ADemoWeaponBase;

/** 武器身份/资源/解锁定义；射速、伤害等玩法数值仍仅属于Weapon BP的Config。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoWeaponDefinition
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Definition") FName WeaponId; // 存档/挑战稳定键，不以数组顺序或展示文本推导。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Definition") TSoftClassPtr<ADemoWeaponBase> WeaponClass; // 武器BP定义；装备初始化时加载，UI无需生成库存Actor。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Definition", meta=(ClampMin="1", ClampMax="2")) int32 AllowedSlot = 1; // 1主武器、2固定副武器，保持原按键/存档协议。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Definition") FText UnlockDescription; // 展示条件，不作为权限判断依据。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Definition") bool bUnlockedByDefault = false; // 原手枪免通关开放，其他需永久事实。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Definition") TArray<FName> CompletionFacts; // easy/normal/hard/hard_pistol/hell事实白名单；任一已确认事实即可派生权限。
    /** Error输出非法身份/槽/资源说明；目录加载和实例绑定时校验，不读取运行中武器数值。 */
    bool Validate(FString& Error) const;
};

/** 稳定目录的数据资产；原生CDO提供原四枪，无需迁移已有武器BP或生成额外资产。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoWeaponDefinitionCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 构造默认四枪身份和资源，不加载武器Actor、不保存玩家解锁状态。 */
    UDemoWeaponDefinitionCatalog();
    /** Error输出重复ID/非法项；历史四ID必须保持原槽和顺序，新增项只能追加主枪。 */
    bool Validate(FString& Error) const;
    /** WeaponId为稳定键；返回目录拥有的只读定义，调用方需自行复制后跨异步使用。 */
    const FDemoWeaponDefinition* FindDefinition(FName WeaponId) const;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Definitions") TArray<FDemoWeaponDefinition> Definitions; // 有序UI目录；ID才是持久身份，原四项顺序兼容旧索引调用。
};

/** 项目配置只选目录资产；可省略而使用原生四枪，不在GameMode复制目录。 */
UCLASS(Config=Game, DefaultConfig)
class FPSDEMO_API UDemoWeaponDefinitionSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Weapons") TSoftObjectPtr<UDemoWeaponDefinitionCatalog> CatalogAsset; // 可选全项目唯一目录；首次目录访问加载，配置到其他路径时须纳入Cook。
    TStrongObjectPtr<UDemoWeaponDefinitionCatalog> LoadedCatalog; // 非反射FGCObject强引用，随Settings析构释放；不序列化/复制，不保存玩家状态。
    bool bCatalogResolved = false; // 进程内只解析一次配置，避免HUD查询反复加载；修改目录路径后需重启运行进程。
};
