#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoProjectileTest.generated.h"

/** -DemoProjectileTest隔离World专项；真实Fire GA、骨骼靶和运动碰撞，不调用伤害入口伪造命中。 */
UCLASS(Transient,NotBlueprintable)
class ADemoProjectileTest : public AActor
{
    GENERATED_BODY()
public:
    /** 开启低频状态机；所有等待都有World秒截止。 */
    ADemoProjectileTest();
    /** DeltaSeconds是帧间秒；只驱动测试时序，子弹仍由生产Movement独立Tick。 */
    virtual void Tick(float DeltaSeconds) override;
    /** EndPlayReason为World结束原因；还原测试借用CDO速度，禁止污染后续同进程World。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    /** Condition是本步断言，Message为非敏感用例名；失败恢复CDO并退出1。 */
    bool Check(bool Condition,const TCHAR* Message);
    /** Delay为World秒，推进一步；全流程另设60秒上限。 */
    void Advance(float Delay);
    /** 恢复临时降低的子弹默认速度；幂等，失败/成功/EndPlay均调用。 */
    void RestoreProjectileDefaults();
    /** Player为当前Avatar，Distance为目标表面距相机cm；返回World持有的真实骨骼敌人。 */
    class ADemoEnemy* SpawnTarget(class ADemoCharacter* Player,float Distance);
    /** Player为借用Avatar，Distance为相机前方cm；生成2cm厚BlockAll墙，由World拥有。 */
    AActor* SpawnWall(class ADemoCharacter* Player,float Distance);
    int32 Step=0; // 当前单World测试步骤，正常游戏不创建此Actor。
    float NextTime=1.f; // 下一步最早World秒，留出初次ASC/Loadout初始化时间。
    bool bFailed=false; // 首次失败停止后续修改。
    TWeakObjectPtr<class ADemoEnemy> Target; // 近方真实骨骼靶，不进入自然关卡注册表。
    TWeakObjectPtr<class ADemoEnemy> RearTarget; // 穿透后方靶，独立生命和骨骼刚体。
    TWeakObjectPtr<AActor> Wall; // 薄墙需跨飞行帧存活，清理后失效。
    TWeakObjectPtr<class ADemoWeaponBase> Pistol; // 发射武器实例，用于验证切枪不读取新装备参数。
    UPROPERTY() TObjectPtr<class ADemoProjectileBase> ProjectileDefaults; // 仅测试借用CDO并强保活，退出前恢复速度。
    float OriginalSpeed=0.f; // CDO速度cm/s原值，0表示尚未修改。
    FVector BodyPoint=FVector::ZeroVector; // 真PhysicsAsset查到的稳定模型空间机身表面点。
    float Before=0.f; // 本步近靶伤前HP，跨异步飞行保持。
    float RearBefore=0.f; // 本步后靶伤前HP，用于穿透时序/墙阻挡。
};
