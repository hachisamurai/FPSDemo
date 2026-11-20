// 职责：本地异步加载本关敌人/装备资源，等落脚点碰撞可用后向服务器确认。仅表现与加载权责，真实伤害/传送仍由Flow执行。
#include "Player/Coop/Preparation/DemoLocalEncounterPreload.h"
#include "Player/DemoPlayerController.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "World/Rooms/DemoRoomStreamingSubsystem.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Debug/DemoLog.h"

UDemoLocalEncounterPreload::UDemoLocalEncounterPreload() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }

void UDemoLocalEncounterPreload::BeginPreload(const FDemoEncounterPreloadRequest& Request)
{
    DEMO_LOG_CALL();
    ADemoPlayerController* Player = Cast<ADemoPlayerController>(GetOwner()); // PC拥有者，远端服务器副本不能加载客户端资源。
    if (!Player || !Player->IsLocalController() || !GetWorld() || Request.Generation <= 0 || Request.Stage <= 0 || Request.Resources.Num() > 64 || Request.LandingPoint.ContainsNaN())
    { UE_LOG(LogFPSDemo, Warning, TEXT("PRELOAD_LOCAL rejected owner/request limits")); return; }
    if (ActiveRequest.Generation == Request.Generation) return; // 可靠重复不能重新加载或延长截止时间。
    Stop(); ActiveRequest = Request; bReported = false; bReadyReported = false; bCommitted = false; bAssetsLoaded = Request.Resources.IsEmpty(); Deadline = FPlatformTime::Seconds() + 30.;
    if (!Request.Room.RoomId.IsNone() && GetWorld()->GetNetMode() == NM_Client)
    {
        FString Error; // 服务器下发身份也经World本地路径/预算检查，不允许任意文件加载。
        if (Request.Room.Generation != Request.Generation || !GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>()->BeginPrepare(Request.Room, Error))
        { Report(false, TEXT("RoomFailed")); return; }
    }
    for (const FSoftObjectPath& Resource : Request.Resources) // 服务器请求也做有限白名单检查，不按网络字符串任意加载系统/插件资源。
        if (!Resource.IsValid() || !Resource.GetLongPackageName().StartsWith(TEXT("/Game/")) || Resource.ToString().Len() > 256 || !Resource.GetSubPathString().IsEmpty())
        { Report(false, TEXT("InvalidResources")); return; }
    bAssetsLoaded = true;
    for (const FSoftObjectPath& Resource : Request.Resources) if (!Resource.ResolveObject()) { bAssetsLoaded = false; break; } // 先检查现成资源；纯单人/已预热时无需为了握手强加一帧延迟。
    if (bAssetsLoaded) for (const FSoftObjectPath& Resource : Request.Resources) RetainedResources.Add(Resource.ResolveObject()); // 快速路径也由UPROPERTY保活，不能只保存软路径并依赖恰好没有GC。
    if (!bAssetsLoaded)
    {
        LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Request.Resources,
            FStreamableDelegate::CreateUObject(this, &UDemoLocalEncounterPreload::HandleAssetsLoaded, Request.Generation)); // 弱UObject、游戏线程完成；绑定不可变代数拒绝旧回调，无裸捕获。
        if (!LoadHandle.IsValid()) { Report(false, TEXT("MissingResources")); return; }
    }
    if (bAssetsLoaded && !PollReadiness(0.f)) return; // 当前本地地面也已就绪时立即ACK；服务器只记状态，不在该RPC栈重入Flow。
    PollTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UDemoLocalEncounterPreload::PollReadiness), .1f);
    UE_LOG(LogFPSDemo, Log, TEXT("PRELOAD_LOCAL begin generation=%d stage=%d resources=%d"), Request.Generation, Request.Stage, Request.Resources.Num());
}

void UDemoLocalEncounterPreload::HandleAssetsLoaded(int32 Generation)
{
    DEMO_LOG_CALL();
    if (Generation != ActiveRequest.Generation || bReported) { UE_LOG(LogFPSDemo, Log, TEXT("PRELOAD_LOCAL stale completion ignored")); return; }
    for (const FSoftObjectPath& Resource : ActiveRequest.Resources)
        if (!Resource.ResolveObject()) { Report(false, TEXT("MissingResources")); return; }
    for (const FSoftObjectPath& Resource : ActiveRequest.Resources) RetainedResources.Add(Resource.ResolveObject()); // 游戏线程同步解析并保活，下一关/取消时统一释放。
    bAssetsLoaded = true;
}

bool UDemoLocalEncounterPreload::PollReadiness(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    if (bReported || !GetWorld() || GetWorld()->bIsTearingDown) { PollTicker.Reset(); return false; }
    if (FPlatformTime::Seconds() >= Deadline) { Report(false, bAssetsLoaded ? TEXT("LandingTimeout") : TEXT("ResourceTimeout")); PollTicker.Reset(); return false; }
    const ADemoPlayerController* Player = Cast<ADemoPlayerController>(GetOwner()); // 依赖可能晚到，暂停不影响真实时间超时。
    if (!bAssetsLoaded || !Player || !Player->GetPawn()) return true;
    if (!ActiveRequest.Room.RoomId.IsNone())
    {
        FDemoAreaSnapshot RoomArea; FString Error; // 客户端需看到本代房间锚点复制与关键Actor，不能只见地面就ACK。
        const EDemoRoomLoadStatus Status = GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>()->PollPrepared(RoomArea, Error);
        if (Status == EDemoRoomLoadStatus::Failed) { Report(false, TEXT("RoomFailed")); PollTicker.Reset(); return false; }
        if (Status != EDemoRoomLoadStatus::Ready) return true;
    }
    FCollisionObjectQueryParams Objects; // 只确认地形/动态回退平台，不把玩家胶囊或弹体当落脚地面。
    Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DemoPreloadLanding), false, Player->GetPawn()); // 原位置Pawn不影响入场点探测。
    FHitResult Floor; // 真实本地碰撞，正常向上且地面位于胶囊下方才确认；不上传Hit或客户端位置。
    const FVector Point = ActiveRequest.LandingPoint; // 已由服务器加好个人120cm间距的入场点。
    if (GetWorld()->LineTraceSingleByObjectType(Floor, Point, Point - FVector(0, 0, 300.f), Objects, Query) && Floor.ImpactNormal.Z >= .7f)
    {
        const ACharacter* Character = Cast<ACharacter>(Player->GetPawn()); // 使用本人的真实胶囊体积，空Pawn不能凭默认尺寸通过。
        const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
        if (!Capsule) return true;
        const FCollisionShape Shape = FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()); // 真实cm尺寸含当前缩放。
        if (GetWorld()->OverlapBlockingTestByChannel(Point, FQuat::Identity, ECC_Pawn, Shape, Query)) return true;
        Report(true, NAME_None); PollTicker.Reset(); return false;
    }
    return true;
}

void UDemoLocalEncounterPreload::Report(bool bReady, FName Code)
{
    DEMO_LOG_CALL();
    if (bReported) return;
    bReported = true;
    bReadyReported = bReady;
    if (ADemoPlayerController* Player = Cast<ADemoPlayerController>(GetOwner())) Player->ServerReportEncounterPreload(ActiveRequest.Generation, bReady, Code);
    UE_LOG(LogFPSDemo, Log, TEXT("PRELOAD_LOCAL reported generation=%d ready=%d code=%s"), ActiveRequest.Generation, bReady, *Code.ToString());
}

void UDemoLocalEncounterPreload::CancelPreload(int32 Generation)
{
    DEMO_LOG_CALL();
    if (ActiveRequest.Generation == Generation) { Stop(); UE_LOG(LogFPSDemo, Log, TEXT("PRELOAD_LOCAL cancelled generation=%d"), Generation); }
}

void UDemoLocalEncounterPreload::Stop()
{
    DEMO_LOG_CALL();
    if (PollTicker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(PollTicker); PollTicker.Reset(); }
    if (LoadHandle.IsValid()) { LoadHandle->CancelHandle(); LoadHandle.Reset(); }
    if (!bCommitted && !ActiveRequest.Room.RoomId.IsNone() && GetWorld() && GetWorld()->GetNetMode() == NM_Client)
        if (UDemoRoomStreamingSubsystem* Rooms = GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>()) Rooms->CancelPrepared(); // Listen的服务器候选由GM独占，PC不得取消共享World的新准备。
    RetainedResources.Reset();
    bAssetsLoaded = false; bReported = true; bReadyReported = false; bCommitted = false; ActiveRequest = FDemoEncounterPreloadRequest();
}

void UDemoLocalEncounterPreload::CommitPreload(int32 Generation)
{
    DEMO_LOG_CALL();
    if (Generation != ActiveRequest.Generation || !bReadyReported || bCommitted) { UE_LOG(LogFPSDemo, Log, TEXT("PRELOAD_COMMIT stale/unready ignored")); return; }
    if (GetWorld()->GetNetMode() == NM_Client && !ActiveRequest.Room.RoomId.IsNone())
        if (!GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>()->CommitPrepared()) { UE_LOG(LogFPSDemo, Error, TEXT("PRELOAD_COMMIT local room missing")); return; }
    bCommitted = true; // ListenWorld已由GM提交；本地PC只记录资源租约已消费。
}

void UDemoLocalEncounterPreload::ResetRooms()
{
    DEMO_LOG_CALL();
    Stop();
    if (GetWorld() && GetWorld()->GetNetMode() == NM_Client)
        GetWorld()->GetSubsystem<UDemoRoomStreamingSubsystem>()->ReleaseRooms(); // RPC可能先于Pawn位置复制，由World服务等玩家确已离开后卸载。
}

void UDemoLocalEncounterPreload::EndPlay(const EEndPlayReason::Type Reason) { DEMO_LOG_CALL(); Stop(); Super::EndPlay(Reason); }
