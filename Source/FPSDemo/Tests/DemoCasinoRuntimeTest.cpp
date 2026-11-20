// 职责：金币娱乐真实World事务回归。使用：-DemoGambleTest的Hub阶段调用；只使用Editor临时存档，断言失败非零退出。
#include "Tests/DemoGambleTest.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Economy/DemoShopComponent.h"
#include "Economy/Gambling/DemoCasinoSettings.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Save/DemoRunSave.h"
#include "Save/Codecs/DemoCheckpointJsonCodec.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Interaction/DemoInteractable.h" // 真实读档后终端位置，独立翻译单元需要完整Actor定义。
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"

#if WITH_EDITOR
bool ADemoGambleTest::CheckCasino()
{
    DEMO_LOG_CALL();
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 独立单人测试夹具。
    auto* Player=Cast<ADemoCharacter>(PC->GetPawn()); // 同步借用真实Pawn。
    auto* Progress=Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 实际权威账本。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 正式拥有者命令入口。
    auto* Shop=Mode->FindComponentByClass<UDemoShopComponent>(); // 真实事务与保存委托。
    auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // Editor隔离保存。
    const auto& Rules=GetDefault<UDemoCasinoSettings>()->Rules; // 生产配置，不改CDO。
    // 捕获PC/Mode/Progress/Rules仅本同步函数，不逃逸；Serial可显式旧值测试幂等，其他意图走真实网关。
    const auto Submit=[PC,Mode,Progress,&Rules](FDemoCasinoIntent Intent,int32 Serial=INDEX_NONE)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CasinoRuntimeTest.Submit"));
        FDemoGameplayCommandRequest Request; Request.Command=EDemoGameplayCommand::Casino; Request.Casino=Intent; Request.Casino.RulesKey=Rules.Fingerprint(); // 生产规则版本。
        Request.GambleSerial=Serial==INDEX_NONE?Progress->GetSnapshot().Gambling.Serial:Serial;
        Request.Terminal=Mode->GetShopTerminal(); Request.TerminalGeneration=Mode->BuildPlayerMenuSnapshot(PC).TerminalGeneration;
        return Mode->ExecutePlayerCommand(PC,Request);
    };
    FDemoCasinoIntent Intent; // 默认0..49下注20；之后逐项覆盖。
    const int32 InitialGold=Progress->GetSnapshot().Coins,InitialSerial=Progress->GetSnapshot().Gambling.Serial; // 单笔金流与幂等基线。
    auto Number=Submit(Intent);
    if(!Check(Number.IsApplied()&&Number.CasinoResult.Number>=0&&Number.CasinoResult.Number<100
        &&Progress->GetSnapshot().Coins==InitialGold-20+Number.CasinoResult.Paid,TEXT("casino number uses real wallet debit and inclusive payout"))) return false;
    if(!Check(!Submit(Intent,InitialSerial).IsApplied()&&Progress->GetSnapshot().Gambling.Serial==InitialSerial+1,TEXT("casino replay rejected without another debit"))) return false;
    Intent.Mode=1; const int32 BeforeSlot=Progress->GetSnapshot().Coins; // 槽机三轴一次结算。
    auto Slot=Submit(Intent);
    if(!Check(Slot.IsApplied()&&Slot.CasinoResult.Symbols.Num()==3&&Slot.CasinoResult.Paid==DemoCasino::Pay(20,DemoCasino::SlotMultiplier(Rules,Slot.CasinoResult.Symbols))
        &&Progress->GetSnapshot().Coins==BeforeSlot-20+Slot.CasinoResult.Paid,TEXT("casino slot three symbols match persisted gold award"))) return false;
    Intent.Mode=2; Intent.Bombs=Rules.BombChoices[0];
    auto Open=Submit(Intent);
    const auto Original=Progress->GetSnapshot(); // 保存私有固定棋盘，用于读档/回滚比较，不打印雷位。
    if(!Check(Open.IsApplied()&&Original.Gambling.Mine.Validate()&&Original.Gambling.Mine.Stake==20,TEXT("casino mine opens and saves fixed private board"))) return false;
    if(!Check(Progress->GetPublicSnapshot().Gambling.Mine.BombMask==0&&Mode->BuildPlayerMenuSnapshot(PC).Progress.Gambling.Mine.BombMask==0,TEXT("casino PS and menu projections redact private bombs"))) return false;
    if(!Check(!Submit(Intent).IsApplied()&&Progress->GetSnapshot().Gambling.Mine.BombMask==Original.Gambling.Mine.BombMask,TEXT("casino pending mine cannot be replaced by a new wager"))) return false;
    FString Error; auto* Decoded=NewObject<UDemoRunSave>(this); // 纯JSON往返，不建立HTTP。
    if(!Check(DemoCheckpointJsonCodec::Decode(Saves->ExportCloud(0),*Decoded,Error)&&Decoded->Version==GetDefault<UDemoRunSave>()->Version
        &&Decoded->Gambling.Mine.BombMask==Original.Gambling.Mine.BombMask&&Decoded->Gambling.Mine.Payouts==Original.Gambling.Mine.Payouts,TEXT("casino V7 codec restores identical fixed board and frozen payouts"))) return false;
    Intent.Operation=2;
    if(!Check(!Submit(Intent).IsApplied(),TEXT("casino cannot refund an unopened mine"))) return false;
    Intent.Operation=1; Intent.Tile=0;
    while(Original.Gambling.Mine.BombMask&(1<<Intent.Tile)) ++Intent.Tile; // 测试夹具选择已知安全格，生产UI无法访问该值。
    Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(this,&ADemoGambleTest::RejectSave));
    auto Rejected=Submit(Intent); // 故障发生在选中安全格后，不允许发布候选。
    Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(Mode,&AFPSDemoGameMode::SaveCheckpoint));
    if(!Check(!Rejected.IsApplied()&&Rejected.CasinoResult.Mode==INDEX_NONE&&Progress->GetSnapshot().Gambling.Serial==Original.Gambling.Serial
        &&Progress->GetSnapshot().Gambling.Mine.OpenMask==0&&Progress->GetSnapshot().Coins==Original.Coins,TEXT("casino save failure rolls back tile serial and wallet without reveal"))) return false;
    if(!Check(Submit(Intent).IsApplied()&&Progress->GetSnapshot().Gambling.Mine.OpenCount()==1,TEXT("casino safe tile commits once"))) return false;
    if(!Check(!Submit(Intent).IsApplied(),TEXT("casino same tile rejected even with fresh serial"))) return false;
    PC->CloseUpgradeMenu(); // 普通关页不能重置已开格或未来棋盘。
    if(!Check(Progress->GetSnapshot().Gambling.Mine.BombMask==Original.Gambling.Mine.BombMask,TEXT("casino closing UI preserves fixed board"))) return false;
    if(!Check(Saves->SelectSlot(0),TEXT("casino reload reads the actual saved slot"))) return false;
    const UDemoRunSave* Reload=Saves->ConsumePending(); // 从实际存盘重新加载，而非只测试内存副本。
    GetWorld()->GetGameState<ADemoGameState>()->Phase=EDemoPhase::Lobby; // 恢复接口只允许从大厅初始化；隔离测试重建这一前置条件，不放宽生产阶段守卫。
    if(!Check(Reload&&Mode->RestoreCheckpoint(*Reload)&&Progress->GetSnapshot().Gambling.Mine.BombMask==Original.Gambling.Mine.BombMask
        &&Progress->GetSnapshot().Gambling.Mine.OpenCount()==1&&Progress->GetSnapshot().Coins==Original.Coins,TEXT("casino restoring checkpoint preserves revealed tile wallet and hidden board"))) return false;
    Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,0)); // 读档重建终端后必须重新靠近/建立交互。
    // 此步骤保持与生产相同交互路径，关闭后必须重新建立终端会话。
    FDemoGameplayCommandRequest Begin; Begin.Command=EDemoGameplayCommand::BeginInteraction; Begin.Terminal=Mode->GetShopTerminal();
    if(!Check(Mode->ExecutePlayerCommand(PC,Begin).IsApplied(),TEXT("casino terminal can reconnect to pending board"))) return false;
    const int32 Available=Progress->GetSnapshot().Gambling.Mine.Available(); // 冻结金额不重新查配置。
    Intent.Operation=2;
    auto Cash=Submit(Intent);
    if(!Check(Cash.IsApplied()&&Cash.CasinoResult.Paid==Available&&Progress->GetSnapshot().Coins==Original.Coins+Available&&Progress->GetSnapshot().Gambling.Mine.Stake==0,TEXT("casino cashout credits frozen amount once and clears pending box"))) return false;
    if(!Check(!Submit(Intent).IsApplied(),TEXT("casino double cashout rejected"))) return false;
    Intent.Operation=0; Intent.Tile=INDEX_NONE;
    if(!Check(Submit(Intent).IsApplied(),TEXT("casino another round allowed after cashout"))) return false;
    const auto BeforeBomb=Progress->GetSnapshot(); // 已扣下注，踩雷只结束不重复扣款。
    Intent.Operation=1; Intent.Tile=0;
    while(!(BeforeBomb.Gambling.Mine.BombMask&(1<<Intent.Tile))) ++Intent.Tile;
    auto Bomb=Submit(Intent);
    if(!Check(Bomb.IsApplied()&&Bomb.CasinoResult.bBomb&&Progress->GetSnapshot().Coins==BeforeBomb.Coins&&Progress->GetSnapshot().Gambling.Mine.Stake==0,TEXT("casino bomb ends box without double debit or free refund"))) return false;
    Intent.Operation=0; Intent.Mode=0; Intent.Stake=0;
    if(!Check(!Submit(Intent).IsApplied(),TEXT("casino invalid bet rejected by authority"))) return false;
    const auto BeforeLarge=Progress->GetSnapshot(); // 新用例不污染其他旧玩法夹具；只检验生产事务不再使用200/50上限。
    Intent.Stake=BeforeLarge.Coins+1;
    if(!Check(!Submit(Intent).IsApplied()&&Progress->GetSnapshot().Coins==BeforeLarge.Coins,TEXT("custom stake above balance rejected without debit"))) return false;
    auto LargeBudget=BeforeLarge; LargeBudget.Coins=3000000; Progress->Commit(LargeBudget); // 隔离Editor账本，为大额存盘准备确定预算。
    Intent.Stake=777; Intent.Low=Intent.High=17;
    const auto LargeNumber=Submit(Intent);
    if(!Check(LargeNumber.IsApplied()&&Progress->GetSnapshot().Coins==3000000-777+LargeNumber.CasinoResult.Paid,TEXT("single number accepts typed stake above old maximum"))) return false;
    Intent.Mode=2; Intent.Stake=4000; // 最后一级完整到账4807万，可表示；旧200万下注应在扣款前因溢出拒绝。
    if(!Check(Submit(Intent).IsApplied()&&Progress->GetSnapshot().Gambling.Mine.Stake==4000,TEXT("mine accepts large stake with uncapped representable payout"))) return false;
    if(!Check(DemoCheckpointJsonCodec::Decode(Saves->ExportCloud(0),*Decoded,Error)&&Decoded->Gambling.Mine.Stake==4000,TEXT("large mine wager round trips through checkpoint JSON"))) return false;
    Intent.Operation=1; Intent.Tile=0;
    while(!(Progress->GetSnapshot().Gambling.Mine.BombMask&(1<<Intent.Tile))) ++Intent.Tile; // 确定结束夹具货箱，无隐藏生产接口。
    if(!Check(Submit(Intent).IsApplied(),TEXT("large stake mine ends without overflow"))) return false;
    Intent.Operation=0; Intent.Mode=0;
    auto* State=GetWorld()->GetGameState<ADemoGameState>(); State->Phase=EDemoPhase::Intermission; Intent.Stake=20; // 真实阶段守卫。
    const auto WrongPhase=Submit(Intent); State->Phase=EDemoPhase::Hub;
    if(!Check(!WrongPhase.IsApplied(),TEXT("casino is blocked outside safe hub"))) return false;
    if(!CheckArcade()) return false;
    FDemoPlayerProgressSnapshot Restore=Progress->GetSnapshot(); Restore.Coins=6000; // 后续旧遗物/GAS测试使用固定夹具预算。
    Progress->Commit(Restore); Mode->SaveCheckpoint(); PC->PublishMenuSnapshot();
    UE_LOG(LogFPSDemo,Display,TEXT("CASINO_RUNTIME_SUCCESS"));
    return true;
}
#endif
