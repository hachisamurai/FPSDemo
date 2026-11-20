// 职责：Editor显式起步抖动诊断；使用真实移动输入和最终骨帧，采集主图/子层缓存，不修改正式配置或资产。
#include "Tests/DemoPlayerAnimationTest.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_SequencePlayer.h" // 只读实例的真实播放时钟，不从动作名称推断相位。
#include "AnimNodes/AnimNode_SequenceEvaluator.h"
#include "Animation/DemoLyraAnimInstance.h" // 从公开配置取得本次已链接层的类，不访问引擎私有数组。
#include "Camera/CameraActor.h"
#include "Characters/DemoCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Weapons/DemoWeaponBase.h"

namespace
{
    /** 在PostUpdateWork等待骨求值结束后读取实际播放器；不修改节点或在动画任务中读UObject。 */
    TArray<TSharedPtr<FJsonValue>> StartAuditPlayers(const UAnimInstance* Instance)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        TArray<TSharedPtr<FJsonValue>> Rows; // 当前帧播放器标量，不包含骨数组。
        if (!Instance) return Rows;
        for (TFieldIterator<FStructProperty> It(Instance->GetClass());It;++It)
        {
            const FStructProperty* Property=*It; // 类拥有的节点描述，读取周期限本调用。
            if (!Property->Struct->IsChildOf(FAnimNode_AssetPlayerBase::StaticStruct())) continue;
            const FAnimNode_AssetPlayerBase* Node=Property->ContainerPtrToValuePtr<FAnimNode_AssetPlayerBase>(Instance);
            const UAnimationAsset* Asset=Node->GetAnimAsset(); // 不保存或更改资产引用。
            if (!Asset) continue;
            TSharedPtr<FJsonObject> Row=MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("node"),Property->GetName());
            Row->SetStringField(TEXT("asset"),Asset->GetPathName());
            Row->SetNumberField(TEXT("time"),Node->GetAccumulatedTime());
            Row->SetNumberField(TEXT("weight"),Node->GetCachedBlendWeight());
            Rows.Add(MakeShared<FJsonValueObject>(Row));
        }
        return Rows;
    }
    /** 将当前实例的少量标量/向量缓存导出为文本；仅游戏线程已完成求值时调用，不保存UObject引用。 */
    TSharedPtr<FJsonObject> StartAuditProperties(const UAnimInstance* Instance)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        TSharedPtr<FJsonObject> Data=MakeShared<FJsonObject>(); // 本帧值对象，写JSON后释放。
        if (!Instance) return Data;
        Data->SetStringField(TEXT("class"),Instance->GetClass()->GetPathName());
        Data->SetArrayField(TEXT("players"),StartAuditPlayers(Instance));
        for (const TCHAR* Name : {TEXT("WorldRotation"),TEXT("RootYawOffset"),TEXT("YawDeltaSinceLastUpdate"),TEXT("YawDeltaSpeed"),TEXT("LocalVelocityDirectionAngle"),TEXT("LocalVelocityDirection"),TEXT("LocalVelocityDirectionNoOffset"),TEXT("CardinalDirectionFromAcceleration"),TEXT("StartDirection"),TEXT("HasAcceleration"),TEXT("HasVelocity"),TEXT("IsRunningIntoWall"),TEXT("DisplacementSinceLastUpdate"),TEXT("DisplacementSpeed"),TEXT("LocalAcceleration2D"),TEXT("LocalVelocity2D"),TEXT("AdditiveLeanAngle"),TEXT("UpperbodyDynamicAdditiveWeight"),TEXT("StrideWarpingStartAlpha"),TEXT("StrideWarpingCycleAlpha"),TEXT("HipFireUpperBodyOverrideWeight"),TEXT("ExplicitTime"),TEXT("DistanceToMatch"),TEXT("NewDesiredSequence")})
        {
            const FProperty* Property=Instance->GetClass()->FindPropertyByName(Name); // 反射对象由Class持有，不越过本次同步采样。
            if (Property)
            {
                FString Value; // 将有限的诊断属性转成可读值，不导出整对象或指针地址。
                Property->ExportText_InContainer(0,Value,Instance,Instance,const_cast<UAnimInstance*>(Instance),PPF_None);
                Data->SetStringField(Name,Value);
            }
        }
        return Data;
    }
}

void ADemoPlayerAnimationTest::RecordStartJitterState(TSharedPtr<FJsonObject> Frame)
{
    DEMO_LOG_TICK();
    UAnimInstance* Main=Player->GetMesh()->GetAnimInstance(); // 最终已求值主实例，不改变其任何字段。
    Frame->SetNumberField(TEXT("actor_yaw"),Player->GetActorRotation().Yaw);
    Frame->SetNumberField(TEXT("control_yaw"),Player->GetBaseAimRotation().Yaw);
    Frame->SetObjectField(TEXT("native_main"),StartAuditProperties(Main));
    Frame->SetStringField(TEXT("motion_isolation"),MotionIsolation);
    TArray<TSharedPtr<FJsonValue>> LayerSamples; // 子层只写本帧值，供排除重复选片/权重跳变。
    if (const UDemoLyraAnimInstance* Native=Cast<UDemoLyraAnimInstance>(Main))
    {
        Frame->SetBoolField(TEXT("preserve_locomotion_spine"),Native->bPreserveLocomotionSpine); // 记录实际实例策略，不能以启动参数冒充生效。
        const TSubclassOf<UAnimInstance>* Configured=Native->WeaponLayers.Find(Native->GetUpperWeaponId()); // 对应本帧武器身份，缺项沿正式DefaultLayer。
        const TSubclassOf<UAnimInstance> LayerClass=Configured?*Configured:Native->DefaultLayer;
        LayerSamples.Add(MakeShared<FJsonValueObject>(StartAuditProperties(Main->GetLinkedAnimLayerInstanceByClass(LayerClass))));
    }
    Frame->SetArrayField(TEXT("native_layers"),LayerSamples);
}

void ADemoPlayerAnimationTest::TickStartJitterAudit(float Now)
{
    DEMO_LOG_TICK();
    if (Stage<0)
    {
        if (!InitializeFixture()) return;
        Stage=0; StageStart=Now; StageName=TEXT("pistol_start_0"); NextSample=NextCapture=Now+.1f;
        Player->GetController()->SetControlRotation(FRotator(bShoulderShadowAudit?-20.f:0.f,0.f,0.f)); // 影子专项真实低头看前方地面，不改骨缓存。
        Player->GetCharacterMovement()->StopMovementImmediately(); Player->ConsumeMovementInputVector();
        Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics); // 初始化正式开关会移动Pawn；起步专项必须显式回到隔离平台。
    }
    if (!Player.IsValid() || !Weapon.IsValid()) { Check(false,TEXT("start jitter fixture lost pawn/weapon")); return; }
    const int32 Variant=Stage%3; // 每枪前行、斜行、静止44度头身差后前行，均先等待真实停稳。
    const float Elapsed=Now-StageStart; // 0..1秒停稳；隔离专项多走3秒，覆盖原夹具没有观察的稳定循环及接缝。
    const float ExtraWalk=MotionIsolation.IsEmpty()?0.f:3.f; // 只延长真实输入，不改变World/动画时钟。
    if (Elapsed>=1.f && Elapsed<2.1f+ExtraWalk)
    {
        const float Heading=Variant==1?45.f:Variant==2?44.f:0.f; // 世界水平输入方向，与控制器请求相匹配。
        Player->AddMovementInput(FRotator(0,Heading,0).Vector(),1.f);
    }
    const FVector Target=Player->GetActorLocation()+FVector(0,0,8); // 跟随真实Pawn观察完整躯干，摄像机不参与移动计算。
    const FVector View=Target+FVector(310,-330,115);
    Camera->SetActorLocationAndRotation(View,(Target-View).Rotation());
    CastChecked<APlayerController>(Player->GetController())->SetViewTarget(bFirstPersonCapture?static_cast<AActor*>(Player.Get()):static_cast<AActor*>(Camera.Get()));
    if (Now+.0001f>=NextSample) { RecordFrame(); NextSample=Now+1.f/60.f; }
    if (bCapture && FApp::CanEverRender() && Now>=NextCapture && Elapsed<3.3f+ExtraWalk && !FScreenshotRequest::IsScreenshotRequested())
    {
        // 截图在帧尾执行；末尾0.1秒不再请求，防止随后测试瞬移只更新身体、展示枪下一帧才刷新而产生伪分离图。
        const FString Path=GetReviewRoot()/TEXT("Frames")/FString::Printf(TEXT("%02d_%05d.png"),Stage,ScreenshotIndex++); // 只写本次诊断目录，图像与骨帧独立保存。
        FScreenshotRequest::RequestScreenshot(Path,false,false); NextCapture=Now+1.f/30.f;
    }
    if (Elapsed<3.4f+ExtraWalk) return;
    if (!MotionIsolation.IsEmpty() && !Check(StageSamples>=30 && MaximumFootTravel>10.f,
        TEXT("isolation must contain a moving articulated leg chain, not a static fallback pose"))) return;
    if (++Stage==12) { SaveAndExit(true); return; } // 成功只代表采集完成，不自动宣称画面正确。
    if (Stage%3==0 && !EquipWeapon(Stage/3)) return;
    Player->GetCharacterMovement()->StopMovementImmediately(); Player->ConsumeMovementInputVector();
    Player->SetActorLocation(PlatformSpawn,false,nullptr,ETeleportType::TeleportPhysics); // 每案同一无障碍起点，避免连续12案跑入白模碰撞影响方向样本。
    Player->SetActorRotation(FRotator::ZeroRotator); // 44度用例必须从相同头身夹角开始，不继承前案ActorYaw。
    Player->GetController()->SetControlRotation(FRotator(bShoulderShadowAudit?-20.f:0.f,Stage%3==2?44.f:0.f,0));
    StageName=FString::Printf(TEXT("%s_start_%d"),*Weapon->GetWeaponId().ToString(),Stage%3);
    StageStart=Now; NextSample=NextCapture=Now+.05f;
    StageSamples=0; MaximumFootTravel=0.f; // 每案独立验证，不能借上一把枪的移动幅度蒙混静态Pose。
    UE_LOG(LogFPSDemo,Log,TEXT("START_JITTER_STAGE name=%s"),*StageName);
}
