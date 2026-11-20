// 职责：按身体同一动作相位采样TP枪机/弹匣，独立TP资源优先，旧配置保留兼容；不驱动玩法时钟。
#include "Animation/DemoPlayerWeaponAnimInstance.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoFPAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"

namespace DemoPlayerMechanical
{
    /** Set是当前枪CDO借用值，Action是身体已经选定的表现状态；返回同枪的原有机械资源。 */
    UAnimSequence* Select(const FDemoWeaponAnimationSet& Set, FName Action)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        if (Action==TEXT("EmptyReload")) return Set.EmptyReload.WeaponSequence;
        if (Action==TEXT("Bolt")) return Set.BoltCycle.WeaponSequence;
        return Set.TacticalReload.WeaponSequence; // Hold/Fire保持闭锁末帧，Reload读取对应phase；普通短后坐只移动整枪。
    }
    /** Poses借用本Pawn只读外观配置，Action由身体决定；新资源缺动作回退同枪Hold，禁止串用FP弹匣轨迹。 */
    UAnimSequence* SelectIndependent(const FDemoPlayerWeaponPoses& Poses, FName Action)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        if (Action==TEXT("Reload")) return Poses.MechanicalReload?Poses.MechanicalReload.Get():Poses.MechanicalHold.Get();
        if (Action==TEXT("EmptyReload")) return Poses.MechanicalEmptyReload?Poses.MechanicalEmptyReload.Get():Poses.MechanicalHold.Get();
        if (Action==TEXT("Bolt")) return Poses.MechanicalBolt?Poses.MechanicalBolt.Get():Poses.MechanicalHold.Get();
        if (Action==TEXT("Fire")) return Poses.MechanicalFire?Poses.MechanicalFire.Get():Poses.MechanicalHold.Get();
        return Poses.MechanicalHold;
    }
}
void UDemoPlayerWeaponAnimInstance::NativeInitializeAnimation()
{
    DEMO_LOG_CALL(); Super::NativeInitializeAnimation();
    Character=Cast<ADemoCharacter>(GetOwningActor());
    CurrentSequence=nullptr; PreviousSequence=nullptr; CurrentTime=0; PreviousTime=0; BlendAlpha=1;
}
void UDemoPlayerWeaponAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::NativeUpdateAnimation(DeltaSeconds);
    if (!Character.IsValid()) return;
    const UDemoPlayerAnimInstance* Body=Cast<UDemoPlayerAnimInstance>(Character->GetMesh()->GetAnimInstance()); // 身体先完成本帧采样，机械不能创建另一套动作时钟。
    const ADemoWeaponBase* Weapon=Character->GetWeaponComponent()?Character->GetWeaponComponent()->GetActiveWeapon():nullptr; // 只借用真实当前装备。
    const UDemoWeaponLayerAnimInstance* Layer=Weapon&&Weapon->Config.WeaponAnimLayerClass?Weapon->Config.WeaponAnimLayerClass.GetDefaultObject():nullptr; // 现有配置CDO只读。
    if (!Body||!Layer) { CurrentSequence=nullptr; PreviousSequence=nullptr; return; }
    const UDemoPlayerAppearance* Appearance=Character->GetPlayerPresentation()?Character->GetPlayerPresentation()->GetAppearance():nullptr; // Pawn保活，不异步加载。
    const FDemoPlayerWeaponPoses* Poses=Appearance?Appearance->WeaponPoses.Find(Weapon->GetWeaponId()):nullptr; // 稳定武器ID，不比较显示名。
    const bool bIndependent=Poses && Poses->MechanicalHold; // 明确启用完整独立TP组；旧资产默认行为不变。
    CurrentSequence=bIndependent?DemoPlayerMechanical::SelectIndependent(*Poses,Body->UpperBodyAction):DemoPlayerMechanical::Select(Layer->AnimSet,Body->UpperBodyAction);
    PreviousSequence=bIndependent?DemoPlayerMechanical::SelectIndependent(*Poses,Body->PreviousUpperBodyAction):DemoPlayerMechanical::Select(Layer->AnimSet,Body->PreviousUpperBodyAction);
    const float MechanicalPhase=Body->bUseAdditiveGunRecoil && Body->UpperBodyAction==TEXT("Fire")?Body->GunRecoilPhase:Body->ActionProgress; // 读取Body真实后坐Montage相位；Hold Evaluator固定0不能作为机械时钟。
    CurrentTime=CurrentSequence?CurrentSequence->GetPlayLength()*(Body->UpperBodyAction==TEXT("Hold")? (bIndependent?0.f:1.f):(!bIndependent && Body->UpperBodyAction==TEXT("Fire"))?1.f:MechanicalPhase):0.f;
    PreviousTime=PreviousSequence?PreviousSequence->GetPlayLength()*(Body->PreviousUpperBodyAction==TEXT("Hold")? (bIndependent?0.f:1.f):(!bIndependent && Body->PreviousUpperBodyAction==TEXT("Fire"))?1.f:Body->PreviousActionProgress):0.f;
    BlendAlpha=Body->UpperBodyBlendAlpha; // 身体/弹匣/枪机同步取消，结束不会一边先瞬移。
}
