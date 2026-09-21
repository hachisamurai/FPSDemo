#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/DemoFPAnimInstance.h"
#include "DemoWeaponAnimationComponent.generated.h"

class ADemoCharacter;
class ADemoWeaponBase;
class UGameplayAbility;
class UAudioComponent;
class UStaticMeshComponent;

/** 同帧真实播放时钟的只读审计值；只测动画时刻，不把它等同于蒙皮/手指观感验收。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoReloadPhaseAudit
{
    GENERATED_BODY()
    // 对应本武器动作序号，完成/取消后保留最后记录供专项检查，下次开始才重置。
    UPROPERTY(BlueprintReadOnly, Category="Phase Audit") int32 Sequence = 0;
    // 引擎真实Montage实例ID，同一资源被重新播放也不会复用旧实例身份。
    UPROPERTY(BlueprintReadOnly, Category="Phase Audit") int32 MontageInstanceID = INDEX_NONE;
    // 已在NativeUpdate实际读取的样本数量；0不能宣称同步验证通过。
    UPROPERTY(BlueprintReadOnly, Category="Phase Audit") int32 SampleCount = 0;
    // 该样本游戏时钟要求的0..1进度，来自本次冻结开始时间与时长。
    UPROPERTY(BlueprintReadOnly, Category="Phase Audit") float ExpectedProgress = 0.f;
    // 引擎UpdateMontage之后的真实归一化位置，不是把目标值回填当作测量。
    UPROPERTY(BlueprintReadOnly, Category="Phase Audit") float ArmsProgress = 0.f;
    // 枪SingleNode当前取样时间/素材长度，与手臂在同一NativeUpdate边界读取。
    UPROPERTY(BlueprintReadOnly, Category="Phase Audit") float WeaponProgress = 0.f;
    // 本样本两网格与世界时钟三者最大差，换算成本次游戏秒数。
    UPROPERTY(BlueprintReadOnly, Category="Phase Audit") float LastErrorSeconds = 0.f;
    // 本动作整个采样区间最大秒误差；低FPS、暂停/时间缩放回归用此断言。
    UPROPERTY(BlueprintReadOnly, Category="Phase Audit") float MaximumErrorSeconds = 0.f;
};

/** Pawn拥有的单人第一人称表现协调器；没有弹药/伤害/技能权限，取消由现有GAS处理。 */
UCLASS(ClassGroup=(Demo), meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoWeaponAnimationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 设置固定主图软类、游戏时钟Tick；仅装备时同步加载，不在Tick加载资产。 */
    UDemoWeaponAnimationComponent();
    /** EndPlayReason为Pawn/World卸载原因；停止本次音频、Montage并恢复临时表现。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    /** DeltaTime秒、TickType/TickFunction为引擎当前Tick；游戏线程按单一phase更新枪械和声音。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    /** 创建/恢复固定主实例并显式刷新ASC的Mesh1P上下文；不会在每次切枪重建它。 */
    bool InitializeArms();
    /** Weapon为待装备库存，Error为失败原因；纯预检，失败必须保留原装备。 */
    bool ValidateWeapon(const ADemoWeaponBase* Weapon, FString& Error) const;
    /** Weapon为已通过预检的目标；真实链接失败内部恢复旧层，调用者再回滚当前装备。 */
    bool EquipWeapon(ADemoWeaponBase* Weapon);
    /** Weapon必须为当前实例；只播放配置中的FPAction开火动作，不改变弹药。 */
    void PlayFire(ADemoWeaponBase* Weapon);
    /** Weapon/Ability为本次锁定武器及GAS技能；Sequence/Duration/Empty由玩法开始时冻结，不允许共享CDO存储。 */
    bool BeginReloadPresentation(ADemoWeaponBase* Weapon, UGameplayAbility* Ability, int32 Sequence, float Duration, bool bEmpty);
    /** Weapon和Sequence必须仍匹配；bCompleted决定是否推进到末帧再复位，不在这里补弹。 */
    void EndReloadPresentation(ADemoWeaponBase* Weapon, int32 Sequence, bool bCompleted);
    /** Weapon为调用方；只为当前装备重建左右手和枪械零件状态。 */
    void RestoreWeaponPose(ADemoWeaponBase* Weapon);
    /** 输出该帧纯值动画状态，UE动画NativeUpdate在游戏线程读取，不绑定World工作线程查询。 */
    FDemoFPAnimationSnapshot GetSnapshot() const;
    /** 返回当前配置的只读借用引用；类CDO由LinkedLayerClass保活，空值表示未配置旧武器。 */
    const FDemoWeaponAnimationSet* GetAnimationSet() const;
    /** 专项测试/图只读当前链接类，不允许绕过装备组件改类。 */
    UFUNCTION(BlueprintPure, Category="Animation") TSubclassOf<UDemoWeaponLayerAnimInstance> GetLinkedLayerClass() const;
    /** 返回当前表现进度[0,1]；只属于本Pawn，不是网络复制/预测协议。 */
    UFUNCTION(BlueprintPure, Category="Animation") float GetReloadProgress() const;
    /** 当前是否存在有效换弹表现武器；表现降级时可能为false，不能代替GAS玩法状态。 */
    UFUNCTION(BlueprintPure, Category="Animation") bool IsReloadPresentationActive() const;
    /** 当前表现开始时冻结的空仓版本；取消/结束后恢复false。 */
    UFUNCTION(BlueprintPure, Category="Animation") bool IsEmptyReload() const;
    /** 当前或最近一次表现序号；需同时匹配武器，不能作为全局唯一动作ID。 */
    UFUNCTION(BlueprintPure, Category="Animation") int32 GetReloadSequence() const;
    /** Main必须是本次绑定主实例、DeltaSeconds是引擎本次动画秒数；只在PreUpdate游戏线程边界调用。 */
    void PrepareReloadAnimationUpdate(UDemoFPAnimInstance* Main, float DeltaSeconds);
    /** Main为刚完成引擎Montage推进的绑定实例；读取真实双时钟位置，不能从工作线程调用。 */
    void SampleReloadAnimationPhase(UDemoFPAnimInstance* Main);
    /** 返回本次/最近一次动作审计副本，纯值无对象所有权；样本为0表示尚未测量。 */
    UFUNCTION(BlueprintPure, Category="Animation") FDemoReloadPhaseAudit GetReloadPhaseAudit() const;
    // 固定主AnimBP软类，仅初始化时解析；资产导入器与Cook规则需同时保持此路径。
    UPROPERTY(EditDefaultsOnly, Category="Animation") TSoftClassPtr<UDemoFPAnimInstance> MainAnimClass;
private:
    /** 主线程重新借用Owner；不用跨帧裸角色指针，不延长Pawn生命周期。 */
    ADemoCharacter* GetCharacter() const;
    /** Progress为[0,1]，bEmitSounds控制阶段跨越播放；可重建零件姿势而不重播声音。 */
    void UpdateReloadPhase(float Progress, bool bEmitSounds);
    /** 同步统一清理，先设保护标志再停止Montage，避免停止回调重入取消新动作。 */
    void ClearReloadPresentation();
    /** Montage/bInterrupted来自引擎；ExpectedSequence/ExpectedInstanceID是绑定时冻结的值，迟到同资源回调不能取消新动作。 */
    void OnReloadMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted, int32 ExpectedSequence, int32 ExpectedInstanceID);
    /** bShow按当前换弹阶段决定左手临时弹匣与枪内骨显隐，无物理和Gameplay所有权。 */
    void SetMagazineInHand(bool bShow);
    // 当前已链接类强引用保活其CDO和资源；不替换手臂主实例。
    UPROPERTY(Transient) TSubclassOf<UDemoWeaponLayerAnimInstance> LinkedLayerClass;
    // 最后成功链接的主实例弱引用；实例被Editor重建/外部更改时先取消旧GA再恢复链接和ASC上下文。
    TWeakObjectPtr<UDemoFPAnimInstance> LinkedMainInstance;
    // 当前装备弱引用；切枪只通过EquipWeapon提交，不延长库存武器生命周期。
    TWeakObjectPtr<ADemoWeaponBase> EquippedWeapon;
    // 本次换弹弱引用；切枪后的旧回调不能借当前装备替旧枪收尾。
    TWeakObjectPtr<ADemoWeaponBase> ReloadWeapon;
    // 本次技能弱引用，仅意外Montage中断时请求取消；GAS任务与弹药由技能自己清理。
    TWeakObjectPtr<UGameplayAbility> ReloadAbility;
    // 本次配对资源值拷贝/强引用，防止类配置或切换销毁掉正在播放的资源。
    UPROPERTY(Transient) FDemoReloadAnimationPair ActivePair;
    // 短音频组件归Pawn，取消时Stop并Destroy；正常完成也不遗留后续旧动作声音。
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> ReloadAudio;
    // 左手临时弹匣组件由Pawn拥有、无碰撞；本协调器在EndPlay销毁。
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> HandMagazine;
    // 武器BeginReload冻结的动作序号，本地不复制，与弱武器共同拒绝迟到回调。
    int32 ReloadSequence = 0;
    // 本次开始的World游戏秒数；暂停/时间缩放遵循World，不读取墙钟。
    float ReloadStartTime = 0.f;
    // 本次冻结的玩法秒数，合法值来自武器ReloadSeconds；清理后为0。
    float ReloadDuration = 0.f;
    // 已发布的归一化表现进度[0,1]，双网格和声音共用，清理后为0。
    float ReloadProgress = 0.f;
    // 开始时Ammo==0的版本选择，动作途中不跟随弹药变化，清理后为false。
    bool bEmptyReload = false;
    // 本次引擎Montage实例ID，在自然混出期间仍可精确定位，不能用仅返回活动实例的资源查询替代。
    int32 ReloadMontageInstanceID = INDEX_NONE;
    // 真实同帧取样结果，清理时保留供诊断，下次Begin重置；只在游戏线程更新。
    FDemoReloadPhaseAudit PhaseAudit;
    // 下一个声音事件索引只向前推进，Seek/暂停恢复不重复触发早期事件。
    int32 NextSoundEvent = 0;
    // 正常清理期间停止Montage可同步回调；此标志避免将自己的Stop当作意外取消。
    bool bClearing = false;
    // 当前弹匣显示缓存，避免每帧重复HideBone/日志；开始和取消均显式重建。
    bool bMagazineInHand = false;
    // Mesh1P首次初始化时的相对相机位置；肩部不随持枪锚点前移，轻摆以此为基线。
    FVector BaseArmsLocation = FVector::ZeroVector;
    // 最近一次成功装备的World游戏秒数，用于C++4cm举枪偏移恢复。
    float EquipStartTime = 0.f;
    // 基线仅从首次有效Mesh1P采样一次，重建动画实例不把当前轻摆误记成新基线。
    bool bCachedArmsTransform = false;
};
