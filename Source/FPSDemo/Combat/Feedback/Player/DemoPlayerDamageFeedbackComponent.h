// 职责：受害者PC拥有的受击回执、限额与本机表现状态；原PS ASC仍是HP真值，不授权伤害、不保存进度。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/Feedback/Player/DemoPlayerDamageTypes.h"
#include "DemoPlayerDamageFeedbackComponent.generated.h"
class APlayerController;
class ADemoPlayerController;
class UDemoPlayerDamageFeedbackProfile;
class UAudioComponent;
class USoundWave;
class UDemoDamageCameraModifier;
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoPlayerDamageFeedbackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 创建PC默认复制子对象，无Actor Tick；仅活跃本机包络使用短Timer。 */
    UDemoPlayerDamageFeedbackComponent();
    /** 沿真实PC找本人的组件；不使用Player0或服务器GameMode。 */
    static UDemoPlayerDamageFeedbackComponent* Find(const APlayerController* PC);
    /** 服务器/本机Possess或复制就绪调用，幂等建立原子Avatar绑定。 */
    void RefreshAvatarBinding();
    /** Pre执行时冻结原Avatar代次；失败0，不给另一个Pawn授权。 */
    uint32 GenerationFor(const ADemoCharacter* Pawn);
    /** 服务器已确认的真实HP事实；即便本次致死已经撤销Session也允许提交，客户端自行处理死亡优先级。 */
    void Submit(const FDemoPlayerDamageCue& Cue);
    /** 仅原攻击成功躲避分支调用，普通飞弹不能凭Dash标签伪造成功。 */
    void NotifyEvaded();
    /** 阵亡/复活/退出主动撤销本组件资源；服务器阵亡还会使旧Avatar代次失效。 */
    void ResetForLifecycle(bool bInvalidateAvatar=false);
    /** HUD/最终相机同步只借用本机视图，不改变HP。 */
    const FDemoPlayerDamageView& GetView() const;
    /** 建立低频生命周期监视与单次初始化Profile，远端服务器不创建声音/相机。 */
    virtual void BeginPlay() override;
    /** 卸载撤销所有Timer/音频/自己添加的相机Modifier。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FDemoPlayerDamageContractTest; // 只读/纯值自动化门，生产网络接口保持封闭。
#endif
    /** 不可靠表现批次，不参与伤害/死亡/成长；Listen同样只经此入口显示一次。 */
    UFUNCTION(Client,Unreliable) void ClientDamage(const TArray<FDemoPlayerDamageCue>& Batch);
    /** 整条可靠生命周期消息；true+null不绑定当前Pawn，等拥有者携带已映射Pawn的握手补发。 */
    UFUNCTION(Client,Reliable) void ClientBindAvatar(const FDemoPlayerDamageAvatar& Binding);
    /** 本机当前Pawn已到时请求服务器核对同一身份；限4次/秒，只补发本人绑定，无伤害或进度能力。 */
    UFUNCTION(Server,Reliable) void ServerRequestBinding(ADemoCharacter* ObservedPawn);
    /** 服务器唯一延迟发送回调；上限8项，合并仅影响表现。 */
    void Flush();
    /** 每0.1秒检查本人生命/菜单/阶段与实际低血，不逐帧扫描场景。 */
    void Maintain();
    /** 活跃本机每0.02秒更新有限包络；过期后停Timer，不能持续创建UObject。 */
    void Animate();
    /** 单条回执验证/去重/短暂等待新代次；只接受当前本人可见战斗视角。 */
    void Receive(const FDemoPlayerDamageCue& Cue,bool bAllowDefer);
    /** 64位窗口支持有界乱序与uint32回绕，重复表现不播放。 */
    bool AcceptSequence(uint32 Sequence);
    /** 当前本机视角/阶段/生命门禁，菜单不取消真实受伤。 */
    bool CanPresent() const;
    /** 当前PC只读借用；组件不接管控制器生命周期。 */
    ADemoPlayerController* PC() const;
    /** 服务器同步World秒，客户端使用GS校准秒。 */
    float ServerNow() const;
    /** 按结果挑源并播放复用固定声部；播放失败不影响HUD。 */
    void PlayContact(const FDemoPlayerDamageCue& Cue,int32 Tier);
    /** 初始化固定2接触＋1人声＋1循环音频组件，无设备时安全静音。 */
    void EnsureLocalResources();
    /** 本机活动Timer幂等启停，不重置窗口。 */
    void EnsureAnimation();
    /** 仅清装饰状态与自己声音，不动Avatar绑定/延迟接收队列。 */
    void ClearPresentation();
    UPROPERTY(Transient) FDemoPlayerDamageAvatar Avatar; // 服务端真绑定/本机可靠快照；不使用拆字段的属性复制。
    UPROPERTY(EditDefaultsOnly,Category="Player Damage") TSoftObjectPtr<UDemoPlayerDamageFeedbackProfile> ProfileAsset; // Cook软引用，BeginPlay一次加载。
    UPROPERTY(Transient) TObjectPtr<UDemoPlayerDamageFeedbackProfile> Profile; // 本组件使用只读配置，缺失回退Native默认。
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Contacts; // 两个固定本机声部。
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Voice; // 单个人声组件，最多1s一次。
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Breathing; // 一个低血循环，切视角/设置/离场停止。
    UPROPERTY(Transient) TObjectPtr<UDemoDamageCameraModifier> CameraModifier; // 只移除本组件安装的Modifier。
    TArray<FDemoPlayerDamageCue> Pending; // 服务器最多8项待发，不影响HP。
    struct FDeferred { FDemoPlayerDamageCue Cue; float Until=0.f; }; // 本机未来代次最多4项、0.2秒等待。
    TArray<FDeferred> Deferred;
    FDemoPlayerDamageView View; // 本机HUD只读状态，不复制/保存。
    FTimerHandle MaintenanceTimer,AnimationTimer,FlushTimer; // UObject弱绑定回调，EndPlay全部清除。
    uint32 NextSequence=0,LastSequence=0; // 服务器发号/本机去重，各只在游戏线程写。
    uint64 Seen=0; // 64次表现去重窗口，无Actor引用。
    uint32 ObservedPhase=0; // 本机阶段边沿清理。
    float LastSend=-10.f,LastContact=-10.f,LastVoice=-10.f; // World秒，音频限频。
    float NextBindingRequest=0.f,LastBindingReply=-10.f; // 客户端请求/服务器回复各最多4Hz，未知Pawn不无限分配队列。
    float EdgePeak=0.f,EdgeStarted=-10.f,RollPeak=0.f,RollStarted=-10.f,TrailStarted=-10.f; // 有限包络，不无限叠加。
    float TrailBase=0.f; // 本批残影起点HP，不把每帧结果重复当起点。
    bool bLow=false,bCritical=false,bResourcesReady=false; // 低血滞回与一次资源建立标记。
    int32 LastLight=INDEX_NONE,LastHeavy=INDEX_NONE,LastPain=INDEX_NONE; // 相邻不重复源索引，非网络随机。
};
