// 职责：将本人受击组件已经限额/过期/舒适度缩放的Roll叠加到最终POV。使用：CameraManager每帧回调，无独立震动状态。
// 依赖：当前PC/Avatar、ASC生命和只读反馈视图；观战或视角切换立刻旁路，不修改玩法镜头输入。
#include "Combat/Feedback/Player/DemoDamageCameraModifier.h"
#include "Weapons/DemoWeaponBase.h"
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackComponent.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Debug/DemoLog.h"

UDemoDamageCameraModifier::UDemoDamageCameraModifier()
{
    DEMO_LOG_CALL();
    Priority=220; // 接近最终POV再加装饰Roll，不影响前面的瞄准/武器视角计算。
    bExclusive=false; // 不阻止现有镜头Modifier；受击组件只撤销自身实例。
}

bool UDemoDamageCameraModifier::ModifyCamera(float DeltaTime,FMinimalViewInfo& InOutPOV)
{
    DEMO_LOG_TICK();
    const ADemoPlayerController* PC=CameraOwner?Cast<ADemoPlayerController>(CameraOwner->PCOwner.Get()):nullptr; // CameraManager拥有，当前回调借用。
    const ADemoCharacter* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 观战仍可能保留旧Pawn，必须同时复核实际ViewTarget。
    const UDemoAttributeSet* Attributes=Pawn?Pawn->GetDemoAttributes():nullptr; // PS ASC是生命真值，Modifier不维护副本。
    if(!PC||!PC->IsLocalController()||PC->HasBlockingOverlay()||PC->IsUpgradeMenuOpen()||PC->IsNextLevelConfirmationOpen()||!Pawn||PC->GetViewTarget()!=Pawn||Pawn->IsCoopEliminated()||!Attributes||Attributes->GetHealth()<=0.f)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("DAMAGE camera bypass inactive own living view/menu")); return false; }
    const UDemoWeaponComponent* Equipment=Pawn->GetWeaponComponent(); // 当前Avatar装备，只读即时Scope变化，避免等待组件20ms刷新造成一帧镜内Roll。
    if(Equipment&&Equipment->GetScopeLevel()>0&&Equipment->GetActiveWeapon()&&Equipment->GetActiveWeapon()->GetWeaponId()==TEXT("sniper")) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("DAMAGE camera bypass sniper scoped view")); return false; }
    const UDemoPlayerDamageFeedbackComponent* Feedback=UDemoPlayerDamageFeedbackComponent::Find(PC); // 本人PC默认子对象，不查Player0或其他玩家。
    if(!Feedback) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("DAMAGE camera bypass missing feedback component")); return false; }
    const FDemoPlayerDamageView& View=Feedback->GetView(); // 组件已经处理菜单、狙击、代次及过期；不另开计时器。
    if(!View.bVisible||!FMath::IsFinite(View.RollDegrees)) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("DAMAGE camera bypass hidden/invalid envelope")); return false; }
    InOutPOV.Rotation.Roll+=FMath::Clamp(View.RollDegrees,-.4f,.4f); // 只改最终Roll，不调用会执行任意蓝图视角修改的基类实现。
    return false;
}
