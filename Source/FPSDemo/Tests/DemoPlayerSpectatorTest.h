#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoPlayerSpectatorTest.generated.h"

class ADemoPlayerController;
class ADemoCharacter;
class ADemoCoopSpectatorCamera;

/** 显式Editor进程的观战集成回归；验证正式RPC本地实现、ViewTarget生命周期和真实相机碰撞，不宣称网络已验证。 */
UCLASS(Transient, NotBlueprintable)
class FPSDEMO_API ADemoPlayerSpectatorTest : public AActor
{
    GENERATED_BODY()
public:
    /** 20Hz有界状态机，仅显式-DemoPlayerSpectatorTest入口生成。 */
    ADemoPlayerSpectatorTest();
    /** DeltaSeconds是World秒，依次检查跟随、遮挡、切换、丢失和复活释放。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** Condition为真实World断言，Message为日志稳定标签；失败只退出本测试进程。 */
    bool Check(bool Condition, const TCHAR* Message);
    /** Next为下个固定阶段，Delay为等待正式Tick/销毁处理的World秒，不手调相机Tick。 */
    void Advance(int32 Next, float Delay = .2f);
    // 本测试借用的真实本地PC及Pawn，不改变它们的经济/存档。
    TWeakObjectPtr<ADemoPlayerController> Controller;
    TWeakObjectPtr<ADemoCharacter> LocalPawn;
    // 测试World专用两具目标Pawn；无控制器/输入，仅验证本地可见性与镜头生命周期。
    TWeakObjectPtr<ADemoCharacter> FirstTarget;
    TWeakObjectPtr<ADemoCharacter> SecondTarget;
    // 由正式RPC创建的相机，弱引用用于验证复活后确实Destroy而非只隐藏。
    TWeakObjectPtr<ADemoCoopSpectatorCamera> Camera;
    // 临时墙Actor归World，验证相机Sweep使用真实物理碰撞。
    TWeakObjectPtr<AActor> Wall;
    // 第一帧正式镜头位置，作为移动跟随的独立位移基准，单位世界厘米。
    FVector InitialCameraLocation = FVector::ZeroVector;
    // 0准备、1跟随、2墙体、3切换、4销毁目标、5复活、6完成；超时直接失败。
    int32 Step = 0;
    // 下一阶段允许执行的World秒，初始化等待正常角色BeginPlay。
    float NextTime = 1.f;
    // 保存本人原始服务器色号，观战不得改本人或目标的外观状态。
    uint8 OriginalOutfit = 255;
    // 首次失败后停止推进，防止随后产生误导的SUCCESS日志。
    bool bFailed = false;
};
