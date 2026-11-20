// 职责：45度自由观察/完整对齐/反向/跨180专项；依赖真实StartRun、FaceRotation和共享枪口，只在Editor启动分发中启用。
#include "Tests/DemoViewTurnTest.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Game/FPSDemoGameMode.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

ADemoViewTurnTest::ADemoViewTurnTest()
{
    DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickGroup=TG_PostUpdateWork;
}
void ADemoViewTurnTest::Fail(const TCHAR* Reason)
{
    DEMO_LOG_CALL(); UE_LOG(LogFPSDemo,Error,TEXT("VIEW_TURN_TEST_FAIL stage=%d reason=%s"),Stage,Reason);
    SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,1);
}
void ADemoViewTurnTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
    const double Now=GetWorld()->GetTimeSeconds(); // 当前临时World时钟，暂停不推进。
    if (Now>30.) { Fail(TEXT("timeout")); return; }
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本端唯一输入拥有者。
    ADemoCharacter* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 不跨旅行持有旧角色。
    if (!PC || !Pawn || Now<1.) return;
    const float Views[]={0,20,45,60,25,-30,-60,-179,179,-160}; // 输入覆盖阈值内/边界/越界/反向/换界，独立于生产算法。
    const float Bodies[]={0,0,0,60,60,-30,-30,-179,-179,-179}; // 新完整对齐规则手算终点，枪口静止与身体同向。
    if (Stage<0)
    {
        AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 只通过真实玩法入口进入安全区。
        if (!Mode || !Mode->StartRun()) { Fail(TEXT("StartRun")); return; }
        PC->OnRunReady(); Pawn->GetWeaponComponent()->EquipSlot(2); Pawn->SetActorRotation(FRotator::ZeroRotator);
        Stage=0; StageStart=Now; PC->SetControlRotation(FRotator::ZeroRotator); return;
    }
    const float Body=Pawn->GetActorRotation().Yaw; // 实际胶囊朝向，不读取生产转身门闩推导预期。
    const float DesiredBody=Bodies[Stage]; // 当前手算目标，范围含跨180。
    const bool bShouldTurn=FMath::Abs(FMath::FindDeltaAngleDegrees(InitialBody,DesiredBody))>1.f; // 有终点位移才要求观察过渡。
    if (bShouldTurn && FMath::Abs(FMath::FindDeltaAngleDegrees(InitialBody,Body))>1.f && FMath::Abs(FMath::FindDeltaAngleDegrees(Body,DesiredBody))>1.f) bSawIntermediate=true;
    if (Now-StageStart>.65)
    {
        const ADemoWeaponBase* Weapon=Pawn->GetWeaponComponent()->GetActiveWeapon(); // 当前真实手枪及其Socket配置。
        const USkeletalMeshComponent* Gun=Pawn->GetPlayerPresentation()->GetThirdPersonWeaponMesh(); // 两视角共用的最终可见枪。
        if (!Weapon || !Gun) { Fail(TEXT("missing shared weapon")); return; }
        const float Muzzle=Gun->GetSocketRotation(Weapon->Config.MuzzleSocket).Yaw; // 最终骨/Socket，不是控制器射线。
        const float BodyError=FMath::Abs(FMath::FindDeltaAngleDegrees(DesiredBody,Body)); // 度数，稳定门槛保留小姿态余量。
        const float GunError=FMath::Abs(FMath::FindDeltaAngleDegrees(DesiredBody,Muzzle));
        if (BodyError>.2f || GunError>2.f)
        { UE_LOG(LogFPSDemo,Error,TEXT("VIEW_TURN_VALUES body=%.3f muzzle=%.3f expected=%.3f"),Body,Muzzle,DesiredBody); Fail(TEXT("body/muzzle failed to align")); return; }
        ++StableSamples;
    }
    if (Now-StageStart<.95) return;
    if (StableSamples<5 || (bShouldTurn && !bSawIntermediate)) { Fail(TEXT("missing stable/intermediate samples")); return; }
    UE_LOG(LogFPSDemo,Display,TEXT("VIEW_TURN_STAGE_PASS stage=%d view=%.1f body=%.1f samples=%d smooth=%d"),Stage,Views[Stage],Body,StableSamples,bSawIntermediate);
    if (++Stage>=UE_ARRAY_COUNT(Views))
    { UE_LOG(LogFPSDemo,Display,TEXT("VIEW_TURN_TEST_SUCCESS stages=10")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); return; }
    InitialBody=Body; StableSamples=0; bSawIntermediate=false; StageStart=Now;
    PC->SetControlRotation(FRotator(0,Views[Stage],0)); // 输入由下一生产Tick处理，测试不直接转Actor或枪。
}
