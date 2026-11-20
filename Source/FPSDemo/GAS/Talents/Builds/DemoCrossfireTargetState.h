// 职责：B05目标上的最小公开反馈。使用：服务器按来源PS精确登记/撤销最多4个方向与截止；客户端为各本地摄像机创建Screen Widget，不保存预算或钱包。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Blueprint/UserWidget.h"
#include "DemoCrossfireTargetState.generated.h"
class ADemoEnemy;
class ADemoPlayerState;
class UDemoRunBuildComponent;
class UTextBlock;
class UWidgetComponent;

/** 允许全队读取的目标事实；无私有伤害预算、分支、经济、账号或会话正文。 */
USTRUCT()
struct FDemoCrossfirePublicSource
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<ADemoPlayerState> OwnerState; // 仅公开参战PS身份，客户端不获得购买/成长数据。
    UPROPERTY() float EndTime=0.f; // 服务器World秒截止，客户端只显示不裁决伤害。
    UPROPERTY() bool bReverse=false; // true显示“破势·近战”，false显示“破势·射击”。
};

/** 纯文字目标反馈；不接收输入，不修改目标状态，复用既有R04中文字体。 */
UCLASS(Transient)
class FPSDEMO_API UDemoCrossfireTargetWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    /** Text由本地公开快照格式化，样式创建一次，之后只改文本。 */
    void SetFeedbackText(const FText& Text);
protected:
    /** 创建唯一TextBlock，无绑定Tick/输入业务；与普通菜单栈无关。 */
    virtual void NativeOnInitialized() override;
private:
    /** 确保原生WidgetTree存在，防先写Text后首次Slate构建时丢内容。 */
    void EnsureLabel();
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Label; // 当前WidgetTree拥有，随本地Widget销毁。
};

UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoCrossfireTargetState : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 动态目标组件默认复制；只在有标记时以10Hz检查最多4条，不扫描全场敌人。 */
    UDemoCrossfireTargetState();
    /** Target为同步借用的真实敌人；bCreate仅服务器可创建，客户端只读现有复制组件。 */
    static UDemoCrossfireTargetState* Find(ADemoEnemy* Target,bool bCreate);
    /** 原策略成功起手后登记来源；EndTime/Reverse由服务器冻结，最多4个来源。 */
    bool SetSource(UDemoRunBuildComponent* Source,float EndTime,bool bReverse);
    /** 策略消费/替换/死亡/离场时仅撤销自己PS的槽，不清其他玩家标记。 */
    void RemoveSource(UDemoRunBuildComponent* Source);
    /** 绑定本目标真实死亡边沿；不从反馈创建伤害或奖励。 */
    virtual void BeginPlay() override;
    /** 服务器有限清理，客户端按各本地相机遮挡/距离显示同一个最早有效来源。 */
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
    /** 目标销毁/流送卸载停止反馈并解绑，不留下WidgetComponent或跨World引用。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** 仅公开Sources值；Marker Widgets不复制，分别由各客户端本地创建。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    /** 目标已经死亡时立即撤公开反馈；原策略仍按原攻击回执消费自己的私有凭据。 */
    void OnTargetDefeated(ADemoEnemy* Target);
    /** 删除到期/阵亡/离场来源；变更一次ForceNetUpdate，不按Tick复制整个钱包。 */
    void PruneSources();
    /** 复制与服务器本地共用刷新；屏幕空间文字天然面向每个LocalPlayer摄像机，不旋转共享敌人Actor。 */
    UFUNCTION() void OnRep_Sources();
    /** 创建或更新每个本地玩家的一份小标签；同目标只展示最早有效来源和总标记数。 */
    void RefreshWidgets();
    /** 销毁本组件创建的全部本地标签，空标记时停止Tick。 */
    void DestroyWidgets();
    /** 统一服务器时钟秒，客户端使用GameState同步时钟而非本机启动偏移。 */
    float ServerTime() const;
    UPROPERTY(ReplicatedUsing=OnRep_Sources) TArray<FDemoCrossfirePublicSource> Sources; // 最多4条脱敏公开事实。
    UPROPERTY(Transient) TArray<TObjectPtr<UWidgetComponent>> Markers; // 每个本地视口一份屏幕标签，绝不复制玩法权限。
    FVector MarkerOffset=FVector(0,0,130.f); // 首次初始化从目标包围盒冻结的厘米高度，避免把自己标签算回包围盒。
};
