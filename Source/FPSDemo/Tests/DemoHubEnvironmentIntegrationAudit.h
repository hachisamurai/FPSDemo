// 职责：仅显式Editor -game Standalone的正式Hub环境集成回归。使用-DemoHubEnvironmentAudit；不改生产Bootstrap/资产/经济/云。
// 依赖正常RunSaves/RunFlow、持久EnvironmentRoot、原三枪七机与CharacterMovement；自动输出本GI独立证据并退出本进程。
// 额外-DemoHubEnvironmentLightingAB只作进程内23局部灯强度单变量四图诊断，无资产保存/全局曝光修改，不授新资产验收。
// 独立-DemoHubEnvironmentWallWashColorAB只作唯一Gun_WallWash温度3400K→5000K两图诊断，与LightingAB互斥；退出恢复原温度并核23灯。
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoHubEnvironmentIntegrationAudit.generated.h"

class ADemoCharacter;
class ADemoPlayerController;
class ADemoCasinoDevice;
class ADemoGunDisplay;
class UDemoHubShopDefinition;
class ULocalLightComponent;
class FJsonObject;

/** 独立有界测试WorldSubsystem；普通游戏/PIE/非Editor目标不会创建实例或注册调试命令。 */
UCLASS()
class FPSDEMO_API UDemoHubEnvironmentIntegrationAudit : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    /** Outer必须是显式-game、独立Game World；非Editor编译固定false，不进入普通会话。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** 仅Game类型；PIE/Editor/Preview即使带参数也不创建会退出进程的夹具。 */
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    /** 再核实际NetMode后创建唯一证据目录；InWorld归引擎，不保留跨World引用。 */
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    /** 仅本次有界状态机执行实际走路/只读射线/正常RHI截图；不提交交易或制造结果。 */
    virtual void Tick(float DeltaTime) override;
    /** 高频只读门；结束/未启用时不再Tick，不在普通World后台轮询。 */
    virtual bool IsTickable() const override;
    /** 引擎Tick统计标识；没有业务状态或额外系统依赖。 */
    virtual TStatId GetStatId() const override;
    /** 卸载停止驱动，正在结束的World不请求新退出/保存/截图。 */
    virtual void Deinitialize() override;
private:
    /** 启动/每次截图前实测项目盘至少10GiB余量；Stage只作本岗证据，不清理文件或扩大输出。 */
    bool CheckOutputBudget(const FString& Stage);
    /** 追加有界断言，首次失败转失败态，下一次Tick统一写报告并退出本测试进程。 */
    bool Check(bool bCondition,const FString& Message);
    /** 正常Hub就绪后实读一套环境/旧外观零件/16锚点与原DA三枪七机根，不改任何被测对象。 */
    bool ValidateActualHub(const UDemoHubShopDefinition* Definition);
    /** 将当前真实Pawn置于可碰撞测试站位，停旧速度并设正常ControlRotation；LocalPoint为Hub局部cm。 */
    bool PlacePawn(const FVector& LocalPoint,const FVector& LocalLookAt);
    /** 当前Pawn从指定西门外起步；bCasino选择原DA对应屋，位移由正常CharacterMovement每帧消费。 */
    bool BeginDoorWalk(bool bCasino,const UDemoHubShopDefinition* Definition);
    /** 按实际World方向AddMovementInput，达到实际行程/门内位置才通过；不能Teleport到终点假报走通。 */
    bool AdvanceDoorWalk();
    /** 排队真实FScreenshotRequest；Name为本次固定文件短名，完成前不排下一张。 */
    void BeginCapture(const FString& Name,int32 NextStep);
    /** 只在下一真实帧且实际PNG落盘后记证；无SceneCapture/贴图重绘或假文件。 */
    bool AdvanceCapture();
    /** 仅显式LightingAB：首次从当前闭包冻结23灯弱引用/稳定ID/原值，bReduced选择原值或原值/16，绝不反复乘当前值。 */
    bool ApplyLightingDiagnostic(bool bReduced);
    /** Finish/失败/超时/Deinitialize幂等恢复仍有效且同World的冻结原值，不保存资产；返回是否完整恢复。 */
    bool RestoreDiagnosticLights();
    /** 仅显式ColorAB：首次冻结当前23灯实际属性，A只读原态，B只对唯一稳定ID墙洗灯SetTemperature(5000)。 */
    bool ApplyWallWashColorDiagnostic(bool bCandidate);
    /** 实读23灯与首次原态比较，仅目标温度可等于ExpectedTemperature；Stage写入有界证据，不修改灯或原快照。 */
    bool ValidateWallWashColorLights(float ExpectedTemperature,const FString& Stage);
    /** 所有退出路径幂等恢复仍有效同World目标的首次温度，实读温度及23灯原态；不写强度或其他属性。 */
    bool RestoreWallWashColorDiagnostic();
    /** 准备一条只读枪墙射线，用实际FocusPoint与Pawn共享眼位，不绕过真实Visibility遮挡。 */
    bool PrepareGunRay(int32 Index,const UDemoHubShopDefinition* Definition);
    /** 当前枪墙目标真实首阻挡必须是对应原注册Display；控视向晚更新时有界重瞄，不改Query。 */
    bool AdvanceGunRay();
    /** 用正式S01稳定ControlId对应实际Query中心准备只读首命中；不打开租约/不下注。 */
    bool PrepareControlRay(int32 Index);
    /** 校验正式ValidateControlHit及FindHit解析均匹配同一稳定ID；不忽略环境、邻键或壳。 */
    bool AdvanceControlRay();
    /** 一次性写JSON与真实截图路径，停止Pawn输入并仅退出本显式Standalone进程。 */
    void Finish(bool bSuccess);

    bool bActive=false; // 仅当前显式测试Game World持有；普通目标恒false。
    bool bFinished=false; // 报告/退出只执行一次，结束后IsTickable=false。
    bool bFailed=false; // 首次失败后停止驱动生产输入，不继续累计假通过。
    bool bLightingAB=false; // 仅显式Editor Standalone诊断模式，默认false；不保存包，不属于运行验收。
    bool bWallWashColorAB=false; // 仅显式Editor Standalone独立两图色温诊断；与bLightingAB互斥，不授权艺术定稿。
    bool bWallWashColorRestored=true; // 冻结原态后false；只有目标温度实读及23灯原态复核完整通过才true。
    TArray<TWeakObjectPtr<ULocalLightComponent>> ColorDiagnosticLights; // 当前World23灯弱引用，保持稳定ID配对，不延长组件生命。
    TArray<FName> ColorDiagnosticLightIds; // 与弱引用同序的实际EntryId；目标只按唯一精确ID选取。
    TArray<TSharedPtr<FJsonObject>> ColorOriginalLightRecords; // 首次实读不可变JSON属性快照；期望候选另复制，不篡改此原态。
    int32 ColorTargetIndex=INDEX_NONE; // 唯一Gun_WallWash在上述数组中的索引；不按Label或数组固定序号猜目标。
    float ColorOriginalTemperature=0.f; // 冻结目标原温度K，必须实读3400；仅本World诊断及退出恢复使用。
    bool bDiagnosticLightsRestored=true; // 首次改灯设false；每个退出/切阶段恢复后true，避免World销毁后重复解引用。
    TArray<TWeakObjectPtr<ULocalLightComponent>> DiagnosticLights; // 只本World闭包23灯弱引用，不保活或触及全局光。
    TArray<FName> DiagnosticLightIds; // 与弱引用等长的稳定EntryId，不依赖ActorLabel或当前枚举顺序。
    TArray<float> DiagnosticOriginalIntensities; // 与弱引用等长的首次原值；所有A/B和恢复都从此值计算。
    TMap<FName,FTransform> DiagnosticOriginalViews; // 各诊断A实际眼位/旋转，B实测需同位同向；ColorAB只枪店，不持有相机对象。
    TMap<FName,float> DiagnosticOriginalFovs; // 各诊断A实际FOV角度，与B按原值比较；ColorAB另核90度，不靠启动参数猜测。
    int32 Step=0; // 有界状态机，代码中固定步骤，无网络/存档语义。
    double StartedRealAt=0.; // 单调秒总90秒上限，暂停/加载不允许无限挂起。
    double NextRealAt=0.; // 单调秒阶段等待，不伪造World时钟或修改表现时长。
    uint64 FrameCount=0; // 只计本夹具Tick，用于证实截图请求后经过真实帧。
    FString EvidenceDirectory; // Saved下GUID独占目录，不覆盖旧失败或玩家存档。
    TArray<FString> Checks; // 有界断言，不保存逐帧状态或私人经济/Token。
    TSharedPtr<FJsonObject> Facts; // 只含环境/Transform/读距/运行测量，不含私有金额或符号。
    FTransform HubFrame; // 从当前真实环境根与Blockout交叉核对的世界框，cm/四元数/scale1。
    TWeakObjectPtr<ADemoPlayerController> Controller; // 当前World本地PC弱引用，Pawn变化即重新核对。
    TWeakObjectPtr<ADemoCharacter> Player; // 当前PC拥有Pawn，不强持有旧Avatar。
    TWeakObjectPtr<ADemoCasinoDevice> Slots; // 当前原注册Mode1设备，绝不再生成一台。
    TArray<TWeakObjectPtr<ADemoGunDisplay>> GunTargets; // 顺序只用于遍历已由稳定ID核对的三个目标。
    TArray<FName> ControlIds; // 正式15个稳定语义，查询真实组件；不增加任何控件行为。
    int32 TargetIndex=0; // 当前三枪/15Query遍历索引；切阶段重置，不当作业务ID。
    FVector WalkStart; // 实际Pawn起步世界cm，Teleport仅发生在门外起点。
    FVector WalkDirection; // 实际Hub +X单位世界水平向量，用CharacterMovement正常消费。
    double WalkStartedAt=0.; // 实际走路开始单调秒，8秒上限独立于总时限。
    FString WalkName; // 门路径证据短名，仅casino/gunshop。
    FVector CurrentRayPoint; // 当前真实Focus/Query中心，不是硬编码模型点。
    double RayStartedAt=0.; // 每条首命中最多2秒，对晚到正常相机帧有界重瞄。
    int32 RayAimCorrections=0; // 正常ControlRotation重瞄次数，只作事实，不放宽射线门限。
    FString PendingCapture; // 当前唯一FScreenshotRequest目标，未落盘不列完成。
    FString PendingCaptureName; // 上述文件对应固定审查视图名。
    uint64 CaptureRequestedFrame=0; // 请求时本夹具Tick计数，至少后续一帧才可完成。
    uint64 CaptureStartedFrame=0; // AB各镜头至少120个实际Tick，不能用等待墙钟替代渲染稳定帧。
    uint64 CaptureStartedEngineFrame=0; // 引擎GFrameCounter另记实际帧门；不是GPU历史/曝光收敛测量。
    double CaptureStartedAt=0.; // 单调秒开始稳定视点；普通0.4秒，AB至少3秒，不改World时钟。
    double CaptureRequestedAt=0.; // 实际FScreenshotRequest时刻；写盘3秒超时从请求计算，不被AB稳定等待耗尽。
    bool bCaptureRequested=false; // 先等待真实视角稳定，再请求引擎截图。
    int32 CaptureNextStep=0; // 当前截图真实落盘后才进入的固定后继步骤。
};
