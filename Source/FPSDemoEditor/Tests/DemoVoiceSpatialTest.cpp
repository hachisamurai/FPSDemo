// 职责：验证语音真实衰减配置、音频组件空间化和换Pawn定位。使用：Automation RunTests FPSDemo.Voice.Spatial；依赖隔离World/UE衰减公式，不采集麦克风、不联网或写玩家存档。
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Player/Voice/DemoVoiceComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "Debug/DemoLog.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoVoiceSpatialTest, "FPSDemo.Voice.Spatial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters为未使用的自动化参数；游戏线程检查实际引擎衰减和源生命周期，不把静态验证视为多人试听。 */
bool FDemoVoiceSpatialTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FDemoVoiceSpatialTest::RunTest"));
    const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
        .CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false); // 隔离World仅供定位，不建立游戏GI、物理或真实音频播放。
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init); // 本测试拥有，任何提前退出都必须销毁。
    if (!TestNotNull(TEXT("Isolated voice test world"), World)) return false;
    // 捕获当前栈的World引用，仅在游戏线程离开本函数时清理；World在此之前由Root保活，无异步/跨线程捕获。
    ON_SCOPE_EXIT { UE_LOG(LogFPSDemo, Log, TEXT("[CALL] VoiceSpatialTest cleanup")); World->DestroyWorld(false); };
    APlayerState* Speaker = World->SpawnActor<APlayerState>(); // 通用PS夹具，不创建真实玩家档案或调用项目战役启动。
    if (!TestNotNull(TEXT("Speaker identity"), Speaker)) return false;
    UDemoVoiceComponent* Voice = NewObject<UDemoVoiceComponent>(Speaker); // PS拥有的实际生产语音组件，仅接收/定位，不调用StartTalking。
    Speaker->AddInstanceComponent(Voice); Voice->RegisterComponent();
    Voice->VoiceInnerRadius = 300.f; Voice->VoiceMaxDistance = 3000.f; Voice->PlaybackVolume = 1.f; // 独立实例基线，测试不修改配置CDO或磁盘。
    const FSoundAttenuationSettings Settings = Voice->MakePlaybackAttenuation(); // 使用生产配置创建真实UE衰减值，不复写衰减公式。
    TestTrue(TEXT("Distance attenuation and spatialization enabled"), Settings.bAttenuate && Settings.bSpatialize);
    TestEqual(TEXT("Full volume at source"), Settings.Evaluate(FTransform::Identity, FVector::ZeroVector), 1.f);
    TestEqual(TEXT("Full volume at three metres"), Settings.Evaluate(FTransform::Identity, FVector(300,0,0)), 1.f);
    TestTrue(TEXT("Half amplitude midway through falloff"), FMath::IsNearlyEqual(Settings.Evaluate(FTransform::Identity, FVector(1650,0,0)), .5f));
    TestEqual(TEXT("Silent at thirty metres"), Settings.Evaluate(FTransform::Identity, FVector(3000,0,0)), 0.f);
    TestEqual(TEXT("Silent beyond maximum distance"), Settings.Evaluate(FTransform::Identity, FVector(5000,0,0)), 0.f);
    TestTrue(TEXT("Inner radius keeps directional panning"), Settings.NonSpatializedRadiusStart==0.f && Settings.NonSpatializedRadiusEnd==0.f);
    Voice->VoiceInnerRadius = 100.f; Voice->VoiceMaxDistance = 1000.f; // 非默认合法配置，检查字段代表总距离而不是额外衰减长度。
    const FSoundAttenuationSettings Custom = Voice->MakePlaybackAttenuation(); // 借用生产校验结果的值副本。
    TestEqual(TEXT("Configured maximum is total radius"), Custom.Evaluate(FTransform::Identity, FVector(1000,0,0)), 0.f);
    TestTrue(TEXT("Configured falloff midpoint"), FMath::IsNearlyEqual(Custom.Evaluate(FTransform::Identity, FVector(550,0,0)), .5f));
    Voice->VoiceInnerRadius = 5000.f; Voice->VoiceMaxDistance = -1.f; // 故意越界；预期Warning，不能生成负区间或NaN。
    const FSoundAttenuationSettings Clamped = Voice->MakePlaybackAttenuation(); // 错误INI规范化结果只存在测试实例。
    TestTrue(TEXT("Invalid ranges stay finite with positive falloff"), Clamped.AttenuationShapeExtents.X==0.f && Clamped.FalloffDistance==1.f);
    Voice->VoiceInnerRadius = std::numeric_limits<float>::quiet_NaN(); Voice->VoiceMaxDistance = std::numeric_limits<float>::infinity(); // 非有限配置必须回到有限默认。
    const FSoundAttenuationSettings Finite = Voice->MakePlaybackAttenuation(); // 测试NaN/Inf不进入音频线程。
    TestTrue(TEXT("Nonfinite configuration falls back to three/thirty metres"), Finite.AttenuationShapeExtents.X==300.f && Finite.FalloffDistance==2700.f);
    Voice->VoiceInnerRadius = 300.f; Voice->VoiceMaxDistance = 3000.f; // 恢复后用生产PreparePlayback检查真实AudioComponent属性。
    if (!TestTrue(TEXT("Playback prepares without microphone"), Voice->PreparePlayback())) return false;
    TestTrue(TEXT("Actual audio source enables attenuation and panning"), Voice->Playback && Voice->Playback->bAllowSpatialization && Voice->Playback->bOverrideAttenuation && Voice->Playback->AttenuationOverrides.bSpatialize);
    TestTrue(TEXT("Silent distance still consumes live procedural audio"), Voice->VoiceWave && Voice->VoiceWave->VirtualizationMode==EVirtualizationMode::PlayWhenSilent);
    TestTrue(TEXT("Preparing receive path never opens capture"), !Voice->bCapturing && !Voice->Capture.IsValid());
    TestFalse(TEXT("No pawn cannot emit from world origin"), Voice->UpdatePlaybackLocation());
    APawn* First = World->SpawnActor<APawn>(); // 首个身体；手工添加SceneRoot以支持真实SetActorLocation，无角色资产依赖。
    APawn* Replacement = World->SpawnActor<APawn>(); // 重生替代身体，确保组件不会缓存旧Pawn。
    if (!TestNotNull(TEXT("Initial pawn"), First) || !TestNotNull(TEXT("Replacement pawn"), Replacement)) return false;
    for (APawn* Pawn : {First, Replacement}) // 两只本测试Actor，各自拥有独立Root，不触碰玩家World。
    {
        USceneComponent* Root = NewObject<USceneComponent>(Pawn); // 生命周期归对应Pawn，World清理时一并销毁。
        Pawn->AddInstanceComponent(Root); Pawn->SetRootComponent(Root); Root->RegisterComponent(); Pawn->BaseEyeHeight = 64.f;
    }
    First->SetActorLocation(FVector(100,200,300));
    First->SetPlayerState(Speaker); // UE公开入口同步Pawn↔PS双向关系，远端没有PC也能解析声源。
    TestTrue(TEXT("Voice uses speaker eye position without controller"), Voice->UpdatePlaybackLocation() && Voice->Playback->GetComponentLocation().Equals(FVector(100,200,364)));
    First->SetActorLocation(FVector(800,-300,100));
    TestTrue(TEXT("Moving pawn moves spatial source"), Voice->UpdatePlaybackLocation() && Voice->Playback->GetComponentLocation().Equals(FVector(800,-300,164)));
    Replacement->SetActorLocation(FVector(-700,900,0));
    First->SetPlayerState(nullptr); Replacement->SetPlayerState(Speaker); // 模拟重生解绑旧身体，再绑定新身体；不访问引擎私有桥接构造器。
    TestTrue(TEXT("Respawn follows replacement pawn"), Voice->UpdatePlaybackLocation() && Voice->Playback->GetComponentLocation().Equals(FVector(-700,900,64)));
    First->SetActorLocation(FVector(9000,9000,9000));
    TestTrue(TEXT("Old body cannot move new voice source"), Voice->UpdatePlaybackLocation() && Voice->Playback->GetComponentLocation().Equals(FVector(-700,900,64)));
    Replacement->SetPlayerState(nullptr); // 模拟复制/销毁空档，声源必须不可用。
    TestFalse(TEXT("Unpossessed source is unavailable"), Voice->UpdatePlaybackLocation());
    TestFalse(TEXT("Missing pawn clears location validity"), Voice->bHasPlaybackLocation);
    Replacement->SetPlayerState(Speaker); // 当前身体重新可用，仅恢复当前位置，不回放历史PCM。
    TestTrue(TEXT("Valid body restores source without reconnecting"), Voice->UpdatePlaybackLocation());
    Voice->ResetPlayback(); // 收包停止路径清嘴型/FIFO；本测试从未调用音频Play或设备采集。
    TestEqual(TEXT("Playback reset closes speech indication"), Voice->GetMouthOpenAmount(), 0.f);
    if (!HasAnyErrors()) UE_LOG(LogFPSDemo, Display, TEXT("VOICE_SPATIAL_TEST_SUCCESS"));
    return !HasAnyErrors();
}
#endif
