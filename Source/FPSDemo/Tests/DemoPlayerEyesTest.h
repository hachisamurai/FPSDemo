#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoPlayerEyesTest.generated.h"

class ADemoCharacter;
class ACameraActor;
class ADemoPlayerController;
class FJsonObject;
class FJsonValue;

/** 仅-DemoPlayerEyesCapture显式Editor入口：真实控制输入与独立正面/斜侧画面的八阶段验收。 */
UCLASS(Transient,NotBlueprintable)
class FPSDEMO_API ADemoPlayerEyesTest : public AActor
{
    GENERATED_BODY()
public:
    /** PostUpdate采集最终眼球/头矩阵；普通游戏不会创建本测试Actor。 */
    ADemoPlayerEyesTest();
    /** DeltaSeconds为World秒；输入交给真正Controller，不直接写眼球或RemoteViewPitch。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** 从正式安全区启动当前Pawn，设置独立只读观察相机，不改生产关卡或外观资产。 */
    bool InitializeFixture();
    /** 为当前正面或斜侧阶段提交真实ControlRotation，重置同样的采样/平滑稳定窗口。 */
    void BeginStage();
    /** 根据实际组件记录JSON；Elapsed为阶段游戏秒，超过.6秒必须完成限幅/平滑断言。 */
    bool RecordFrame(float Elapsed);
    /** bSuccess/Reason为最终证据；写专属JSON，等待截图完成后结束本隔离进程。 */
    void Finish(bool bSuccess,const FString& Reason);
    /** Value为本帧真实Transform；输出列向量4x4厘米矩阵及可读位置，仅读值。 */
    static TSharedRef<FJsonObject> TransformJson(const FTransform& Value);
    // 两个World拥有对象的弱引用，不延长旅行后的Pawn/PC生命周期。
    TWeakObjectPtr<ADemoCharacter> Player;
    TWeakObjectPtr<ADemoPlayerController> Controller;
    // World拥有的观察相机，避免ViewTarget=Pawn让OwnerNoSee隐藏被检验眼球。
    UPROPERTY() TObjectPtr<ACameraActor> Camera;
    // 六个原正面阶段加两个±35度斜侧摄影阶段；不改变Pawn的身体Yaw跟随策略。
    static constexpr int32 TestStageCount=8;
    // 验收专用水平FOV，度；独立声明避免MSVC在导出类的逗号静态声明上重复应用DLL接口。
    static constexpr float InspectionFov=36.f;
    // 验收水平视距，厘米；130cm配36度保留完整头脸，不写生产相机配置。
    static constexpr float InspectionDistance=130.f;
    // -1等待初始化，0..5中/左/右/上/下/回中，6/7斜侧上/下视，100/101为成功/失败退出。
    int32 Stage=-1;
    // 单阶段与采样时钟，单位World秒；截图是实际采样图而非60fps视频承诺。
    float StageStart=0.f,NextSample=0.f,NextCapture=0.f;
    // 串行截图编号与本阶段平滑收敛后的真实证据数。
    int32 ScreenshotIndex=0,StableSamples=0;
    // 控制朝向输入，度；与CameraActor自身朝向分开记录，测试二者不能混用。
    FRotator DesiredControl=FRotator::ZeroRotator;
    // 观察镜头围绕Pawn正前方的水平角，度；0/±35仅作用于测试Camera，不传给Eyes求值。
    float CameraOrbitYaw=0.f;
    // 独立JSON值缓存，八阶段约400帧；不捕获任何异步UObject引用。
    TArray<TSharedPtr<FJsonValue>> Frames;
    // 幂等结束门闩；进程退出稍后执行，让最后真实截图落盘。
    bool bFinished=false;
    double ExitAt=0.0;
};
