#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "DemoEnemyPresentation.generated.h"

/** 敌人表现协调器：持有资产、向 GAS 请求动作并仲裁优先级；没有伤害/奖励权限。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoEnemyPresentation : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 加载两套硬引用外观契约，确保打包包含其动画资源。 */
    UDemoEnemyPresentation();
    /** 出生/重配时bBoss选择骨架，Mesh由敌人持有；刷新ASC并验证受击配置。返回动画实例是否有效，碰撞失败独立禁查询并报错。 */
    bool Configure(bool bBoss,class USkeletalMeshComponent* Mesh);
    /** Action 为配置键，Seconds>0 时按指定时长缩放播放；低优先级不打断施法，死亡可抢占。 */
    bool Play(FName Action,float Seconds=0.f);
    /** 死亡/换关/卸载撤销表现 GA；不取消俯冲等玩法 GA。 */
    void Cancel();
    /** 本地转向与延迟二阶段表现；DeltaTime为秒，其余为引擎 Tick 上下文，均不跨帧保存。 */
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    /** EndPlayReason 为引擎卸载原因；销毁前精确取消表现任务。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** GAS 激活时同步借用待播资产/速率，不返回跨 World 的裸 Actor。 */
    class UAnimMontage* GetRequestedMontage() const;
    float GetRequestedRate() const;
    /** 只读网格供回归检查，所有权仍归 Enemy。 */
    class USkeletalMeshComponent* GetMesh() const;
private:
    /** 控制台Demo.Combat.DebugHitZones开启时画当前骨骼碰撞凸体；仅调试表现，不参与命中判断。 */
    void DrawHitZones() const;
    // 和武器共享的Cook配置；用于初始化全骨校验及调试配色，运行中只读。
    UPROPERTY() TObjectPtr<class UDemoEnemyHitProfile> HitProfile;
    // 两套共享只读配置归资产系统拥有；强引用用于加载与烹饪依赖。
    UPROPERTY() TObjectPtr<class UDemoEnemyAppearance> Drone;
    UPROPERTY() TObjectPtr<class UDemoEnemyAppearance> Warden;
    UPROPERTY() TObjectPtr<class UDemoEnemyAppearance> Appearance;
    // 借用同 Actor 网格；UPROPERTY 跟踪销毁，不单独创建副网格。
    UPROPERTY() TObjectPtr<class USkeletalMeshComponent> Body;
    // 当前请求在同步 TryActivateAbility 前写入，GA 激活后由 Task 自持 Montage。
    UPROPERTY() TObjectPtr<class UAnimMontage> RequestedMontage;
    float RequestedRate=1.f; // 有限正倍率；Seconds=0 使用原动作时长。
    FGameplayAbilitySpecHandle AnimationAbility; // 自身 ASC 的表现专用 GA，不持有其他技能。
    int32 Priority=0; // 10受击，20二阶段，40普通攻击，60施法，100死亡。
    bool bRageQueued=false; // 低优先级二阶段动作等到攻击结束，仅本次 Actor 有效。
};
