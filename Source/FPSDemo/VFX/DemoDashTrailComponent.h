// 职责：玩家Lyra流线及旧快照回退。使用：GAS Cue/已批准Dash凭据驱动短时表现；配置资产独立于角色材质、位移和免伤。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Engine/TimerHandle.h"
#include "DemoDashTrailComponent.generated.h"

class UAbilitySystemComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPoseableMeshComponent;
class USkeletalMesh;
class UDemoDashFXProfile;
class UNiagaraComponent;
class UAudioComponent;

/** 一个冻结的人形快照；组件/材质由本Pawn拥有，不创建Actor，不复制骨骼，不驱动AnimBP。 */
USTRUCT()
struct FDemoDashGhost
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> Body; // 保存快照时的全身骨姿态，世界变换固定。
    UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> Head; // 可选独立头部分区，同一次采样复制Body姿态，不能遗漏头部轮廓。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material; // 这一张快照的Body/Head共用MID，Fade与其他快照独立。
    float Age=-1.f; // 本地游戏秒年龄，-1表示隐藏可复用；不按真实时间跨暂停消散。
};

/** 消费已批准闪避凭据生成Lyra流线；可配置回退全身快照，颜色复制而粒子在各端独立生成。 */
UCLASS(Config=Game, DefaultConfig, ClassGroup=(Demo), meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoDashTrailComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 默认停Tick；硬引用DA_DashFlow及回退材质进入Cook，不加载最早NS_DashTrail。 */
    UDemoDashTrailComponent();
    /** NewASC属于PlayerState；Character就绪时调用，可空/重复；旧绑定精确解绑，换Avatar清除快照。 */
    void BindAbilitySystem(UAbilitySystemComponent* NewASC);
    /** 标签及阵亡复制到达时重算表现；支持先标签后BeginPlay，不依赖武器复制完成。 */
    void RefreshFromAbilitySystem();
    /** Cue、Dash凭据OnRep及Avatar就绪时调用；共享服务器起点去重/剔除过期事件，不复制粒子或修改移动。 */
    void RefreshApprovedDash();
    /** NewColor为线性RGBA，RGB 0..4、A 0..1；仅服务器调用并复制，只影响此Pawn残影，不写存档。 */
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Demo|Dash Trail")
    void SetTrailColor(FLinearColor NewColor);
    /** OutLifetimeProps为引擎注册表；只复制颜色，不复制瞬时Mesh或每帧姿态。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    /** DeltaTime为游戏秒，TickType/TickFunction由引擎提供；只在闪避/尾迹存活期间，在身体最终求值后采样和淡出。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

protected:
    // 默认DA_DashFlow集中配置；旧效果回退只切bLegacyAfterimages，不删除历史资产。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Demo|Dash Flow") TObjectPtr<UDemoDashFXProfile> FlowProfile;
    /** 读取配置/颜色并建立Body求值依赖；仅Legacy模式预建模型池，无资源不阻塞玩法。 */
    virtual void BeginPlay() override;
    /** Reason为卸载原因；清Timer/委托并销毁全部快照Mesh/MID，不能遗留到下一Pawn。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    // 真实Editor材质实例；参数TrailColor/GhostFade由组件驱动，GhostOpacity/GlowStrength由材质实例配置。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Demo|Dash Trail") TObjectPtr<UMaterialInterface> GhostMaterial;
    // 保留旧属性名以兼容组件覆盖；勾选后覆盖配置/回退材质的默认颜色。
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Demo|Dash Trail", meta=(DisplayName="Override Material Color")) bool bOverrideSystemDefaults=false;
    // 默认从DA_DashFlow读取，Legacy时从MI_DashAfterimage读取；SetTrailColor权威修改复制，Alpha控制透明度。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_TrailColor, Category="Demo|Dash Trail") FLinearColor TrailColor=FLinearColor(.08f,.65f,1.f,.8f);
    // 两次姿态快照的最小游戏秒间隔，.016.. .1；每帧最多一次，不补采相同姿态，默认.025形成密集运动残像。
    UPROPERTY(Config, EditDefaultsOnly, Category="Demo|Dash Trail", meta=(ClampMin="0.016",ClampMax="0.1",Units="s")) float SnapshotInterval=.025f;
    // 每张全身残影淡出的游戏秒，.08.. .6；默认.24与12槽/.025采样预算配套。
    UPROPERTY(Config, EditDefaultsOnly, Category="Demo|Dash Trail", meta=(ClampMin="0.08",ClampMax="0.6",Units="s")) float GhostLifetime=.24f;
    // 每Pawn快照槽数2..16，默认12；每槽至多Body+Head两个无碰撞网格，环形复用，不随游戏时长增长。
    UPROPERTY(Config, EditDefaultsOnly, Category="Demo|Dash Trail", meta=(ClampMin="2",ClampMax="16")) int32 MaxGhosts=12;
    // 实际移动达到该cm值才采下一张，默认3，避免撞墙原地叠成高亮人形。
    UPROPERTY(Config, EditDefaultsOnly, Category="Demo|Dash Trail", meta=(ClampMin="0",ClampMax="20",Units="cm")) float MinSampleDistance=3.f;
    // 单帧位移超过该cm值清历史，以免传送/校正跨场景留下残像；默认400，低帧大跨步也优先清理。
    UPROPERTY(Config, EditDefaultsOnly, Category="Demo|Dash Trail", meta=(ClampMin="200",ClampMax="2000",Units="cm")) float ResetDistance=400.f;
    // 最长生成游戏秒，.1..2；默认.6覆盖现有.45秒GE，兜底仅停表现，不延长躲避/无敌。
    UPROPERTY(Config, EditDefaultsOnly, Category="Demo|Dash Trail", meta=(ClampMin="0.1",ClampMax="2",Units="s")) float MaxEmissionSeconds=.6f;

private:
    /** DeltaSeconds为游戏帧秒；世界真实位置检测撞墙/传送，截止实际推进时间并渐隐，然后关闭Tick。 */
    void TickFlow(float DeltaSeconds);
    /** bImmediate用于死亡/切图/传送；正常结束停止发射后渐隐，重复调用不会重置衰减起点。 */
    void StopFlow(bool bImmediate);
    /** 把配色及第一人称强度写到本Pawn独立MID/System，绝不改人物或枪械材质。 */
    void ApplyFlowParameters();
    /** 游戏状态时钟可用时返回服务器秒，否则回退World秒；只用于已批准表现的时序。 */
    double FlowServerTime() const;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> FlowSystem; // 每Pawn最多一个本地系统，正常结束复用，EndPlay销毁。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FlowMaterial; // 本Pawn独立MID，不能串色或更改角色材质。
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> FlowAudio; // 当前短促声音；被死亡/切图停止，自动销毁后引用会失效。
    float LastFlowStart=-100.f; // 已处理服务器起点；Cue+OnRep/重复ASC初始化同事件只播一次。
    double FlowEndTime=0.; // 本次批准推进截止服务器秒，正常收尾不等待0.45秒躲避Tag。
    float FlowFadeAge=-1.f; // -1正在发射，>=0为停止后的本地游戏秒；只在效果活跃时累积。
    float FlowBlockedAge=0.f; // 连续低于最小位移的游戏秒，真实移动时清零。
    float FlowStartupAge=0.f; // 起步缓冲，防Cue/移动包同帧乱序被误判撞墙。
    bool bFlowActive=false; // 控制Tick及幂等停止，不以Niagara异步IsActive替代业务状态。
    /** 复制颜色到达只更新本Pawn所有快照MID，包括还未消失的旧姿态。 */
    UFUNCTION() void OnRep_TrailColor();
    /** Tag/Count为游戏线程ASC事件；重新读取完整状态，不依赖两个标签的到达顺序。 */
    void OnStateTagChanged(FGameplayTag Tag, int32 Count);
    /** 标签上升沿使能采样Tick，实际取姿态延迟到PostUpdateWork，避免拿到移动前/动画前数据。 */
    void StartTrail();
    /** bImmediate为死亡/解绑清理；true立即隐藏全部快照，false只停新采样并保持Tick直到尾迹消散。 */
    void StopTrail(bool bImmediate);
    /** 最长生成时间兜底；窗口锁存不清，避免重复ASC初始化重播同一标签。 */
    void OnEmissionTimeout();
    /** BeginPlay或开始闪避时预建/重建固定池；Body/Head资产改变才重建，禁止每张快照NewObject。 */
    bool PreparePool();
    /** Mesh为当前身体或互补头部资源，Material为该槽MID；创建无碰撞/阴影/动画Tick、未附着角色的快照组件。 */
    UPoseableMeshComponent* CreateGhostMesh(USkeletalMesh* Mesh, UMaterialInstanceDynamic* Material);
    /** 一次采样冻结最终姿态及世界变换，环形复用槽；无有效骨姿态时跳过，不能显示参考T Pose。 */
    void CaptureGhost();
    /** 隐藏所有槽并重置采样位置；保留已分配对象供下一闪避复用。 */
    void ClearGhosts();
    /** Avatar结束/网格更换时释放池，不能误销毁源Body/Head或其他系统网格。 */
    void ReleasePool();
    /** 给池内MID更新有限且限幅的RGBA；不修改原人物材质或共享材质实例。 */
    void ApplyParameters();
    /** BeginPlay读取材质颜色并规范化Config；服务器颜色读取后复制，客户端不覆盖先到达的复制值。 */
    void ReadAssetDefaults();

    TWeakObjectPtr<UAbilitySystemComponent> BoundASC; // PS拥有ASC，组件只借用，不延长PS寿命。
    FDelegateHandle DashTagHandle; // 当前ASC的躲避边沿，解绑不影响其他订阅者。
    FDelegateHandle DeadTagHandle; // 当前ASC的死亡边沿，阵亡OnRep另有入口兜底。
    FTimerHandle EmissionTimer; // World游戏秒UObject回调，无异步lambda或线程访问骨骼。
    UPROPERTY(Transient) TArray<FDemoDashGhost> Ghosts; // 最多16槽强引用，GC生命周期随Pawn结束。
    TWeakObjectPtr<USkeletalMesh> PoolBodyMesh; // 当前池的源资源身份，换装时必须重建，非源组件所有权。
    TWeakObjectPtr<USkeletalMesh> PoolHeadMesh; // 当前互补头部资源，可空代表Body本身完整。
    int32 NextGhost=0; // 环形槽位写入位置，永不追加无限队列。
    float SampleElapsed=0.f; // 采样间隔累积游戏秒，低FPS不追补多张相同快照。
    FVector LastSamplePosition=FVector::ZeroVector; // 最近成功采样世界位置，用于原地抑制。
    FVector LastFramePosition=FVector::ZeroVector; // 本地前帧身体位置，用于传送断点检测。
    bool bHasSample=false; // 新窗口/清场尚无历史时，下一次PostUpdate可以立即采样。
    bool bWindowActive=false; // 标签窗口锁存，超时不重新触发同一窗口。
    bool bEmitting=false; // 是否允许新增快照；false时仍可存在短暂淡出的历史。
    bool bColorExplicitlySet=false; // BeginPlay前服务器改色优先于材质默认值，仅本Pawn有效。
};

