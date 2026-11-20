// 职责：赏金靶场入场与离场的服务器位移。使用：Range生命周期调用，经济结算仍归DemoCasinoRange/Service。
#include "Economy/Gambling/DemoCasinoRange.h"
#include "Economy/Gambling/DemoRangeRoom.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Debug/DemoLog.h"
bool ADemoCasinoRange::EnterRoom()
{
    DEMO_LOG_CALL(); auto* Player=Shooter.Get(); auto* PC=Player?Cast<APlayerController>(Player->GetController()):nullptr; // 仅使用此次入场Avatar。
    if(!HasAuthority()||!PC) return false;
    Room=ADemoRangeRoom::Create(PC);
    if(!Room) { UE_LOG(LogFPSDemo,Warning,TEXT("RANGE_ENTER failed room creation; saved challenge preserved")); return false; }
    ReturnTransform=Player->GetActorTransform(); ReturnAim=PC->GetControlRotation();
    const FVector Location=Room->Entrance(Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()); // 由真实碰撞身高求入口，脚底在房间地面上。
    if(!Player->TeleportTo(Location,FRotator::ZeroRotator)) { Room->Destroy(); Room=nullptr; UE_LOG(LogFPSDemo,Warning,TEXT("RANGE_ENTER failed collision; saved challenge preserved")); return false; }
    bEnteredRoom=true; Player->GetCharacterMovement()->StopMovementImmediately(); PC->SetControlRotation(FRotator::ZeroRotator); PC->ClientSetRotation(FRotator::ZeroRotator,true); Player->ForceNetUpdate();
    UE_LOG(LogFPSDemo,Display,TEXT("RANGE_ENTER owner=%s from=%s to=%s"),*PC->GetName(),*ReturnTransform.GetLocation().ToString(),*Location.ToString()); return true;
}
void ADemoCasinoRange::ReturnFromRoom()
{
    DEMO_LOG_CALL(); if(!HasAuthority()||!bEnteredRoom) return;
    bEnteredRoom=false;
    auto* Player=Shooter.Get(); auto* PC=Player?Cast<APlayerController>(Player->GetController()):nullptr; const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 不跨Avatar重生/阶段迁移恢复旧点。
    if(!Player||!PC||PC!=GetOwner()||!State||State->Phase!=EDemoPhase::Hub||!IsValid(Room)||FVector::DistSquared(Player->GetActorLocation(),Room->GetActorLocation())>FMath::Square(2500.f)) { UE_LOG(LogFPSDemo,Log,TEXT("RANGE_RETURN skipped: avatar/phase/location changed")); return; }
    FVector Destination=ReturnTransform.GetLocation(); // 引擎查找附近无穿插位置；失败保留原Hub点上方安全余量。
    if(!GetWorld()->FindTeleportSpot(Player,Destination,ReturnTransform.Rotator())) Destination=ReturnTransform.GetLocation()+FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()*2.f);
    Player->SetActorLocationAndRotation(Destination,ReturnTransform.Rotator(),false,nullptr,ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->StopMovementImmediately(); PC->SetControlRotation(ReturnAim); PC->ClientSetRotation(ReturnAim,true); Player->ForceNetUpdate();
    UE_LOG(LogFPSDemo,Display,TEXT("RANGE_RETURN owner=%s location=%s"),*PC->GetName(),*Destination.ToString());
}
