#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "DemoAmmoComponent.generated.h"

/** Pawn弹药选择；金币/解锁与同槽检查点一起提交，只支持单人权威端。 */
UCLASS(ClassGroup=(Demo),meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoAmmoComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 加载配置目录的硬引用，确保图标和配置随游戏Cook。 */
    UDemoAmmoComponent();
    /** EndPlayReason为Pawn卸载原因；移除PS ASC上的旧装配GE。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** 只读目录，未创建资产时使用原生默认值以便Editor初次生成资产。 */
    const class UDemoAmmoCatalog* GetCatalog() const;
    /** Index为0..3，默认解锁配置与该槽已购集合共同决定权限。 */
    bool IsUnlocked(int32 Index) const;
    /** 只读当前0..3装配，主副武器共用，默认普通弹。 */
    int32 GetSelected() const;
    /** 终端权限拒绝原因；无槽、非Hub、暂停、距离不符均不能交易或装配。 */
    FString GetBlockReason() const;
    /** Index/QuotedCost为确认时固定报价；OutMessage接收成功或失败原因。成功写盘才扣币/解锁。 */
    bool Purchase(int32 Index,int32 QuotedCost,FString& OutMessage);
    /** Index为已解锁类型，保存失败回滚并保留旧GE；OutMessage反馈结果。 */
    bool Equip(int32 Index,FString& OutMessage);
    /** Data为同一检查点值快照；导出/恢复稳定ID，不保存GE句柄。 */
    void Capture(class UDemoRunSave& Data) const;
    void Restore(const class UDemoRunSave& Data);
private:
    /** 根据Selected重建唯一装配GE；ASC初始化后或合法装配成功调用。 */
    void RefreshEffect();
    UPROPERTY(EditDefaultsOnly,Category="Ammo") TObjectPtr<class UDemoAmmoCatalog> Catalog; // Editor生成的单一配置资产。
    UPROPERTY() TArray<FName> Unlocked = {FName(TEXT("normal"))}; // 本槽购买结果，不作为全局武器解锁。
    int32 Selected = 0; // 当前类型，恢复时未知/未解锁回退0。
    FActiveGameplayEffectHandle LoadoutEffect; // PS ASC拥有，组件卸载必须精确移除。
};
