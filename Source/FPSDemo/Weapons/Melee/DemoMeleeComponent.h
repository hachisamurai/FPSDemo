// 职责：近战槽、权威GAS结算、拥有者预测与单槽连招缓冲；早到确认保持本地桥时钟，过界短暂续采仅供表现，绝不扩大伤害授权。
// 服务器密采TP烘焙轨迹；显式上身武器可按服务器当前已求值握点重锚，不信任客户端命中或骨数据。
// WITH_EDITOR仅显式隔离-game回归可用私有瞬态闸对照旧canonical衔接，不改共享DA/权威逻辑，不进入非Editor包体。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Weapons/Melee/DemoMeleeDefinition.h"
#include "Game/DemoGameState.h" // 阶段快照阻止安全区挥击跨入下一场战斗；不是额外战局真值。
#include "DemoMeleeComponent.generated.h"
class ADemoCharacter;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UDemoCombatSession;
class UDemoMeleeCameraModifier;
class APlayerCameraManager;

/** 本Avatar短期权威确认关联；仅相机表现，不用本地预测Serial猜服务器刀号。 */
struct FDemoMeleeCameraAuthority
{
    uint32 Serial=0; // 服务器FDemoMeleeState.Serial；与HP回执ShotId.B相同。
    uint32 Request=0; // 服务器已确认的OwnerRequestId，房主本机为0，不复制/存档。
    double ExpiresAt=0.; // 该刀计划结束+0.25服务器游戏秒，晚到回执不能无限有效。
};

/** HP回执可能早于动作确认；最多8项/0.25秒等待，过期只丢相机反馈，不回放伤害。 */
struct FDemoMeleePendingCameraHit
{
    FGuid Shot; // 既有服务器真实扣血身份，不解析Actor。
    double ReceivedAt=0.; // 本World游戏秒，不跨World保留。
};

/** 一份原子复制的姿态状态，不复制逐帧骨骼、不信任客户端提交命中或伤害。 */
USTRUCT()
struct FDemoMeleeState
{
    GENERATED_BODY()
    UPROPERTY() bool bEquipped=false; // 仅服务器写入；原枪库存保留弹药，表现被近战覆盖。
    UPROPERTY() int32 Attack=INDEX_NONE; // -1待机，其他为定义数组下标。
    UPROPERTY() uint32 Serial=0; // 单Pawn递增序号，重播与声音去重。
    UPROPERTY() uint32 LastCancelledSerial=0; // 最近显式撤销的服务器刀号；跨后续起刀保留，避免属性合并漏掉取消→新刀之间的反馈撤销。不写存档。
    UPROPERTY() double StartTime=0.; // 原作者时间轴的运行时原点；连招入口会回溯RuntimeAtPhase(EntryPhase)，不是本刀实际激活时刻。权威/预测各用自身时基。
    UPROPERTY() uint32 OwnerRequestId=0; // 拥有者请求关联号，0为服务器本地请求；只关联确认/取消，不能携带命中或回溯时间。
    UPROPERTY() int32 NextHorizontalCursor=0; // 服务器成功起招后的下一循环位置，供拥有者预测下一动作；真正选择仍由服务器复核。
    UPROPERTY() bool bPrimarySequence=false; // 只有左键主循环启动的刀允许进入提前连招，右键保持Standalone。
    UPROPERTY() bool bComboExitCommitted=false; // 本刀已承诺唯一后继输入，选择兼容前缀/Antic变体；排队不代表下一刀已开始或已授伤害。
    UPROPERTY() bool bStartedFromCombo=false; // 本刀从前刀匹配入口起招；只影响连续表现与剩余时长。
    UPROPERTY() int32 ComboSourceAttack=INDEX_NONE; // 连续边的前刀索引，供表现验证真实前缀/目标关系，不接受客户端传入。
    UPROPERTY() float EntryPhase=0.f; // 本刀原作者0..1入口；服务器扫掠从此开始，禁止补跳过的前摇。
};

UCLASS(ClassGroup=(Demo),meta=(BlueprintSpawnableComponent))
class FPSDEMO_API UDemoMeleeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 设置只读默认DA软引用及复制；不在CDO加载模型或创建临时存档。 */
    UDemoMeleeComponent();
    /** Pawn开始时载入并验证配置；Editor隔离夹具可选白名单候选/严格-game单机A/B瞬态闸，普通启动仍正式DA；失败禁止装备，原枪仍可用。 */
    virtual void BeginPlay() override;
    /** DeltaTime为游戏秒；TickType/TickFunction是引擎当前调度上下文，仅游戏线程处理状态。 */
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    /** Reason为World/Pawn结束原因，撤销GA并清理本组件临时网格。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** Props为引擎复制注册输出，仅发布同一原子姿态状态。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
    /** bEquip来自本人输入；服务器复核存活/阶段/菜单，装备时取消枪械动作。 */
    UFUNCTION(BlueprintCallable,Category="Melee") bool SetEquipped(bool bEquip);
    /** 数字3无参数输入回调，仍走服务器统一装备校验。 */
    void EquipPressed();
    /** Index为0主攻击循环/1可选副攻击；新Standalone可按DA在收招后重选组首，缺项立即拒绝；客户端不能指定目标/数值，权威GA成功才推进。 */
    UFUNCTION(BlueprintCallable,Category="Melee") void RequestAttack(int32 Index);
    /** GA激活内调用；冻结请求招式、阶段/关号和伤害，只有Combat绑定战斗凭据；成功才提交组首/下一游标，失败不消耗时钟或推进。 */
    bool BeginAttack();
    /** 菜单/换枪/死亡/GA取消时幂等撤销；冷却截止保留，不能切枪取消后摇刷频率。 */
    void CancelAttack();
    /** GA结束回调，保留装备和冷却；bCancelled=true禁止补伤害并发布/执行本刀镜头撤销，正常结束补扫长帧漏过的剩余窗口。 */
    void FinishAttack(bool bCancelled);
    /** 只读当前复制装备，不代表尚未收到的客户端请求已获服务器批准。 */
    UFUNCTION(BlueprintPure,Category="Melee") bool IsEquipped() const;
    /** 表现是否在招式时长内或合法已承诺连招的短宽限内；拥有者预测/其他端复制，不改变GA/伤害截止。 */
    UFUNCTION(BlueprintPure,Category="Melee") bool IsAttacking() const;
    /** 返回本Pawn强持有的只读配置，加载失败为空。 */
    UFUNCTION(BlueprintPure,Category="Melee") UDemoMeleeDefinition* GetDefinition() const;
    /** 本Pawn已求值的本人近战臂借用引用；初始化前可为空，绝不作为权威伤害扫掠源。 */
    USkeletalMeshComponent* GetFirstPersonArmsMesh() const;
    /** 相机空间刀的纯表现组件；代理仅借模型/相对握点，不复制其压缩世界比例。 */
    UStaticMeshComponent* GetFirstPersonWeaponMesh() const;
    /** 正常世界TP手骨刀的借用引用，权威伤害仍走冻结作者轨迹。 */
    UStaticMeshComponent* GetThirdPersonWeaponMesh() const;
    /** 装备/生命/代理状态共同决定TP刀投影，表现Tick及代理边沿共用本唯一写入入口。 */
    void RefreshOwnerShadowPolicy();
    /** 本Pawn拥有者收到实际HP损失后调用；ShotId必须对应当前权威刀，迟到/已换枪/旧Avatar只丢装饰反馈，不影响HUD或伤害。 */
    void ConfirmCameraHit(const FGuid& ShotId);
    /** bFirstPerson选择资源；Time为本轮Idle相位或当前刀映射到实际表现源秒数（可位于父完整上下文），Progress/Action仍属当前刀；拥有者用预测起点，绝不授权伤害。 */
    UAnimSequence* GetPose(bool bFirstPerson,float& Time,float& Progress,FName& Action) const;
    /** 游戏线程仅读表现：bFirstPerson选择Idle长度，首次装备/每次收招共用本机epoch从0循环；活动刀底层Idle返回0。
     * 缓存仅本Avatar瞬态，重复FP/TP查询不重置；Serial/取消标识能发现漏采的整刀，绝不改State/GA/网络或伤害时钟。 */
    float GetIdlePoseTime(bool bFirstPerson) const;
    /** 返回本Pawn递增动作序号，供既有TP过渡检测重复同招式。 */
    uint32 GetAttackSerial() const;
    /** 当前GA下一次WaitDelay秒数：先到分支，之后到原/连招结束；全部由同一StartTime与逆映射推导。仅服务器GA调用。 */
    float GetAbilityWaitSeconds() const;
    /** 当前权威刀是否达到承诺结束点；GA回调调用，未提交连招时保持完整Standalone结束。 */
    bool IsAttackSegmentComplete() const;
    /** GA在每刀CommitAbility前检查唯一后继资格；不改变状态、不授伤害。 */
    bool CanAdvanceCombo() const;
    /** 当前GA每刀Commit成功后调用：补旧窗口、发布新Serial并从匹配Entry起刀；失败由GA安全结束。 */
    bool AdvanceCombo();
    /** 仅已审兼容前缀或相邻连招边可跳过冻结混合；同Serial提交后继时Previous也可为本刀合法父完整源。
     * Previous为本实例上一实际资源；普通起招、取消和回Idle仍使用原混合，不伪造来源快照。 */
    bool IsContinuousPoseTransition(bool bFirstPerson,const UAnimSequence* Previous,uint32 PreviousSerial) const;
#if WITH_EDITOR
    /** 只读Editor验收：按当前权威招式查询实际伤害轨迹公式；不执行Sweep/扣血，不注册到非Editor包体。 */
    bool DebugSampleActiveTracePose(float Phase,FVector& Base,FVector& Tip) const;
#endif
    // 新武器入口：角色组件Details选择复制出的DA，模型/动画/攻击/声音均来自此资产。
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Melee") TSoftObjectPtr<UDemoMeleeDefinition> DefinitionAsset;
private:
    /** 每次表现更新只管理本人当前装备的唯一Modifier；缺PC/菜单/切枪时精确撤销，不影响受击/其它镜头。 */
    void UpdateCameraFeedback();
    /** 挥声相位调用一次；AttackIndex为当前真实招式，仅给反馈左右符号。 */
    void PlaySwingCameraFeedback(int32 AttackIndex);
    /** 取消/死亡/EndPlay撤销本组件创建的实例，不使用StopAll或改CameraComponent。 */
    void ClearCameraFeedback();
    /** ClientAttackResult/OnRep冻结真实Serial与Request；至多16项，不改业务预测State或把排队当起刀。 */
    void RememberCameraAuthority(const FDemoMeleeState& Confirmed);
    /** 明确取消入口在改写预测前撤销该刀身份及包络，迟到真实回执也不能重新震屏。 */
    void CancelCameraAttack();
    TWeakObjectPtr<UDemoMeleeCameraModifier> CameraFeedback; // CameraManager拥有强引用，Melee只借用自身实例。
    TWeakObjectPtr<APlayerCameraManager> FeedbackCameraManager; // 用原Manager精确移除，控制器切换也不误操作新实例。
    TArray<FDemoMeleeCameraAuthority> CameraAuthorities; // 最多16条只读权威事实，解决可靠确认/属性复制/HP回执乱序。
    TArray<FDemoMeleePendingCameraHit> PendingCameraHits; // 最多8项/0.25秒；取消、失去视角、离开装备时清空。
    TArray<uint32> CancelledCameraRequests,CancelledCameraSerials; // 各最多16个显式撤销身份，不让旧回执在新装备周期复活。
    double CameraNaturalCompletionTime=-1.; // 权威自然结束本机收敛时刻，最多0.25秒保留真实末刀反馈；明确取消置-1。
    /** 只接收装备请求，不允许客户端更换定义资产。 */
    UFUNCTION(Server,Reliable) void ServerSetEquipped(bool bEquip); // bEquip为本人目标状态。
    /** Index为本人0/1意图，RequestId为非零本机关联号；服务器重新复核GAS/阶段/冷却，不接受时间戳或目标。 */
    UFUNCTION(Server,Reliable) void ServerRequestAttack(int32 Index,uint32 RequestId);
    /** RequestId只处理当前预测；bAccepted/Confirmed来自服务器结果，RecoveryUntil为服务器可再攻击秒数，失败回到待机。 */
    UFUNCTION(Client,Reliable) void ClientAttackResult(uint32 RequestId,bool bAccepted,FDemoMeleeState Confirmed,double RecoveryUntil);
    /** RequestId为这次真实新按键，ParentOwnerRequestId锚定正在播放的服务器父刀；不接收时间/phase/任意攻击索引。 */
    UFUNCTION(Server,Reliable) void ServerQueueCombo(uint32 RequestId,uint32 ParentOwnerRequestId);
    /** 单独确认排队事实，不能当作下一GA/伤害已启动；迟到响应只匹配本次pending或其已预测后继，不复活取消。 */
    UFUNCTION(Client,Reliable) void ClientComboQueueResult(uint32 RequestId,uint32 ParentOwnerRequestId,bool bAccepted,FDemoMeleeState Confirmed,double RecoveryUntil);
    /** 本人菜单取消请求，不允许指定其他Actor。 */
    UFUNCTION(Server,Reliable) void ServerCancelAttack();
    /** 状态到达时恢复表现；重复调用不重复创建网格或重播旧声音。 */
    UFUNCTION() void OnRep_State();
    /** Location为服务器检测到的非怪物接触点cm；仅播放环境声，怪物金属声由实际伤害回执统一处理。 */
    UFUNCTION(NetMulticast,Unreliable) void MulticastWorldImpact(FVector_NetQuantize Location);
    /** 装备/复制/Tick更新双视角；FP不投影，TP世界表示与本人正常尺度代理共用唯一隐藏投影策略；收起/阵亡关闭投影，所有表现网格无伤害碰撞。 */
    void UpdatePresentation();
    /** From/To为本帧跨越的归一化有效窗口，固定子步扫过完整棍段并执行权威GE。 */
    void SweepAttack(float From,float To);
    /** Hit为服务器Sweep结果；仅原Combat每目标一次Health GE，设施无弱点/击退/奖励；敌人命中前冻结状态交核心，成功接管后不叠普通击退。 */
    void ApplyHit(const FHitResult& Hit);
    /** 每次Tick/补扫/子步重验原阶段、关号和生命；Combat还要求原会话有效，安全区动作永不升级为伤害动作。 */
    bool IsAttackContextValid() const;
    /** Phase为0..1作者时刻；旧策略按服务器视角，新策略以真实握点映射密采弧线。
     * CachedBaked/CachedActual是本次Sweep同时提供的可空只读帧缓存；调试单采样为空时自行捕获，绝不跨帧持有。 */
    void TracePose(float Phase,FVector& Base,FVector& Tip,const FTransform* CachedBaked=nullptr,const FTransform* CachedActual=nullptr) const;
    /** 同步捕获本次Sweep的当前烘焙握框/实际TP武器世界框，两个输出均为值；只在显式握点策略使用，不读取FP或改变骨姿。 */
    void CaptureTraceFrame(FTransform& Baked,FTransform& Actual) const;
    /** 返回GameState同步秒数；World存在但GameState未就绪时用本World秒数。 */
    double ServerTime() const;
    /** Owner只在游戏线程借用，卸载时允许空。 */
    ADemoCharacter* Character() const;
    /** 返回纯表现快照；拥有者预测与复制状态分离，伤害函数禁止读取此入口。 */
    const FDemoMeleeState& PresentationState() const;
    /** 拥有者预测使用本World单调游戏秒，其他视图使用同步服务器秒；防时钟校正使本地动作倒退。 */
    double PresentationTime() const;
    /** Index为输入类别、Cursor为候选循环位置，返回实际招式下标；缺失ID返回INDEX_NONE。 */
    int32 ResolveInputAttack(int32 Index,int32 Cursor) const;
    /** 本机非权威输入调用；与服务器共用DA收招后组首策略，创建带关联号的暂时表现并发送请求，绝不调用ApplyHit或改权威游标/ASC属性。 */
    void PredictAttack(int32 Index);
    /** 本机取消时清空动作/缓冲但保留已产生的冷却；屏蔽随后到达的旧确认，不发起新攻击。 */
    void CancelPredictedPresentation(bool bExplicitCancel=true); // false仅权威自然结束收敛，保短时末刀回执；true用于明确取消/拒绝。
    /** Index为有效输入、ReadyAt为已有动作冷却截止；仅本机最后InputBufferSeconds内缓存一次，不刷新过期时间。 */
    bool BufferLocalInput(int32 Index,double ReadyAt);
    /** Tick消费本机或服务器有界队列；验证阶段/关号/菜单/生命，先出队再请求，远端请求仅在真实结果后确认。 */
    void ConsumeBufferedInput();
    /** 返回该快照本刀原作者结束phase；可选变体仅改变已过伤害窗口的尾段。 */
    float SegmentEndPhase(const FDemoMeleeState& Snapshot) const;
    /** 返回快照自身时基的计划结束时刻；严禁用当前帧Now重置长帧overshoot。 */
    double SegmentEndTime(const FDemoMeleeState& Snapshot) const;
    /** 数学出口已到但Timer/复制尚未切刀时，只表现续采完整Combo源最多0.1秒；Time/Progress为该资源源秒/父规范相位。
     * Snapshot/Now采用同端时基，bFirstPerson选表现资源；取消/死亡/本机菜单/阶段失效即拒绝，不触发GA、声音或碰撞。 */
    bool GetCommittedComboContinuation(const FDemoMeleeState& Snapshot,double Now,bool bFirstPerson,float& Time,float& Progress) const;
    /** GetPose在有效子刀段调用；Snapshot/ChildProgress为当前表现事实/子刀0..1作者phase，bFirstPerson选择FP/TP。
     * 成功只返回组件持有的父完整资源及源秒Time；来源ID/Entry/长度不符即返回空，不改变Action/Serial/GA；目标可以有后继。
     * 不按bComboExitCommitted拒绝，供同刀兼容前缀资格只读复核来源；GetPose只在未承诺下一刀时选择此资源。
     * 晚确认可关闭bStartedFromCombo的零混合，但不能抹掉合法ComboSourceAttack/Entry事实；普通/取消无有效子刀来源，不使用此路由。 */
    UAnimSequence* GetFullContextComboPose(const FDemoMeleeState& Snapshot,bool bFirstPerson,float ChildProgress,float& Time) const;
    /** 只读0..Branch输入资格与主循环真实目标；Now必须与Snapshot.StartTime同一时基。 */
    bool CanQueueCombo(const FDemoMeleeState& Snapshot,double Now,int32& Target) const;
    /** 本机新点击先试提前连招，单缓存且不延长；服务器本机直接承诺，远端拥有者发送锚定父刀的可靠请求。 */
    bool TryQueueLocalCombo(int32 Index);
    /** 服务器接收本机/RPC后设置承诺及队列，不推进游标/序号；0请求号仅限房主本机直接调用。 */
    bool QueueAuthorityCombo(uint32 RequestId,uint32 ParentOwnerRequestId);
    /** 清空唯一输入槽及其所有关联/过期元数据，避免普通缓冲与连招队列互相继承。 */
    void ClearBufferedInput();
    /** 拥有者在计划ComboEnd消费一次旧pending，预测新Entry；不再发送第二次起招RPC。 */
    bool AdvancePredictedCombo();
    /** 被拒后继已本地预测时恢复当前权威父刀/Idle，保取消闩锁、抑制旧声音，不回到新刀前摇重播。 */
    void RollbackComboPrediction(const FDemoMeleeState& Confirmed,double RecoveryUntil);
    UPROPERTY(Transient) TObjectPtr<UDemoMeleeDefinition> Definition; // 运行时保活，永不修改共享配置。
#if WITH_EDITOR
    // 仅本Avatar组件生命周期的非反射只读诊断闸；默认false，BeginPlay仅在-game/Standalone/GameWorld及两显式回归参数齐全时置true。
    // true只让GetFullContextComboPose退回原canonical选择；不改DA开关/资源/时钟/伤害，不复制、不保存、无CVar或公开setter。
    bool bEditorDisableFullContextForProbe=false;
#endif
    UPROPERTY(ReplicatedUsing=OnRep_State) FDemoMeleeState State; // 权威姿态及时间一并复制，避免字段错配。
    FDemoMeleeState PredictedState; // 仅拥有者的表现快照，不复制，不作为服务器碰撞输入。
    bool bUsePrediction=false; // 首次预测后保持拥有者表现通道，包括取消后的待机，防旧OnRep让被取消动作复活。
    bool bPredictionCancelled=false; // 当前关联号已主动取消/被拒绝；晚到确认只能记录，不能重新播放。
    uint32 NextRequestId=0,CurrentInputRequest=0; // 前者拥有者单调分配（跳过0）；后者仅服务器RPC->GA同步栈关联，返回立即清零。
    uint32 LocalPresentationSerial=0; // 本机每次新预测递增，拒绝后的重试也能触发同序列过渡；不用于权威伤害去重。
    double LocalRecoveryUntil=0.; // 拥有者预测冷却截止；确认不倒退动作时钟，主动取消不能提前清零。
    EDemoPhase PredictedPhase=EDemoPhase::Lobby; // 预测开始时的客户端阶段，只用于变化时撤回表现。
    int32 PredictedLevel=0; // 预测开始的关号，阻止旅行后延续旧动作表现。
    int32 BufferedInput=INDEX_NONE; // 每组件最多一个待执行输入；本机尾部点击，或服务器冷却末尾稍早到达的拥有者RPC。
    uint32 BufferedRequestId=0; // 服务器延后确认的远端请求号；0为本机输入，必须在实际GA启动/拒绝/取消时返回结果。
    bool bBufferedCombo=false; // 同一输入槽的提前连招类型；false仍为旧尾部普通点击缓冲。
    uint32 BufferedSourceSerial=0; // 当前端父刀序号，阻止队列跨过另一次起招；本地预测与服务器序号不混用。
    uint32 BufferedParentRequestId=0; // 远端父刀OwnerRequestId，跨RPC关联不使用本机预测Serial。
    int32 BufferedTargetAttack=INDEX_NONE; // 接受时冻结的主循环目标，只由已验证DA解析。
    uint32 LastComboRPCRequest=0; // 服务器已处理最新连招请求号，防旧请求在后来刀上重新承诺；不是持久化进度。
    uint32 LastQueuedComboRequest=0,LastQueuedComboParent=0; // 本机最近真实连招点击；队列超时后仍可识别服务器实际起刀结果，显式取消清除。
    uint32 UnconfirmedComboRequest=0,UnconfirmedComboParent=0; // 最多一刀已推进但起招确认未到；其后点击仍可单缓存，不在未知权威链上无限预测。
    FDemoMeleeState PendingConfirmedComboStart; // 服务器实际起刀已早到时暂存的只读事实；拥有者继续当前桥到本地计划出口再消费，不复制/保存。
    bool bHasPendingConfirmedComboStart=false; // 仅当前唯一输入槽的已确认后继有效，清槽/取消即清，不能在之后动作复活。
    uint32 PendingConfirmedComboParent=0; // 暂存实际起刀的父OwnerRequestId，避免早到确认附到另一刀。
    mutable uint32 LoggedEntryClampSerial=0; // 游戏线程只读表现接口的诊断去重号；每刀最多记录一次入口校时钳制，不参与玩法/复制。
    mutable double IdlePoseEpoch=-1.; // 本组件同端PresentationTime秒；<0表示活动刀/未装备/失效，合法Hold首次查询才从0开始。
    mutable uint32 IdlePoseSerial=0,IdlePoseCancelledSerial=0; // 上次观察的表现刀号/权威撤销事实；整刀落在两次求值之间仍重新从中立Idle起步，不复制。
    mutable bool bIdleClockObserved=false,bIdleClockEquipped=false; // 区分首次观察与重复同帧查询；FP/TP共同消费，不各开一只呼吸时钟。
    mutable TWeakObjectPtr<UDemoMeleeDefinition> IdleClockDefinition; // 只比较本机DA身份，不保活旧配置，换定义不得继承上一套Idle相位。
    static constexpr double ComboPresentationGraceSeconds=.1; // 已承诺连招过界后的最大纯表现补位秒数；不改变原+.1秒输入消费/权威结束规则。
    double BufferedExpires=0.; // 本机缓冲绝对过期游戏秒，不因反复点击延长。
    EDemoPhase BufferedPhase=EDemoPhase::Lobby; // 记录排队时阶段，消费时必须一致。
    int32 BufferedLevel=0; // 记录排队时关号，跨关输入自动丢弃。
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ThirdPersonBat; // 原TP手骨刀供世界观察者；默认/回退投本人影，正常尺度代理接管时撤销原隐藏影；收起/阵亡无影、无伤害碰撞。
    UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> FirstPersonArms; // Pawn拥有的本地相机空间网格，不替换Mesh1P/GAS主实例。
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> FirstPersonBat; // 本地手骨的纯表现副棍，与FP双手同帧变换；没有复制或命中权限。
    TWeakObjectPtr<UDemoCombatSession> Session; // 仅Combat招式绑定可撤销凭据；Hub/Intermission为空且不可伤敌。
    EDemoPhase AttackPhase=EDemoPhase::Lobby; // 本次招式开始阶段，仅服务器使用；变化立即失效，不复制/保存。
    int32 AttackLevel=0; // 同一阶段内换关也不能沿用旧窗口；关号从服务器GameState冻结。
    int32 RequestedAttack=INDEX_NONE; // 仅服务器同步Request->GA调用栈中有效，防直接激活绕过输入验证。
    bool bRequestedHorizontal=false; // 同步请求到GA期间的输入类别；决定成功激活是否推进横挥循环，不复制/保存。
    int32 HorizontalCursor=0; // 服务端本Pawn下一横挥序号；Standalone可按DA从组首重选并仅在Begin成功后提交，连段沿原目标推进；拒绝/纵劈不推进，重生归零。
    float FrozenDamage=0.f; // 激活时基础伤害+成长，后续切换成长不改变本次攻击。
    FVector2D FrozenHitWindow=FVector2D::ZeroVector; // 每刀起始从TP通知或旧配置读取的作者phase区间；服务器私有，当前刀期间不遍历通知/追随编辑变更。
    float PreviousProgress=0.f; // 上次服务器已处理的phase，低FPS补扫有效窗口。
    double NextAttackTime=0.; // 服务器冷却截止，取消/切枪不会提前清零。
    TSet<TWeakObjectPtr<AActor>> HitTargets; // 每招式每敌一次；在GE同步回调前登记。
    bool bWorldImpactPlayed=false; // 本次击墙只播一次，避免密集子步重复音效。
    uint32 SoundSerial=0; // 本地音效游标，不复制。
    bool bSwingPlayed=false; // 当前序号挥声已播放或因迟到跳过。
    bool bFinishing=false; // 正常结束补扫期间的同步重入闩锁，清关/死亡不能再次结束同一个GA。
};
