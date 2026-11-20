// 职责：第二期金币娱乐真实World回归。使用：-DemoGambleTest的Hub阶段调用，全部使用Editor隔离进度，不接触云或真实账号。
#include "Tests/DemoGambleTest.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "UI/Flow/DemoMenuFlowComponent.h" // 独立场地不能远距交互终端，用真实暂停页测试射击门禁。
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Economy/DemoShopComponent.h"
#include "Economy/Gambling/DemoCasinoSettings.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Economy/Gambling/DemoCasinoRange.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Interaction/DemoInteractable.h"
#include "Save/DemoRunSave.h"
#include "Save/Codecs/DemoCheckpointJsonCodec.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
bool ADemoGambleTest::CheckArcade()
{
    DEMO_LOG_CALL();
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); auto* Player=Cast<ADemoCharacter>(PC->GetPawn()); // 仅本隔离单人测试。
    auto* Progress=Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 正式权威事务。
    auto* Shop=Mode->FindComponentByClass<UDemoShopComponent>(); auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 正式保存委托和本地临时槽。
    const auto& Rules=GetDefault<UDemoCasinoSettings>()->Rules;
    // 捕获均仅本同步函数，调用生产拥有者命令；不传结果、概率、骰面或奖金。
    const auto Submit=[PC,Mode,Progress,&Rules](FDemoCasinoIntent Intent)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] ArcadeTest.Submit"));
        FDemoGameplayCommandRequest Request; Request.Command=EDemoGameplayCommand::Casino; Request.Casino=Intent; Request.Casino.RulesKey=Rules.Fingerprint();
        Request.GambleSerial=Progress->GetSnapshot().Gambling.Serial; Request.Terminal=Mode->GetShopTerminal(); Request.TerminalGeneration=Mode->BuildPlayerMenuSnapshot(PC).TerminalGeneration;
        return Mode->ExecutePlayerCommand(PC,Request);
    };
    FDemoCasinoIntent Intent; Intent.Mode=4; Intent.Stake=200000; auto Budget=Progress->GetSnapshot(); Budget.Coins=150000000; Progress->Commit(Budget); // 测试大额钱包，非玩家Debug权限。
    if(!Check(Submit(Intent).IsApplied(),TEXT("arcade dice opens using real 200000 gold wager"))) return false;
    const auto Before=Progress->GetSnapshot(); FString Error; auto* Decoded=NewObject<UDemoRunSave>(this); // 实际保存后JSON往返。
    if(!Check(DemoCheckpointJsonCodec::Decode(Saves->ExportCloud(0),*Decoded,Error)&&Decoded->Gambling.Arcade.Values==Before.Gambling.Arcade.Values&&Decoded->Coins==149800000,TEXT("arcade frozen dice and high gold survive full checkpoint codec"))) return false;
    const auto* Native=Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(Saves->SlotName(0),0)); // 重新读取实际磁盘文件，不复用GI缓存或JSON候选。
    if(!Check(Native&&Native->Gambling.Arcade.Values==Before.Gambling.Arcade.Values&&Native->Gambling.Arcade.Payouts==Before.Gambling.Arcade.Payouts&&Native->Coins==149800000,TEXT("native checkpoint restores revealed dice frozen payouts and high wallet"))) return false;
    Intent.Operation=1; Intent.KeepMask=31;
    Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(this,&ADemoGambleTest::RejectSave));
    const auto Rejected=Submit(Intent);
    Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(Mode,&AFPSDemoGameMode::SaveCheckpoint));
    if(!Check(!Rejected.IsApplied()&&Progress->GetSnapshot().Gambling.Arcade.Values==Before.Gambling.Arcade.Values&&Progress->GetSnapshot().Coins==Before.Coins&&Progress->GetSnapshot().Gambling.Serial==Before.Gambling.Serial,TEXT("dice save failure restores revealed faces serial and wallet"))) return false;
    auto Winning=Progress->GetSnapshot(); Winning.Gambling.Arcade.Values={6,6,6,6,6}; Progress->Commit(Winning); Mode->SaveCheckpoint(); // Editor测试夹具固定公开五同，不新增运行时作弊入口。
    const auto Paid=Submit(Intent);
    if(!Check(Paid.IsApplied()&&Paid.CasinoResult.Paid==2400000&&Progress->GetSnapshot().Coins==152200000&&Progress->GetSnapshot().Gambling.Arcade.Mode<0,TEXT("dice actual transaction pays above old cap and persists above old wallet ceiling"))) return false;
    if(!Check(!Submit(Intent).IsApplied(),TEXT("completed dice cannot pay twice"))) return false;
    Intent.Operation=0; Intent.Stake=MAX_int32;
    const int32 GoldBeforeOverflow=Progress->GetSnapshot().Coins; // 验证拒绝发生在扣费之前。
    if(!Check(!Submit(Intent).IsApplied()&&Progress->GetSnapshot().Coins==GoldBeforeOverflow,TEXT("unrepresentable wager refused without debit"))) return false;
    Intent.Mode=3; Intent.Stake=20;
    if(!Check(Submit(Intent).IsApplied()&&Progress->GetSnapshot().Gambling.Arcade.Mode==3,TEXT("reactor persistent charge contract opens"))) return false;
    auto Reactor=Progress->GetSnapshot(); Reactor.Gambling.Arcade.Chances.Init(1.,Reactor.Gambling.Arcade.Chances.Num()); Progress->Commit(Reactor); // 稳定检验事务路径，不依赖概率偶然失败。
    Intent.Operation=1;
    if(!Check(Submit(Intent).IsApplied()&&Progress->GetSnapshot().Gambling.Arcade.Step==1,TEXT("reactor successful charge saves intermediate step"))) return false;
    const int32 Frozen=Progress->GetSnapshot().Gambling.Arcade.Payouts[0]; Intent.Operation=2;
    if(!Check(Submit(Intent).CasinoResult.Paid==Frozen&&Progress->GetSnapshot().Gambling.Arcade.Mode<0,TEXT("reactor cashout uses frozen amount and consumes contract"))) return false;
    Intent.Mode=5; Intent.Operation=0;
    const auto Battle=Submit(Intent);
    if(!Check(Battle.IsApplied()&&!Battle.CasinoResult.Battle.IsEmpty()&&Battle.CasinoResult.Winner>=0&&Battle.CasinoResult.Winner<=1,TEXT("arena settles with actual alternating combat replay"))) return false;
    Intent.Mode=6;
    if(!Check(Submit(Intent).IsApplied()&&ADemoCasinoRange::Find(Player),TEXT("range saved bet creates actual world targets"))) return false;
    PC->CloseUpgradeMenu(); PC->GetMenuFlow()->EscapePressed(); // 已传送至独立房间，真实暂停覆盖层阻断射击；不伪造远距终端。
    Player->GetWeaponComponent()->StartFire();
    if(!Check(PC->HasBlockingOverlay()&&Progress->GetSnapshot().Gambling.Arcade.Shots==0,TEXT("range rejects fire while pause menu remains open"))) return false;
    PC->GetMenuFlow()->CloseOverlay(); // 训练开火遵守真实菜单/暂停准入。
    auto* Range=ADemoCasinoRange::Find(Player); // World拥有，下面立即同步借用。
    FVector Eye; FRotator Aim; Player->GetActorEyesViewPoint(Eye,Aim);
    Range->SetActorLocation(Eye+FVector(500,0,100)); // 靶位在房间内部，抬高头部射线而不穿越顶棚。
    PC->SetControlRotation((Range->GetActorLocation()+FVector(0,0,46.8)-Eye).Rotation()); // 上方头部的真实位置（父缩放.72×65）。
    Player->FindComponentByClass<UDemoWeaponComponent>()->StartFire(); // 正式Enhanced Input后端 → 独立GAS训练射击 → 权威射线 → 保存。
    if(!Check(Progress->GetSnapshot().Gambling.Arcade.Shots==1&&Progress->GetSnapshot().Gambling.Arcade.Step==1&&Progress->GetSnapshot().Gambling.Arcade.Heads==1,TEXT("range real GAS fire consumes one shot and scores a physical head hit"))) return false;
    if(!Check(DemoCheckpointJsonCodec::Decode(Saves->ExportCloud(0),*Decoded,Error)&&Decoded->Gambling.Arcade.Shots==1&&Decoded->Gambling.Arcade.DeadlineMs==Progress->GetSnapshot().Gambling.Arcade.DeadlineMs,TEXT("range hit and original deadline persisted without timer reset"))) return false;
    Intent.Operation=2;
    Range->Destroy(); // 真实销毁返回入场点；保存的未结挑战保留，可在终端领取。
    Mode->GetShopTerminal()->Interact(Player); // 领取仍需重新建立有效终端会话。
    if(!Check(Submit(Intent).IsApplied()&&Progress->GetSnapshot().Gambling.Arcade.Mode<0,TEXT("range early settlement clears score once"))) return false;
    UE_LOG(LogFPSDemo,Display,TEXT("ARCADE_RUNTIME_SUCCESS")); return true;
}
#endif
