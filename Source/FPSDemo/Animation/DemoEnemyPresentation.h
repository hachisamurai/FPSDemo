// 职责：敌人动画优先级与旧灯带/新全身Overlay短闪。使用：Enemy出生注入Mesh/ASC，已确认命中选择Flash入口；依赖GAS、网格独立MID与Editor候选材质，不控制伤害/移动碰撞。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpec.h"
#include "AI/DemoEnemyDefinition.h"
#include "DemoEnemyPresentation.generated.h"

/** 敌人表现协调器：持有资产、向 GAS 请求动作并仲裁优先级；没有伤害/奖励权限。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoEnemyPresentation : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 创建纯表现更新器；资源及ASC由出生握手注入，不自行选择兵种。 */
    UDemoEnemyPresentation();
    /** Definition为冻结体型，Mesh/ASC由同Actor持有；全部动画和真实受击契约就绪才返回true。 */
    bool Configure(const FDemoEnemyBodyDefinition& Definition,class USkeletalMeshComponent* Mesh,class UAbilitySystemComponent* ASC);
    /** Direction为仲裁器提供的世界朝向；零向量清除转向，表现不反查玩家或攻击组件。 */
    void SetFacingIntent(const FVector& Direction);
    /** bAlive由宿主死亡边沿推送；死亡动作仍可播放，其余请求拒绝。 */
    void SetAlive(bool bAlive);
    /** Action 为配置键，Seconds>0 时按指定时长缩放播放；低优先级不打断施法，死亡可抢占。 */
    bool Play(FName Action,float Seconds=0.f);
    /** bCore/bFatal为服务器已确认区域与致死状态；各相关客户端短闪灯带，不打断施法或移动真实受击网格。 */
    void FlashHit(bool bCore, bool bFatal);
    /** 新方案确认后调用：bCore/bFatal只选择短闪级别；独占空闲Overlay而不改本体/灯带材质，Boss/冰霜/施法时让位，不授予硬直。 */
    void FlashConfirmedImpact(bool bCore, bool bFatal);
    /** 死亡/换关/卸载撤销表现 GA；不取消俯冲等玩法 GA。 */
    void Cancel();
    /** 本地转向、受击闪光与延迟二阶段表现；DeltaTime为秒，其余为引擎 Tick 上下文，均不跨帧保存。 */
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    /** EndPlayReason 为引擎卸载原因；销毁前精确取消表现任务。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** GAS 激活时同步借用待播资产/速率，不返回跨 World 的裸 Actor。 */
    class UAnimMontage* GetRequestedMontage() const;
    float GetRequestedRate() const;
    /** 只读网格供回归检查，所有权仍归 Enemy。 */
    class USkeletalMeshComponent* GetMesh() const;
    // 本机闪光持续秒数，0禁用；不复制逐帧参数，既有Multicast通知开始。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback", meta=(ClampMin="0", ClampMax="0.4")) float HitFlashSeconds = .16f;
    // 模型可按材质槽替换发光区域，默认只改灯带，保留Boss红色技能预警与装甲辨识。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback") FName HitFlashSlot = TEXT("M_Enemy_Amber");
    // 发光材质必须已有此向量参数；缺槽/缺参数仅跳过短闪，不生成或改写材质资产。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback") FName HitFlashParameter = TEXT("EnemyTint");
    // Editor创建的候选Overlay硬引用，参与Cook；缺失时仅普通小怪降级旧灯带，开火/受击路径不加载资源。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback|Confirmed") TObjectPtr<class UMaterialInterface> ConfirmedOverlayMaterial;
    // 2026-11-19用户决定加强身体闪光：普通80ms，头部100ms/致死120ms保持；纯表现不改GAS时钟。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback|Confirmed", meta=(ClampMin="0", ClampMax="0.3")) float ConfirmedHitSeconds = .08f;
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback|Confirmed", meta=(ClampMin="0", ClampMax="0.3")) float ConfirmedCoreSeconds = .10f;
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback|Confirmed", meta=(ClampMin="0", ClampMax="0.3")) float ConfirmedFatalSeconds = .12f;
    // 2026-11-19用户决定普通0.25/头部0.30/致死0.30；总上限仍0.3，Boss/元素/施法/外部Overlay保护不变。
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback|Confirmed", meta=(ClampMin="0", ClampMax="0.3")) float ConfirmedHitOpacity = .25f;
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback|Confirmed", meta=(ClampMin="0", ClampMax="0.3")) float ConfirmedCoreOpacity = .30f;
    UPROPERTY(EditDefaultsOnly, Category="Demo|Hit Feedback|Confirmed", meta=(ClampMin="0", ClampMax="0.3")) float ConfirmedFatalOpacity = .30f;
private:
    /** DeltaSeconds为本帧秒数；恢复最新角色/狂暴颜色，死亡尸体也完成淡出，DedicatedServer不执行。 */
    void UpdateHitFlash(float DeltaSeconds);
    /** DeltaSeconds为World帧秒；只推进本组件已占用Overlay，外部接管/冻结/施法时立即归还；死亡仍完成短尾。 */
    void UpdateConfirmedImpactFlash(float DeltaSeconds);
    /** 仅首次确认调用创建至多一个MID；检查真实FeelColor/FeelOpacity参数，缺资源返回false，不生成或改写材质资产。 */
    bool EnsureConfirmedOverlayMaterial();
    /** 只恢复仍由本组件占用的Overlay并清自己的强度；外部已替换时保持其Overlay不变，Configure/旧模式/卸载均调用。 */
    void StopConfirmedImpactFlash();
    /** 只读宿主Boss身份与ASC元素/施法Tag；受保护时新短闪必须让位，不反向控制任何玩法状态。 */
    bool IsConfirmedImpactProtected() const;
    /** 控制台Demo.Combat.DebugHitZones开启时画当前骨骼碰撞凸体；仅调试表现，不参与命中判断。 */
    void DrawHitZones() const;
    // 和武器共享的Cook配置；用于初始化全骨校验及调试配色，运行中只读。
    UPROPERTY() TObjectPtr<class UDemoEnemyHitProfile> HitProfile;
    // 注入的只读外观引用，不持有或查询战斗决策对象。
    UPROPERTY() TObjectPtr<class UDemoEnemyAppearance> Appearance;
    // 借用同 Actor 网格；UPROPERTY 跟踪销毁，不单独创建副网格。
    UPROPERTY() TObjectPtr<class USkeletalMeshComponent> Body;
    TWeakObjectPtr<class UAbilitySystemComponent> AbilitySystem; // 宿主ASC弱引用；卸载时不延长Actor生命。
    FVector FacingIntent=FVector::ZeroVector; // 本帧世界方向，来源为战斗组件，零表示不转向。
    bool bOwnerAlive=true; // 死亡边沿由宿主推送，避免表现反向依赖Enemy实现。
    // 当前请求在同步 TryActivateAbility 前写入，GA 激活后由 Task 自持 Montage。
    UPROPERTY() TObjectPtr<class UAnimMontage> RequestedMontage;
    float RequestedRate=1.f; // 有限正倍率；Seconds=0 使用原动作时长。
    FGameplayAbilitySpecHandle AnimationAbility; // 自身 ASC 的表现专用 GA，不持有其他技能。
    int32 Priority=0; // 10受击，20二阶段，40普通攻击，60施法，100死亡。
    bool bRageQueued=false; // 低优先级二阶段动作等到攻击结束，仅本次 Actor 有效。
    TWeakObjectPtr<class UMaterialInstanceDynamic> FlashMaterial; // 网格持有MID，本组件弱跟踪，复制替换材质时重新取当前实例。
    FLinearColor FlashBaseTint = FLinearColor::White; // 当前角色/狂暴基色，外部改色后更新，不能把瞬时HDR写成永久角色颜色。
    FLinearColor FlashAppliedTint = FLinearColor::White; // 本组件上次写入值，用于识别其他系统对同一参数的修改。
    FLinearColor FlashPeakTint = FLinearColor::White; // 当前命中冻结的HDR短闪颜色，普通白/核心金/致死暖红。
    float FlashRemaining = 0.f; // 剩余World秒，0时无需读写材质，不进入存档或网络状态。
    float FlashDuration = .16f; // 本次起点冻结时长，运行时修改默认配置不会导致除零。
    UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> ConfirmedOverlayMID; // 本组件拥有、每次Configure最多一个，停闪不销毁以复用；与原材质槽完全独立。
    UPROPERTY(Transient) TObjectPtr<class UMaterialInterface> PreviousOverlayMaterial; // 占用之前的原始Overlay引用，只有仍占用时恢复；本批只在原Overlay为空时取得租约。
    float ConfirmedFlashRemaining = 0.f; // 剩余World秒，0时不读取Overlay或Tag，不进入存档/网络。
    float ConfirmedFlashDuration = .08f; // 本次脉冲冻结总秒数，升级时重新取候选档位。
    float ConfirmedFlashPeakOpacity = 0.f; // 本次冻结透明度，低于0.3，避免临时配置修改导致强度跳变。
    int32 ConfirmedFlashPriority = 0; // 普通0/弱点1/致死2；同一短窗口仅升档，不让低级命中覆盖高级提示。
    bool bOwnsConfirmedOverlay = false; // 只有本组件成功SetOverlayMaterial后为true，不能清除别的系统的Overlay。
    bool bConfirmedOverlayAttempted = false; // 每次Configure仅诊断/创建一次，缺资产不在每颗子弹同步加载或创建MID。
};
