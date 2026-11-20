// 职责：隔离实体弹GAS/碰撞专项；附加-DemoProjectilePresentationAudit仅Editor -game Standalone采真实Mesh/HP/RHI，不修改正式资产或直接设Phase/损血。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Weapons/Projectiles/DemoProjectileConfig.h"
#include "Weapons/DemoWeaponConfig.h"
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
#if WITH_EDITOR
    /** 新显式分支通过真实战局/GA验证慢弹Mesh、Scope/换枪/墙/阶段取消；不运行旧测试的Phase夹具。 */
    void TickPresentationAudit(float DeltaSeconds);
    /** 一次真实Fire GA并冻结P0/F0/HP/弹药，必要时插墙或切枪；false已写失败证据。 */
    bool BeginPresentationAuditShot();
    /** 隔离RHI专项：在游戏帧开始用正式枪械开火；固定或移动转向，随后三次从真实Socket和亮条两端复核屏幕投影。 */
    void SampleMovingMuzzle(UWorld* World,ELevelTick TickType,float DeltaSeconds);
    FVector MuzzleEarlySocket=FVector::ZeroVector; // 开火回调同步取的旧点cm，用作同枪对照，不写生产缓存。
    FVector MuzzlePawnStart=FVector::ZeroVector; // 开火时Avatar位置cm，用于证明实际移动。
    uint64 MuzzleShotFrame=0; // 标识生产发射帧，防止多帧后追枪也误判通过。
    /** 同步CSV/JSON及RHI请求清单，明确技术范围后仅退出双显式独立进程。 */
    void FinishPresentationAudit(bool bSuccess,const FString& Reason);
    /** 成功/失败/EndPlay共用幂等还原：仅本夹具CDO/武器实例，撤掉临时墙/相机。 */
    void RestorePresentationAudit();
    /** 仅Editor隔离夹具变更/还原弹CDO后调用UE原生API重建BP默认差异表；不保存包或改构造脚本。 */
    void RefreshPresentationAuditDefaults();
    double PresentationWallStart=0.,PresentationGameStart=0.,PresentationShotStart=0.; // 90墙钟/35World秒预算及当前真实发射时刻。
    double PresentationShopStarted=0.; // 枪墙真实Focus/报价/购买最多3World秒，失败保留具体前置原因。
    FString PresentationDirectory,PresentationOriginalProjectileText; // 唯一Saved证据路径及CDO原始配置文本，不写Content。
    TArray<FString> PresentationRows,PresentationEvents,PresentationCaptures; // 本World逐帧原始数值、边界和真实RHI请求，结束一次写盘。
    TWeakObjectPtr<class ADemoCharacter> PresentationPawn; // World持有的当前Avatar，不能跨旅行延命。
    TWeakObjectPtr<class ADemoProjectileBase> PresentationBullet; // 真正Fire GA生成的唯一慢弹，不是视觉代理。
    TWeakObjectPtr<AActor> PresentationCamera; // 仅外部ViewTarget拒绝案例使用，不改角色相机。
    FDemoProjectileConfig PresentationOriginalProjectile; // 临时CDO完整值快照；只改标量，原资源仍由原CDO持有，退出严格还原。
    FDemoWeaponConfig PresentationOriginalWeapon; // 本次手枪实例值快照；原资源不替换，仍由World库存持有，不改BP/CDO。
    FVector PresentationPhysicalOrigin=FVector::ZeroVector,PresentationVisualOrigin=FVector::ZeroVector; // 成功开火瞬间的真实P0/当前FP枪口F0，不随在飞换枪变化。
    FGuid PresentationShot; // 整枪真实服务器身份，每案换新，不能从当前装备推断。
    float PresentationHPBefore=0.f,PresentationHPAfter=0.f,PresentationPreviousCorrection=0.f; // 实际目标HP及上帧可见偏移，用于时序/单调边界。
    float PresentationPreviousScale=1.f; // 实际渲染根world尺寸的上帧值，保证同弹归并不倒退；物理Actor scale另核1。
    int32 PresentationCase=0,PresentationFrame=0,PresentationShotFrames=0,PresentationDamageCases=0; // 八案索引/总采样帧/本弹帧/真实命中案例数。
    bool bPresentationFinished=false,bPresentationSavedConfig=false,bPresentationRestored=false; // 幂等结束/临时配置/还原状态，只属于本测试进程。
    bool bPresentationScopeReady=false; // 双显式Editor RHI Standalone门已通过；未通过时不得修改用户PIE的World。
    bool bPresentationProfileFlagSaved=false,bPresentationOriginalInfiniteGold=false; // 枪墙前置只用本GI免扣，结束恢复原Editor调试标志，不改金币余额。
    bool bPresentationSawHandoff=false,bPresentationSawLiveMesh=false,bPresentationCaseHandoff=false; // 已实际见到Mesh/归并，不能以源码或截图请求代替。
    bool bPresentationResetCheckpointVerified=false; // 实际生产ResetCheckpoint及从本GI临时槽磁盘重新Load/Validate通过；不代表ReturnHub/OpenLevel。
    bool bPresentationResetHadCoins=false,bPresentationResetHadSilver=false,bPresentationResetHadPermanentGrowth=false,bPresentationResetHadTemporaryGrowth=false; // 明确非零输入覆盖边界；零输入不冒充非零经济/成长清理验收。
#endif
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
