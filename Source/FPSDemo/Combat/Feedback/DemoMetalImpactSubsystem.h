// 职责：全场共享、有界的金属火花/暗红血雾与原空间声音池。权威回执准入、Pawn批量RPC分发；依赖World时钟/ISM/Audio，血雾独立固定槽、不持有敌人。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/NetSerialization.h"
#include "Combat/Feedback/DemoFleshMist.h"
#include "DemoMetalImpactSubsystem.generated.h"

/** 已确认损血的表现快照；只允许资源引用、不带敌人引用，丢包绝不改变伤害。 */
USTRUCT()
struct FDemoMetalImpactEvent
{
    GENERATED_BODY()
    UPROPERTY() FVector_NetQuantize Point = FVector::ZeroVector; // 真正接触点，世界cm，敌人死亡后仍有效。
    UPROPERTY() FVector_NetQuantizeNormal Normal = FVector::UpVector; // 从表面朝外单位法线，非怪物中心朝向。
    UPROPERTY() bool bMelee = false; // 近战用预留池和DA显式空间接触音；普通弹击仍只占公共池。
    UPROPERTY() bool bPlaySound = false; // 设施枪弹请求同一空间金属声；与近战预算独立，非音量或伤害参数。
    UPROPERTY() bool bRedesigned = false; // 候选原Wave使用显式空间衰减/增益，固定音频预算不变。
    UPROPERTY() bool bSuppressSparks = false; // 肉体接触不生成金属火花；仍可使用同池固定声部播放空间撞击。
    UPROPERTY() uint8 FleshTier = 0; // v32已确认表面：0无血雾、1身体、2头部；只复制纯表现等级。
    UPROPERTY() bool bHeavyWeapon = false; // v32原发射武器为霰弹/狙击；不读取客户端当前装备。
    UPROPERTY() TObjectPtr<class USoundBase> Sound; // 可选近战DA的Cook资源，非Actor；为空时回退池默认金属Cue。
};

/** 只读诊断，累计值仅本World生命周期；自动化证明限流与对象数量有界，不上传/保存。 */
struct FDemoMetalImpactStats
{
    uint64 Accepted = 0; // 服务器准入总数，不等于客户端实际可见数。
    uint64 Deduplicated = 0; // 同枪同目标省略的重复回执。
    uint64 RateDropped = 0; // 目标间隔/全局桶限流，仅丢表现。
    uint64 PoolDropped = 0; // 本机视觉无空槽；声音独立尝试。
    uint64 DistanceDropped = 0; // 本机无观众或超过最大距离。
    uint64 Played = 0; // 成功激活的火花簇。
    uint64 MistPlayed = 0; // 本World成功激活血雾簇数量，与金属独立预算。
    uint64 MistPoolDropped = 0; // 血雾固定槽满后丢弃数，不抢占火花/近战预留。
    uint64 AudioPlayed = 0; // 池内成功请求的音频声部，-nosound不证明可听性。
    int32 PeakActive = 0; // 本机同时存活簇峰值，不超过冻结容量。
    int32 PoolObjects = 0; // Actor+Root+已建立ISM（含血雾）+固定音频数；命中后不增长。
    FVector LastPoint = FVector::ZeroVector; // 最近成功播放的原始接触点，诊断真实伤害与表现一致，不做瞄准输入。
};

// 纯值短期去重；FGuid/目标实例ID不解析为Actor，固定128项，覆盖远长于弹丸最大寿命的常规射击。
struct FDemoMetalImpactStamp { FGuid Shot; uint32 Target = 0; };
// 单目标节流时间，最多128目标，世界卸载整体丢弃；近战不走该弹击间隔。
struct FDemoMetalImpactTarget { uint32 Target = 0; double At = -100.; };
// 一根火花的弹道/消失参数；cm、cm/s、秒，CPU纯表现不执行物理碰撞。
struct FDemoMetalSpark { FVector Velocity = FVector::ZeroVector; float Life = .2f; float Length = 5.f; };
// 一簇固定12根的位置，空闲时Age=-1；不保存Actor或组件引用。
struct FDemoMetalImpactSlot
{
    FVector Origin = FVector::ZeroVector; // 世界接触点加0.8cm外偏，避免埋进表面。
    float Age = -1.f; // World秒龄，暂停时冻结；最多.32秒后释放。
    int32 Count = 0; // LOD后实际有效火花根数，3..12。
    FDemoMetalSpark Sparks[12]; // 编译期固定上限，运行期无颗粒分配。
};

UCLASS()
class FPSDEMO_API UDemoMetalImpactSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    /** CDO硬引用配置，确保Cook收集整个资源闭包；不在命中时同步读盘。 */
    UDemoMetalImpactSubsystem();
    /** Collection为当前World子系统集合；初始化纯值桶及固定容量缓存，不创建网络Actor。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** InWorld开始游玩时一次性建立视觉/声音池；Dedicated仅保留权威限流器。 */
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    /** 卸载时停所有池声音、销毁本地Actor并清空缓存，不跨关卡保留。 */
    virtual void Deinitialize() override;
    /** DeltaTime为World秒；更新固定火花与独立血雾缓冲，不增长数组、不做碰撞查询。 */
    virtual void Tick(float DeltaTime) override;
    /** 仅有活跃簇才Tick；空闲不每帧更新渲染状态。 */
    virtual bool IsTickable() const override;
    /** 引擎性能计数器标识；无玩法状态变化。 */
    virtual TStatId GetStatId() const override;
    /** Shot/Target来自服务端实际扣血；bMelee分独立桶。false只抑制表现，调用者不得取消伤害。 */
    bool Admit(const FGuid& Shot, uint32 Target, bool bMelee);
    /** Event来自服务器不可靠批次，本地按距离/空槽播放；近战记录资源/请求/引擎活跃状态供排错，不证明可听性。新表面Wave独立随机Pitch0.95..1.05及±1.5dB，近战/Cue不变，无伤害权限。 */
    void Play(const FDemoMetalImpactEvent& Event);
    /** 测试/诊断同步借用本World累计值；不得跨帧保留引用。 */
    const FDemoMetalImpactStats& GetStats() const;
protected:
    /** Type仅允许Game/PIE，禁止编辑器预览World重复预分配资源。 */
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
    /** Event为已准入血肉事件，DistanceSquared为最近本机观众距离平方cm²；固定槽满只计数丢弃。 */
    void SpawnFleshMist(const FDemoMetalImpactEvent& Event, float DistanceSquared);
    /** World开始建立独立血雾ISM；缺材质仅一次Warning，不阻断金属/声音。 */
    void InitializeFleshMist();
    // CDO硬资源所有权，由GC追踪；只在World开始读取预算。
    UPROPERTY() TObjectPtr<class UDemoMetalImpactSettings> Settings;
    UPROPERTY() TObjectPtr<AActor> PoolActor; // 本机非复制瞬态Actor，World卸载销毁。
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> Streaks; // 所有簇共用一个ISM绘制管线。
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> Flashes; // 瞬闪独立材质的第二个ISM。
    UPROPERTY() TObjectPtr<class UInstancedStaticMeshComponent> FleshMistPool; // 第三个本地无碰撞ISM，固定血雾团实例。
    FDemoFleshMistState MistState; // 独立预分配纯值槽/变换/淡出缓冲，命中不扩容。
    UPROPERTY() TArray<TObjectPtr<class UAudioComponent>> Voices; // 池Actor拥有，不AutoDestroy、不按击中动态创建。
    TArray<FDemoMetalImpactSlot> Slots; // 初始化一次SetNum，命中不扩容。
    TArray<FTransform> SparkTransforms; // 长度Slots*12，一次批量提交复用缓冲。
    TArray<FTransform> FlashTransforms; // 长度Slots，未激活者零缩放。
    TArray<FDemoMetalImpactStamp> Recent; // 最近128枪目标组合，满后覆盖环形索引。
    TArray<FDemoMetalImpactTarget> Targets; // 最近128目标弹击时间，同样有界。
    int32 RecentCursor = 0; // 满缓存的下一个覆盖位置，0..127。
    int32 TargetCursor = 0; // 目标缓存满后的覆盖位置，0..127。
    int32 Reserved = 4; // World开始冻结的近战保留槽数。
    int32 Active = 0; // 本机当前活跃簇数，用于空闲停Tick。
    float MaxDistance = 2500.f; // 本World冻结的LOD/音频剔除距离cm。
    float BulletRate = 48.f; // 服务端弹击每秒令牌补充量。
    float Interval = .06f; // 本World单目标弹击间隔秒。
    double LastAdmission = 0.; // 令牌桶上次更新的World秒，近战/枪械共享更新时间。
    double BulletTokens = 8.; // 弹击突发容量8，初始化允许第一枪霰弹即时反馈。
    double MeleeTokens = 4.; // 独立近战容量4、每秒16，不被弹击耗尽。
    FDemoMetalImpactStats Stats; // 有界纯值计数，不逐帧输出业务日志。
};
