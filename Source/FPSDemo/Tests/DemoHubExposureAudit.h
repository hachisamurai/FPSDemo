// 职责：仅显式Editor Standalone的Hub曝光view-step诊断；正常StartRun后每进程测一个房间。
// 使用：-game -DemoHubExposureAudit -DemoHubExposureRoom=Gun|Casino；仅主地图，输出Saved/HubExposure20261117/Runtime。
// 依赖原Player/RunSaves/Hub环境与公开SceneView接口；只临时借用Camera的SpeedDown两字段，不保存资产或改高亮。
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoHubExposureAudit.generated.h"

class ADemoCharacter;
class ADemoPlayerController;
class UCameraComponent;
class UDemoHubShopDefinition;
class FJsonObject;
class FJsonValue;
class FSceneViewExtensionBase;
struct FDemoHubExposureRenderState;

/** 只在显式独立测试进程创建；原态与SpeedDown6候选各预热8秒、出屋后0.1/12秒四图，不代表真实走门验收。 */
UCLASS()
class FPSDEMO_API UDemoHubExposureAudit : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 仅WITH_EDITOR、真实主图Game World、NM_Standalone及完整显式CLI；PIE/其它地图/普通游戏均拒绝。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** 不支持Editor/PIE/Preview World；参数为引擎创建时借用枚举。 */
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    /** 再核实际World与输出预算，创建唯一非进度证据目录；不创建业务替代路径。 */
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    /** GT有界状态机；DeltaTime为引擎帧秒，计时使用单调真实时间，不改World时钟。 */
    virtual void Tick(float DeltaTime) override;
    /** 只在本诊断活动且未结束时允许Tick；不在普通游戏轮询。 */
    virtual bool IsTickable() const override;
    /** 引擎Tick统计标识，无业务副作用。 */
    virtual TStatId GetStatId() const override;
    /** 停观察并恢复冻结的两字段；不整包覆盖PP，防止删除其他组件的Blendables。 */
    virtual void Deinitialize() override;
private:
    /** 追加有界断言；失败后下一Tick统一结束。Message只含诊断事实，不含私有存档内容。 */
    bool Check(bool bCondition,const FString& Message);
    /** 每次截图前实测10GiB盘余量、当前输出总量<=64MiB；不清理他人文件。 */
    bool CheckBudget(const FString& Stage);
    /** 从实际唯一Blockout/EnvironmentRoot核HubFrame，冻结当前本机Camera两字段并创建只读View观察器。 */
    bool PrepareHub(const UDemoHubShopDefinition* Definition);
    /** Phase=0恢复原态，Phase=1只覆盖SpeedDown6；同房间同室内视点重新预热8秒。 */
    bool BeginPhase(const UDemoHubShopDefinition* Definition);
    /** 当前真实Pawn可碰撞Teleport到Hub局部cm并用正常PC控制朝向；是view-step，不声称走门。 */
    bool PlacePawn(const FVector& LocalPoint,const FVector& LocalLookAt);
    /** 精确恢复首次Camera SpeedDown与override位，核同World对象有效；幂等且不改其他PP字段。 */
    bool RestoreCamera();
    /** GT最多10Hz复制最新已完成RT纯值，记录旧帧/无buffer/0标量限制；bForce仅补阶段端点。 */
    void RecordSample(bool bForce);
    /** 在目标延迟后请求一次真实主Viewport截图，记录实际请求时刻，绝不标迟到图为0.1秒。 */
    bool RequestCapture(const FString& Name,double TargetDelay);
    /** 下一真实Tick且PNG写入后记录实际写盘延迟；3秒超时失败，不排重叠请求。 */
    bool AdvanceCapture();
    /** 停观察/恢复/写唯一JSON并仅退出已授权独立测试进程；bSuccess不等于游戏或艺术验收。 */
    void Finish(bool bSuccess);

    bool bActive=false; // 当前显式Game World活动门；普通目标恒false。
    bool bFinished=false; // 防止重复写报告/退出。
    bool bFailed=false; // 首次失败后停止正常诊断推进。
    bool bCasino=false; // CLI选择本进程唯一房间，不在一次运行混合两室历史。
    bool bFrozen=false; // 已捕获本Camera两字段的首次原态。
    bool bOriginalOverride=false; // 首次bOverride_AutoExposureSpeedDown，只有本机Camera临时借用。
    float OriginalSpeedDown=0.f; // 首次AutoExposureSpeedDown，EV/s；基线要求原值1。
    int32 Phase=0; // 0=原态，1=SpeedDown6临时候选，不写正式资产。
    int32 Step=0; // 固定状态：0正常StartRun、1等Hub、2室内预热、3出屋早图、4出屋晚图。
    double StartedAt=0.; // 单调真实秒，总90秒上限。
    double WarmupAt=0.; // 本阶段室内真实视点开始秒；预热8秒不冒称曝光完全收敛。
    double ExitAt=0.; // 本阶段碰撞Teleport到室外的真实秒，截图延迟原点。
    double NextSampleAt=0.; // GT10Hz采样门，不修改渲染采样率。
    uint64 TickCount=0; // 本夹具真实GT Tick数，用于截图请求后至少一帧门。
    uint64 CaptureTick=0; // 当前截图请求时Tick数。
    double CaptureAt=0.; // 当前实际FScreenshotRequest时间，写盘延迟从此计算。
    double CaptureTarget=0.; // 当前目标出屋延迟秒，报告另写实际值。
    FVector CaptureEye=FVector::ZeroVector; // 实际请求时PC眼位cm，写盘时的视点另记，不声称GPU图帧身份。
    FRotator CaptureRotation=FRotator::ZeroRotator; // 实际请求时PC控制相机角度deg。
    float CaptureFov=0.f; // 实际请求时PlayerCameraManager的FOV角度deg。
    bool bCapturePending=false; // 只取消本夹具拥有的当前截图请求。
    FString PendingCapture; // 当前唯一绝对PNG路径，不覆盖旧图。
    FString EvidenceDirectory; // Saved下GUID目录，非战役进度。
    FTransform HubFrame; // 实际Blockout/环境根一致的Hub世界框，cm。
    FVector OutsidePoint=FVector::ZeroVector; // 本室西门外固定Pawn局部cm，A/B同一站位。
    FVector OutsideLook=FVector::ZeroVector; // 本室室外固定LookAt局部cm，A/B相同。
    TWeakObjectPtr<ADemoCharacter> Player; // 当前被本机PC占有Pawn，不延长Avatar寿命。
    TWeakObjectPtr<ADemoPlayerController> Controller; // 当前World本机PC，任何身份变化即拒绝。
    TWeakObjectPtr<UCameraComponent> Camera; // 仅此相机两曝光字段可改；其他PP/Blendables保持原路由。
    TSharedPtr<FDemoHubExposureRenderState,ESPMode::ThreadSafe> RenderState; // Mutex保护的纯值桥；RT不访问UObject。
    TSharedPtr<FSceneViewExtensionBase,ESPMode::ThreadSafe> ViewExtension; // 本World公开扩展，停止后在GT释放持有引用。
    TSharedPtr<FJsonObject> Facts; // GT拥有报告，RT绝不写JSON。
    TArray<TSharedPtr<FJsonValue>> Samples; // <=1000项GT采样，每项可指向同一已完成RT序号。
    TArray<TSharedPtr<FJsonValue>> Captures; // 恰四张实际请求/写入记录，不含SceneCapture。
    TArray<FString> Checks; // <=1000条断言，失败证据保留。
};
