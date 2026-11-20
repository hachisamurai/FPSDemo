// 职责：本人PC独立NoCollision赌场表现层、每键下压回弹与复用技美材质的真实轮廓；依赖原枪墙遮罩工厂，不移动共享Query或持有经济真值。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Economy/Gambling/DemoCasinoTypes.h"
#include "DemoCasinoLocalViewComponent.generated.h"
class ADemoCasinoDevice;
class UTextRenderComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UCameraComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UDemoCasinoDeviceProfile;
class APawn;
struct FDemoGambleProgress; // 只读本Owner已保存账本，恢复typed符号不解析展示文案。

/** 单键本机脉冲；仅数字/功能键位移，时间与幅度都不能反向驱动输入、付款或保存。 */
struct FDemoCasinoKeyPressState
{
    double StartedAt=0.; // 本次有效点击的单调真实秒，不使用暂停/服务器时间。
    float StartFraction=0.f; // 重按时从前一次曲线的当前0..1压入比例继续，不先跳回原位。
    float DownSeconds=.04f; // 点击时冻结的下压时长秒，避免实时调配置令半段动作跳变。
    float HoldSeconds=.01f; // 点击时冻结的底部短停秒，可为0。
    float ReturnSeconds=.1f; // 点击时冻结的回弹时长秒，结束准确回模型Rest。
};

UCLASS()
class FPSDEMO_API UDemoCasinoLocalViewComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无独立Tick；Input组件驱动本机显示，视觉单调时钟不创建第二个经济/服务器计时器。 */
    UDemoCasinoLocalViewComponent();
    /** 本人进入Operate时创建实际私有机械件/屏并恢复同次SavedProgress的typed耐久末帧；旧CP01才用Cube杆，公共中性件仅逐视图隐藏。 */
    void Open(ADemoCasinoDevice* Device,const FDemoGambleProgress& SavedProgress);
    /** 幂等撤私有层，保存事实不受关页影响；最后已提交回执供诊断保留到下一次Open。 */
    void Close();
    /** Input本地Tick调用以更新草稿/等待/已提交结果和视觉曲线，不读取服务器私有候选。 */
    void Refresh();
    /** 只接已Committed且相关的原回执，未保存失败不得替换上一已结结果。 */
    void AcceptCommittedResult(const FDemoCasinoResult& Result);
    /** 输入组件已确认可激活的真实键按下，仅记录独立本机脉冲；拉杆必须等Committed，不由此生成结果。 */
    void PressControl(FName ControlId);
    /** 20Hz本机细焦点更新；NAME_None/禁用时撤本人轮廓，仅可用键帽或拉杆描白外沿，不授权交易或改变Query。 */
    void SetFocusedControl(FName ControlId,bool bCanActivate);
    // 仅本机按键曲线，独立于Profile拉杆/滚轴时钟；时长冻结到每次有效点击，非复制/存档字段。
    // 快速下压秒，范围[0.01,0.25]；非法运行值钳制或使用0.04默认，视觉失败不改变下注。
    UPROPERTY(EditAnywhere,Category="Casino|Key Feedback",meta=(ClampMin="0.01",ClampMax="0.25",Units="s")) float KeyDownSeconds=.04f;
    // 底部短停秒，范围[0,0.2]；0为无停留，不影响一次Started只记一个数字。
    UPROPERTY(EditAnywhere,Category="Casino|Key Feedback",meta=(ClampMin="0",ClampMax="0.2",Units="s")) float KeyHoldSeconds=.01f;
    // 柔和回弹秒，范围[0.01,0.5]；完成后精确回Rest而非累加位移。
    UPROPERTY(EditAnywhere,Category="Casino|Key Feedback",meta=(ClampMin="0.01",ClampMax="0.5",Units="s")) float KeyReturnSeconds=.1f;
    /** 最后实际消费的已提交回执，便于真实夹具对比符号，不是经济真值。 */
    const FDemoCasinoResult& GetLastResult() const;
#if WITH_EDITOR
    /** 隔离正式模型夹具只读本人真实机械组件World变换；不是计算目标，不创建部件或改变表演/经济。 */
    bool ReadModelPartPoseForAudit(FName PartId,FTransform& WorldTransform) const;
    /** 只读实际注册遮罩、Renderer本请求及实际Camera的Composite MID；不是像素可见/捕获提交验收，退出/禁用/缺资源返回false。 */
    bool ReadFocusedOutlineForAudit(FName& OutControlId,FString& OutMaterialPath,FTransform& OutWorldTransform) const;
#endif
    /** 控制器卸载清本机独立组件，不销毁共享机台。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** 纯值读取指定脉冲在Now真实秒的0..1压入量；平滑下压/回弹，不更新状态或调用输入。 */
    float EvaluateKeyPress(const FDemoCasinoKeyPressState& Press,double Now) const;
    /** 将唯一焦点映射实际活动Mesh，附着原253遮罩后交PC LocalOutline捕获/合成；resize不逐帧重造mask，不改Query。 */
    void RefreshFocusOutline();
    /** 先按本Requester清LocalOutline借用，再销毁本人遮罩；换键/关闭/Avatar失效不清他人的后处理或源Mesh。 */
    void ClearFocusOutline();
    /** 正式Profile全部活动件按rest/pivot更新；不移动公共中性件或Query。 */
    void RefreshModelParts();
    UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> Screen; // 本PC服务保活、Primitive归当前Pawn以匹配OnlyOwnerSee/ViewActor；NoCollision且不复制金额。
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Lever; // 私有NoCollision装饰，旋转不改变Device查询件。
    UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> FocusOutline; // 当前Pawn拥有的OnlyOwnerSee私有Stencil253遮罩，附着唯一选中活动件；不画Main/普通Depth、不碰撞/复制。
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FocusOutlineMaterial; // 本Open首次合法焦点从可信目录载入的主Composite，只读原资产。
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FocusOutlineMaskMaterial; // 同次一次载入的私有Capture最终Mask PP，缺失不回退旧pass。
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FocusOutlineBlendable; // 借LocalOutline当前请求的Composite MID作诊断缓存，不直接增删相机PP。
    TWeakObjectPtr<UCameraComponent> FocusOutlineCamera; // 本请求的实际Camera，换Avatar/Camera由Renderer精确清理。
    TWeakObjectPtr<UStaticMeshComponent> FocusOutlineSource; // 当前本人活动键帽/拉杆Mesh；弱引用不阻止卸载，源材质/Stencil不改。
    UPROPERTY(Transient) TObjectPtr<UDemoCasinoDeviceProfile> ModelProfile; // Open时冻结已验证资源，绑定版本变化由Input先退出。
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> ModelParts; // 与Profile.Parts同索引，仅活动件创建本Pawn拥有的OnlyOwnerSee克隆。
    TArray<TWeakObjectPtr<UPrimitiveComponent>> HiddenByThisView; // 仅记录本服务实际插入PC隐藏表的公共中性件，Close逐项撤回，不清整表。
    TWeakObjectPtr<APawn> PrimitiveAvatar; // Open时创建者，换Pawn/ViewTarget时撤旧层。
    TArray<float> ReelAngles; // 3个当前视觉角deg；非权威、不持久化。
    TArray<float> ReelStartAngles; // 接收Committed时冻结当前角，连续表演不跳回零位。
    TMap<FName,FDemoCasinoKeyPressState> KeyPresses; // 仅当前Profile存在的Key可入表，最多14项；跨键独立回弹，无UObject/网络数据。
    FName FocusedControl=NAME_None; // 仅本机已解析的稳定控件ID；关闭/换机清理，不作权威凭据。
    bool bFocusedCanActivate=false; // 对应本机UI可用状态；禁用键不描可用白沿，灰色准星/原因仍由HUD处理，交易另由服务器复核。
    FName OutlinedControl=NAME_None; // 实际已创建遮罩对应的稳定ID，仅本机表现诊断，不是授权凭据。
    bool bFocusOutlineLoadAttempted=false; // 当前Open只一次加载Composite/Mask及域检查，缺失不逐帧读盘；下次Open再尝试。
    bool bWarnedKeyTiming=false; // 一次Open至多一次非法表现配置警告，普通逐帧/连点不刷日志。
    TWeakObjectPtr<ADemoCasinoDevice> Device; // 同World借用，弱指针不阻止卸载。
    FDemoCasinoResult LastResult; // 已提交公开结果，无Mine私有雷图。
    double RevealStarted=0.; // 本地单调秒，仅表现，不能作为付款时钟。
    FString LastText; // 避免逐帧重设同一Text/日志，不进入存档。
};
