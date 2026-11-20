// 职责：合作控制器请求及准入校验。使用：服务器根据当前协议/阶段授权；新增Tag字典后与房间发现版本同时升级。
#include "Economy/Gambling/DemoGambleService.h"
#include "Economy/Gambling/DemoCasinoSettings.h" // 菜单公开配置，不含随机流和私有雷位。
#include "Game/FPSDemoGameMode.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Debug/DemoEditorCheats.h" // Editor单人免扣也要同步按钮可用性；包体分支完全裁去。
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/Coop/DemoCoopProtocol.h"
#include "Progression/Summary/DemoRunSummaryComponent.h"
#include "Player/Coop/Ready/DemoTeamReadyComponent.h"
#include "Characters/DemoCharacter.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Interaction/DemoInteractable.h"
#include "Economy/DemoShopComponent.h"
#include "Economy/DemoUpgradeCatalog.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Save/DemoRunSave.h"
#include "World/DemoEncounterAreaSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Player/Online/DemoOnlineIdentity.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Player/Coop/DemoCoopSessionSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoNetworkDiagnostics.h" // Editor专用生命周期证据；包体宏不求值，无新增网络行为。
#include "Kismet/GameplayStatics.h" // PreLogin解析受限身份/协议选项，不直接采用远端URL执行命令。

UDemoCoopRosterComponent* AFPSDemoGameMode::GetCoopRoster() const { DEMO_LOG_TICK(); return CoopRoster; }
void AFPSDemoGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
    DEMO_LOG_CALL(); Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
    const ADemoGameState* State = GetGameState<ADemoGameState>(); // 在创建玩家 Actor 前拒绝非安全区加入。
    if (ErrorMessage.IsEmpty() && (!State || !RunFlow->CanAcceptConnections())) ErrorMessage = TEXT("Join is allowed only after the safe hub is ready"); // 旅行/读档中的旧Hub也不接受连接。
    if (ErrorMessage.IsEmpty() && GetNumPlayers() >= 4) ErrorMessage = TEXT("This cooperative room supports at most four players");
    // 搜索广播只是提示；连接瞬间再次校验协议和固定成员身份，防止旧列表绕过准入。
    if (ErrorMessage.IsEmpty() && UGameplayStatics::ParseOption(Options, DemoCoopProtocol::LoginOption) != DemoCoopProtocol::Version)
        ErrorMessage = TEXT("Incompatible cooperative protocol"); // 与搜索/登录共同读取唯一版本；缺失、旧版或手工连接均不能绕过。
    const UDemoOnlineIdentity* Identity = GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>(); // 公网身份取实际P2P源，不能用Options覆盖模式。
    FString Key = UGameplayStatics::ParseOption(Options, TEXT("DemoPlayerKey")); // LAN维持可信本机GUID语义；Public下面强制替换。
    if (Identity->IsPublic())
    {
        if (!Identity->IsReady() || !UniqueId.IsValid() || !UDemoOnlineIdentity::MatchTransportIdentity(Address, *UniqueId)) ErrorMessage = TEXT("Authenticated EOS transport identity required");
        Key = UniqueId.IsValid() ? UDemoOnlineIdentity::PlayerKeyFromNetId(*UniqueId) : FString();
    }
    else if (Address.StartsWith(TEXT("EOS:"))) ErrorMessage = TEXT("LAN room does not accept EOS transport");
    if (ErrorMessage.IsEmpty() && !CoopRoster->CanAdmit(Key)) ErrorMessage = TEXT("Fixed team is full or this member is already online");
    if (!ErrorMessage.IsEmpty()) UE_LOG(LogFPSDemo, Log, TEXT("COOP_LOGIN_REJECT %s"), *ErrorMessage);
}
bool AFPSDemoGameMode::RegisterCoopPlayer(ADemoPlayerController* Player, const FString& PlayerKey)
{
    DEMO_LOG_CALL();
    const UDemoOnlineIdentity* Identity = GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>(); // 在恢复检查点前再次验证当前拥有者；RPC自报键没有授权作用。
    FString VerifiedKey = PlayerKey; // LAN沿用设备关联键；Public必须由服务器连接推导。
    if (Identity->IsPublic() && GetNetMode() != NM_Standalone)
    {
        if (!Player || !Identity->IsReady()) return false;
        if (Player->IsLocalController()) VerifiedKey = Identity->GetOnlinePlayerKey();
        else
        {
            const APlayerState* PS = Player->PlayerState; // 服务器握手设置的ID仍必须与当前P2P远端一致。
            UNetConnection* Connection = Player->GetNetConnection(); // 引擎持有的实际连接，不来自RPC参数。
            if (!PS || !PS->GetUniqueId().IsValid() || !Connection || !UDemoOnlineIdentity::MatchTransportIdentity(Connection->LowLevelGetRemoteAddress(true), *PS->GetUniqueId()))
            { UE_LOG(LogFPSDemo, Warning, TEXT("EOS_REGISTER rejected transport identity mismatch")); return false; }
            VerifiedKey = UDemoOnlineIdentity::PlayerKeyFromNetId(*PS->GetUniqueId());
        }
        if (VerifiedKey.IsEmpty() || VerifiedKey != PlayerKey) { UE_LOG(LogFPSDemo, Warning, TEXT("EOS_REGISTER rejected claimed key")); return false; }
    }
    if (!CoopRoster->RegisterPlayer(Player, VerifiedKey)) { UE_LOG(LogFPSDemo, Warning, TEXT("COOP_REGISTER rejected")); return false; }
    GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->RegisterMember(Player); // 目录人数与实际认证的成员同步，固定队伍数据仍由Roster保存。
    ADemoCharacter* Pawn = Cast<ADemoCharacter>(Player->GetPawn()); // 已注册本人 Avatar。
    const ADemoGameState* State = GetGameState<ADemoGameState>(); // 只在真正安全区放置队友，Lobby房主由Flow负责。
    if (State && State->Phase == EDemoPhase::Hub && !Player->IsLocalController())
    {
        FDemoAreaSnapshot Area; FString Error; // 地图配置快照，避免用终端中心作为出生点。
        if (GetWorld()->GetSubsystem<UDemoEncounterAreaSubsystem>()->ResolveArea(-1, Area, Error))
            Pawn->SetActorLocation(Area.PreparedStart + FVector(0, 120.f * (CoopRoster->GetPlayers().Num() - 1), 0), false, nullptr, ETeleportType::TeleportPhysics);
        Pawn->SetCoopEliminated(false);
    }
    TeamReadySystem->InvalidateAll(TEXT("队伍成员加入或重新登记，请重新准备")); // 新身份必须主动确认，不继承上次连接状态。
    Player->PublishMenuSnapshot();
    if (State && State->Phase == EDemoPhase::Hub && !SaveCheckpoint()) UE_LOG(LogFPSDemo, Warning, TEXT("COOP_REGISTER checkpoint pending"));
    return true;
}
void AFPSDemoGameMode::Logout(AController* Exiting)
{
    DEMO_LOG_CALL();
    const UWorld* World=GetWorld(); // 同步短期借用；切图及停止PIE均会拆World，GI子系统可能已清理，不继续做房间/奖励/存档事务。
    DEMO_NET_DIAG(TEXT("Logout.Begin"), World, FString::Printf(TEXT("controllerValid=%d"), IsValid(Exiting))); // 不输出玩家标识，按World/PIE编号关联。
    if (!World || World->bIsTearingDown)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("COOP_LOGOUT_WORLD_TEARDOWN skipping gameplay cleanup controller=%s"),*GetNameSafe(Exiting));
        DEMO_NET_DIAG(TEXT("Logout.Super.Begin"), World, TEXT("path=worldTeardown"));
        Super::Logout(Exiting); // 保留原防崩溃语义；诊断不能替代或改变引擎释放流程。
        DEMO_NET_DIAG(TEXT("Logout.Super.End"), World, TEXT("path=worldTeardown"));
        DEMO_NET_DIAG(TEXT("Logout.End"), World, TEXT("path=worldTeardown"));
        return;
    }
    ADemoPlayerController* Player = Cast<ADemoPlayerController>(Exiting); // 离线前仍可读取 Pawn，会话必须先撤销。
    ADemoCharacter* Pawn = Player ? Cast<ADemoCharacter>(Player->GetPawn()) : nullptr; // 战斗掉线按离场处理，不继续提供免死目标。
    if (Pawn) { TerminalSystem->EndInteraction(Pawn); Pawn->SetCoopEliminated(true); }
    if (Player) CoopRoster->UnregisterPlayer(Player);
    UGameInstance* Instance=GetGameInstance(); // 退出过程中不拥有GI；子系统集合可在World网络连接之前销毁。
    UDemoRoomSubsystem* Room=Instance?Instance->GetSubsystem<UDemoRoomSubsystem>():nullptr; // 仅有效运行实例才能注销线上成员，禁止对空this调用成员函数。
    DEMO_NET_DIAG(TEXT("Logout.Room.Begin"), World, FString::Printf(TEXT("roomValid=%d"), IsValid(Room))); // 只检查本次已取得的指针，不重新查找/创建子系统。
    if (IsValid(Room)) Room->UnregisterMember(Player); // 必须在Super销毁PS前取在线ID。
    else UE_LOG(LogFPSDemo,Log,TEXT("COOP_LOGOUT_ROOM_UNAVAILABLE controller=%s"),*GetNameSafe(Exiting));
    DEMO_NET_DIAG(TEXT("Logout.Room.End"), World, TEXT(""));
    DEMO_NET_DIAG(TEXT("Logout.Super.Begin"), World, TEXT("path=normal"));
    Super::Logout(Exiting);
    TeamReadySystem->InvalidateAll(TEXT("队伍成员离开，请重新准备")); // 在Super移除PC之后重建公开目录。
    DEMO_NET_DIAG(TEXT("Logout.Super.End"), World, TEXT("path=normal"));
    if (GetGameState<ADemoGameState>() && GetGameState<ADemoGameState>()->Phase == EDemoPhase::Reward)
    { RunFlow->ReconcileRewardReadiness(); SaveCheckpoint(); } // 最后未领队员离线也不能永久阻塞剩余玩家。
    if (GetGameState<ADemoGameState>() && GetGameState<ADemoGameState>()->Phase == EDemoPhase::Combat) NotifyPlayerDied(Pawn);
    DEMO_NET_DIAG(TEXT("Logout.End"), World, TEXT("path=normal")); // 成对事件的mono差值能定位退出卡在业务还是Super内部。
}
void AFPSDemoGameMode::NotifyPlayerDied(ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    TeamReadySystem->InvalidatePlayer(Player?Cast<ADemoPlayerController>(Player->GetController()):nullptr,TEXT("成员阵亡，已撤销准备"));
    if (Player) TerminalSystem->EndInteraction(Player);
    const TArray<ADemoCharacter*> Alive = CoopRoster->GetPlayers(true); // 当前仍存活的战斗队友，不使用 Player0 判断团灭。
    if (Alive.IsEmpty()) { RunFlow->NotifyPlayerDied(); return; }
    for (ADemoCharacter* Member : CoopRoster->GetPlayers()) // 某位观战目标再次死亡时重定向到其他存活队友。
        if (Member->IsCoopEliminated())
            if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(Member->GetController()))
            {
                PC->SetViewTarget(Alive[0]); // 服务器逻辑目标仍随活队友，网络相关性不能停在死亡落点；引擎可能先发同PC的ClientSetViewTarget。
                PC->ClientSpectateTeammate(Alive[0]); // 随后的同PC可靠RPC将最终本地ViewTarget换为独立TP相机，避免远端FP私有层暴露。
                PC->PublishMenuSnapshot(); // 死者仍Possess原Pawn保留经济关系，菜单只更新其本人快照。
            }
    UE_LOG(LogFPSDemo, Log, TEXT("COOP_SPECTATE alive=%d"), Alive.Num());
}
bool AFPSDemoGameMode::RegisterWeaponShot(FName WeaponId, ADemoCharacter* Source)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetGameState<ADemoGameState>(); // 战斗与关间备战均属于同轮挑战；只记录实际提交枪械的所属玩家。
    ADemoPlayerState* PS = Source ? Source->GetPlayerState<ADemoPlayerState>() : nullptr; // 服务器拥有者身份，不由客户端请求指定。
    if (!HasAuthority() || !State || (State->Phase != EDemoPhase::Combat && State->Phase != EDemoPhase::Intermission)
        || !PS || !Source->CanUseFirearmActions() || DemoWeaponCatalog::IndexOf(WeaponId) < 0)
    { UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_CHALLENGE_REJECT authority/phase/source/weapon")); return false; }
    FDemoPlayerProgressSnapshot Next = PS->GetProgressComponent()->GetSnapshot(); // 不触碰队友资格。
    Next.PistolChallenge = WeaponId == TEXT("pistol") ? FMath::Max(1, Next.PistolChallenge) : 2;
    if (Next.PistolChallenge != PS->GetProgressComponent()->GetSnapshot().PistolChallenge && !PS->GetProgressComponent()->Commit(Next)) return false; // 稳定资格不逐发复制整份菜单；保存失败的重试仍在下面执行。
    return GetGameInstance()->GetSubsystem<UDemoRunSaves>()->StorePlayerChallenge(CoopRoster->GetPlayerKey(Source), Next.PistolChallenge, Next.BestEndlessLevel);
}
FDemoPlayerMenuSnapshot AFPSDemoGameMode::BuildPlayerMenuSnapshot(ADemoPlayerController* Player) const
{
    DEMO_LOG_TICK();
    FDemoPlayerMenuSnapshot View; // 独立返回值，不把队友快照放入拥有者 RPC。
    ADemoCharacter* Pawn = Player ? Cast<ADemoCharacter>(Player->GetPawn()) : nullptr; // 只能访问该控制器本人。
    const ADemoPlayerState* PS = Pawn ? Pawn->GetPlayerState<ADemoPlayerState>() : nullptr; // 尚未就绪时保持禁用投影。
    const ADemoGameState* State = GetGameState<ADemoGameState>(); // 公开战局只提供阶段与关号。
    if (!PS || !State) return View;
    View.Build=PS->GetRunBuildComponent()->GetSnapshot();
    View.Summary=PS->GetRunSummaryComponent()->GetSnapshot(); // 只有该PC可收到，不用已清理的Build/Progress反推上一轮。
    View.bBranchReward=RunFlow->UsesDemoCampaign() && State->Phase==EDemoPhase::Reward && (State->LevelNumber==3||State->LevelNumber==7);
    for(int32 Index=0;Index<DemoRunBuild::CoreCount();++Index) // 可信中央目录；显示、服务器选择与存档白名单不各维护独立数量。
    {
        FDemoShopOfferSnapshot Offer; // 只发本人当前可选状态，不接受客户端提交资产类。
        Offer.Id=DemoRunBuild::CoreAt(Index); Offer.Title=DemoRunBuild::Title(Offer.Id).ToString(); Offer.Detail=DemoRunBuild::Describe(Offer.Id).ToString();
        Offer.bSelected=View.Build.CoreId==Offer.Id; Offer.bUnlocked=true;
        Offer.BlockReason=State->Phase==EDemoPhase::Hub&&!State->bEndless&&!View.Build.bLocked?FString():TEXT("只能在战役安全区出发前选择");
        View.CoreOffers.Add(Offer);
    }
    View.Progress = PS->GetProgressComponent()->GetPublicSnapshot(); View.bCanLeadSession = CoopRoster->IsLeader(Player);
#if WITH_EDITOR
    // 只扩本人菜单可装备项；PS真实Ownership/派生缓存不变，保存与云仍只捕获真实账本。
    if (GetNetMode()==NM_Standalone && Player->IsLocalController())
        for (int32 Index=0;Index<DemoWeaponCatalog::Num();++Index)
        {
            const FName WeaponId=DemoWeaponCatalog::IdAt(Index); // 固定目录身份，不能由UI传入资源路径。
            if (PS->GetProgressComponent()->IsWeaponUnlocked(WeaponId)) View.Progress.UnlockedWeaponIds.AddUnique(WeaponId);
        }
#endif
    View.PrimaryWeaponId = DemoWeaponCatalog::IdAt(Pawn->GetWeaponComponent()->GetPrimaryIndex() + 1); // 旧主枪索引-1表示无选择，不映射成pistol。
    if (Pawn->GetWeaponComponent()->GetPrimaryIndex() < 0) View.PrimaryWeaponId = NAME_None;
    View.bCanChooseReward = State->Phase == EDemoPhase::Reward && View.Progress.LastRewardLevel < State->LevelNumber;
    const UDemoAttributeSet* Attributes = Pawn->GetDemoAttributes(); // 只判断会因放弃丢失的临时成长。
    View.bHasRunUpgrades = Attributes && (Attributes->GetMaxHealth() > 100.f + View.Progress.UpgradeProgress.PermanentHealth
        || Attributes->GetWeaponDamageBonus() > View.Progress.UpgradeProgress.PermanentDamage || Attributes->GetMagazineBonus() > View.Progress.UpgradeProgress.PermanentMagazine
        || Pawn->GetHealAmount() > 35.f || Pawn->GetDashSpeed() > 1300.f);
    for(FName Id:View.Progress.Gambling.Talents) // 每个拥有者独立生成等级描述；联机不暴露队友账本。
    {
        const int32 Rank=View.Progress.Gambling.Rank(Id); // 本次权威快照等级，旧档自动解释为1。
        View.OwnedTalentViews.Add({Id,FString::Printf(TEXT("%s  Lv.%d"),*DemoGamble::Name(Id),Rank),DemoGamble::DescribeTalent(Id,Rank)});
    }
    View.bHasRunUpgrades |= !View.Progress.Gambling.Talents.IsEmpty(); // 天赋也属于主动放弃将丢失的成长。
    View.TerminalBlockReason = ShopSystem->GetTerminalBlockReason(Pawn);
    if (TerminalSystem->ValidateSession(Pawn, true).IsEmpty()) View.Terminal = TerminalSystem->GetShopTerminal();
    else if (TerminalSystem->ValidateSession(Pawn, false).IsEmpty()) { View.Terminal = TerminalSystem->GetNextLevelTerminal(); View.bNextLevel = true; }
    View.TerminalGeneration = View.Terminal ? TerminalSystem->GetGeneration() : 0;
    ShopSystem->ProjectCasinoDevice(Pawn,View); // 已批准实体设备独立会话，原菜单沿同一Rules和钱包投影。
    for (int32 Choice = 0; Choice < 3; ++Choice) // 三个商品独立定价，不能用当前默认Choice代替全部行。
    {
        FDemoShopOfferSnapshot Offer; // 只在此拥有者回执中存活的商品投影。
        Offer.Cost = ShopSystem->GetPlayerUpgradeCost(Pawn, Choice); Offer.BlockReason = ShopSystem->GetPurchaseBlockReason(Pawn, Choice);
        const UDemoUpgradeCatalog* Upgrades = UDemoUpgradeCatalog::Get(); // 全进程只读配置；RPC把实际报价/增量发给本人，客户端无需有相同目录数值。
        const FDemoUpgradeDefinition* Item = Upgrades ? Upgrades->Find(false,Choice) : nullptr; // 业务索引稳定，资产可调展示和增量。
        if (Item && Attributes)
        {
            Offer.Id=Item->Id; Offer.Title=Item->Title.ToString(); Offer.Gain=Item->Increment;
            Offer.CurrentValue=Choice==0?Attributes->GetWeaponDamageBonus():Choice==1?Attributes->GetMaxHealth():Attributes->GetMagazineBonus();
            Offer.Detail=Choice==0?FString::Printf(TEXT("全武器伤害加成 +%g"),Offer.Gain):Choice==1?FString::Printf(TEXT("生命上限 +%g，并恢复等量生命"),Offer.Gain):FString::Printf(TEXT("全武器容量 +%g，并补给等量弹药"),Offer.Gain); // UE5.4格式检查要求字面量数组。
        }
        View.UpgradeOffers.Add(Offer);
        FDemoShopOfferSnapshot Reward; // 免费奖励同样由服务器投影，达到上限的实际增量显示0。
        Item=Upgrades?Upgrades->Find(true,Choice):nullptr;
        if (Item && Attributes)
        {
            Reward.Id=Item->Id; Reward.Title=Item->Title.ToString();
            Reward.CurrentValue=Choice==0?Attributes->GetWeaponDamageBonus():Choice==1?Pawn->GetHealAmount():Pawn->GetDashSpeed();
            Reward.Gain=FMath::Clamp(Item->Cap-Reward.CurrentValue,0.f,Item->Increment);
            Reward.Detail=Choice==0?FString::Printf(TEXT("全武器伤害加成 +%g"),Reward.Gain):Choice==1?FString::Printf(TEXT("治疗恢复量 +%g HP"),Reward.Gain):FString::Printf(TEXT("冲刺速度 +%g cm/s"),Reward.Gain); // 所有文案使用真实增量。
            Reward.BlockReason=View.bCanChooseReward?FString():TEXT("当前不能领取奖励");
        }
        View.RewardOffers.Add(Reward);
    }
    if(View.bBranchReward)
    {
        View.RewardOffers.Reset();
        for(int32 Choice=0;Choice<2;++Choice) // 第3/7关互斥组取代原奖励，不额外赠送属性。
        {
            FDemoShopOfferSnapshot Offer;
            Offer.Id=FName(*FString::Printf(TEXT("%s-%c"),*View.Build.CoreId.ToString(),(State->LevelNumber==3?'A':'C')+Choice));
            Offer.Title=DemoRunBuild::Title(Offer.Id).ToString(); Offer.Detail=DemoRunBuild::Describe(Offer.Id).ToString();
            Offer.BlockReason=View.bCanChooseReward&&View.Build.PendingBranchStage==State->LevelNumber?FString():TEXT("该分支已领取或未开放");
            View.RewardOffers.Add(Offer);
        }
    }
    View.bHasRunUpgrades|=View.Build.bLocked;
    const UDemoAmmoComponent* Ammo = Pawn->FindComponentByClass<UDemoAmmoComponent>(); // 本人可恢复弹药解锁。
    const UDemoAmmoCatalog* Catalog = Ammo ? Ammo->GetCatalog() : nullptr; // 权威配置，客户端展示不解析业务。
    for (int32 Index = 0; Index < 4; ++Index)
    {
        FDemoShopOfferSnapshot Offer; // 缺配置保持明确拒绝，不产生零价购买。
        if (Catalog && Catalog->Entries.IsValidIndex(Index))
        {
            Offer.Cost = Catalog->Entries[Index].UnlockGoldCost; Offer.bUnlocked = Ammo->IsUnlocked(Index);
            Offer.bSelected = Ammo->GetSelected() == Index; Offer.BlockReason = ShopSystem->GetAmmoBlockReason(Pawn);
            int32 RequiredCost=Offer.Cost; // 对外报价不变，余额校验与服务器实际扣款相同。
#if WITH_EDITOR
            RequiredCost=DemoEditorCheats::Debit(Pawn,true,RequiredCost);
#endif
            if (Offer.BlockReason.IsEmpty() && !Offer.bUnlocked && View.Progress.Coins < RequiredCost) Offer.BlockReason = TEXT("个人金币不足");
        }
        View.AmmoOffers.Add(Offer);
    }
    View.CasinoRules=GetDefault<UDemoCasinoSettings>()->Rules; // 本人菜单读权威公开配置。
    View.CasinoRules.Validate(View.CasinoConfigError);
    for(int32 Choice=0;Choice<16;++Choice) View.GambleOffers.Add(DemoGambleService::Quote(*ShopSystem,Pawn,Choice)); // 同一权威策略生成全部报价。
    return View;
}
FDemoCommandResult AFPSDemoGameMode::ExecutePlayerCommand(ADemoPlayerController* Player, const FDemoGameplayCommandRequest& Request)
{
    DEMO_LOG_CALL();
    FDemoCommandResult Result=ExecutePlayerCommandInternal(Player,Request); // 同一游戏线程先完成事务，失败不得虚假撤销准备。
    if(Result.IsApplied())
    {
        switch(Request.Command)
        {
        case EDemoGameplayCommand::BeginInteraction:
        case EDemoGameplayCommand::EndInteraction:
            TeamReadySystem->InvalidatePlayer(Player,TEXT("终端交互变化，请重新准备")); break;
        case EDemoGameplayCommand::SelectCore:
        case EDemoGameplayCommand::ChooseReward:
        case EDemoGameplayCommand::Casino:
        case EDemoGameplayCommand::Gamble:
        case EDemoGameplayCommand::UpgradeStat:
        case EDemoGameplayCommand::EquipWeapon:
        case EDemoGameplayCommand::EquipAmmo:
        case EDemoGameplayCommand::PurchaseAmmo:
            TeamReadySystem->InvalidatePlayer(Player,TEXT("成长或装备变化，请重新准备")); break;
        default: break;
        }
    }
    return Result;
}
FDemoCommandResult AFPSDemoGameMode::ExecutePlayerCommandInternal(ADemoPlayerController* Player, const FDemoGameplayCommandRequest& Request)
{
    DEMO_LOG_CALL();
    ADemoCharacter* Pawn = Player ? Cast<ADemoCharacter>(Player->GetPawn()) : nullptr; // 绝不接受请求体指定另一个玩家。
    const ADemoGameState* State = GetGameState<ADemoGameState>(); // 只借用当前World，旅行后旧请求被服务拒绝。
    if (!HasAuthority() || !Player || Player->GetWorld() != GetWorld() || !Pawn || !State) return FDemoCommandResult::Reject(TEXT("NotReady"), TEXT("玩家尚未就绪"));
    if (ShopSystem->IsTransactionInProgress()) return FDemoCommandResult::Reject(TEXT("TransactionBusy"),TEXT("正在提交本地交易，请稍后再试")); // 同步保存期间禁止回调重入出发/旅行覆写候选。
    if (Request.Command == EDemoGameplayCommand::RefreshView) return FDemoCommandResult::Applied(true, FString());
    if (Request.Command == EDemoGameplayCommand::BeginInteraction)
    {
        if (!TerminalSystem->BeginInteraction(Pawn,Request.Terminal)) return FDemoCommandResult::Reject(TEXT("InteractRejected"),TEXT("交互失败，请靠近当前终端"));
        ShopSystem->CloseCasinoDevice(Pawn); return FDemoCommandResult::Applied(true,FString());
    }
    if (Request.Command == EDemoGameplayCommand::CloseCasinoDevice) { if(!Request.CasinoDeviceToken.IsValid()) return FDemoCommandResult::Reject(TEXT("LeaseExpired"),TEXT("设备会话已过期")); ShopSystem->CloseCasinoDevice(Pawn,Request.CasinoDeviceToken); return FDemoCommandResult::Applied(true,FString()); }
    if (Request.Command == EDemoGameplayCommand::EndInteraction) { TerminalSystem->EndInteraction(Pawn); return FDemoCommandResult::Applied(true, FString()); }
    if (Request.TerminalGeneration && !TerminalSystem->ValidateInteraction(Pawn, Request.Terminal, Request.TerminalGeneration).IsEmpty())
        return FDemoCommandResult::Reject(TEXT("StaleInteraction"), TEXT("交互已过期，请重新打开终端"));
    switch (Request.Command)
    {
    case EDemoGameplayCommand::SetReady:
    {
        FString Error; // 可读拒绝原因与GS队伍状态一致，不从客户端接受玩家身份。
        return TeamReadySystem->SetReady(Player,Request.bReady,Request.ReadyRevision,Request.ExpectedLevel,Error)
            ?FDemoCommandResult::Applied(true,Request.bReady?TEXT("已准备，等待房主出发"):TEXT("已取消准备"))
            :FDemoCommandResult::Reject(TEXT("ReadyRejected"),Error);
    }
    case EDemoGameplayCommand::SelectCore:
    {
        const UDemoRunBuildComponent* Build=Pawn->GetPlayerState<ADemoPlayerState>()->GetRunBuildComponent(); // 修订在服务器真实快照校验。
        if(Request.BuildRevision!=Build->GetSnapshot().Revision||Request.ExpectedLevel!=0||State->bEndless)
            return FDemoCommandResult::Reject(TEXT("CoreStale"),TEXT("选择页已过期，请重新打开终端"));
        return RunFlow->SelectRunCore(Pawn,Request.Choice)?FDemoCommandResult::Applied(SaveCheckpoint(),TEXT("本轮核心已选择")):FDemoCommandResult::Reject(TEXT("CoreRejected"),TEXT("请在安全区终端出发前选择核心"));
    }
    case EDemoGameplayCommand::Casino: return ShopSystem->Casino(Pawn,Request.Casino,Request.GambleSerial,Request.CasinoDeviceId,Request.CasinoDeviceToken,Request.HubWorldEpoch); // 私有结果只由服务器产生，实体设备重验专属租约/Mode。
    case EDemoGameplayCommand::Gamble: return ShopSystem->Gamble(Pawn,Request.Choice,Request.ExpectedPrice,Request.GambleSerial); // 拥有者身份及终端重新验证。
    case EDemoGameplayCommand::UpgradeStat: return ShopSystem->PurchaseUpgradeResult(Request.Choice, Pawn);
    case EDemoGameplayCommand::EquipWeapon: return ShopSystem->EquipPrimaryWeapon(DemoWeaponCatalog::IdAt(Request.Choice), Pawn);
    case EDemoGameplayCommand::EquipAmmo: return ShopSystem->EquipAmmo(Pawn, Request.Choice);
    case EDemoGameplayCommand::PurchaseAmmo:
    {
        FString Message; // 服务返回具体拒绝原因，不从日志或UI文案推断事务状态。
        return ShopSystem->PurchaseAmmo(Pawn, Request.Choice, Request.ExpectedPrice, Message) ? FDemoCommandResult::Applied(true, Message) : FDemoCommandResult::Reject(TEXT("PurchaseRejected"), Message);
    }
    case EDemoGameplayCommand::ChooseReward:
        if(RunFlow->UsesDemoCampaign()&&(State->LevelNumber==3||State->LevelNumber==7)
            && (Request.ExpectedLevel!=State->LevelNumber||Request.BuildRevision!=Pawn->GetPlayerState<ADemoPlayerState>()->GetRunBuildComponent()->GetSnapshot().Revision))
            return FDemoCommandResult::Reject(TEXT("BranchStale"),TEXT("分支选择已过期，请等待最新状态"));
        return RunFlow->ChooseReward(Request.Choice, Pawn) ? FDemoCommandResult::Applied(true, TEXT("已选择本关奖励")) : FDemoCommandResult::Reject(TEXT("RewardRejected"), TEXT("奖励已经领取或当前不可领取"));
    case EDemoGameplayCommand::SaveCheckpoint:
        return FDemoCommandResult::Applied(SaveCheckpoint(), TEXT("检查点保存请求已处理"));
    default: break;
    }
    if (!CoopRoster->IsLeader(Player)) return FDemoCommandResult::Reject(TEXT("HostOnly"), TEXT("此操作需要房主执行"));
    UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 只有房主选择队伍战局文件。
    switch (Request.Command)
    {
    case EDemoGameplayCommand::SelectDeparture:
        if(State->Phase!=EDemoPhase::Hub||!TerminalSystem->ValidateSession(Pawn,false).IsEmpty()) return FDemoCommandResult::Reject(TEXT("DepartureConfig"),TEXT("请在安全区下一关终端选择模式"));
        if(Request.ReadyRevision!=State->TeamReady.Revision||Request.ExpectedLevel!=State->LevelNumber) return FDemoCommandResult::Reject(TEXT("ReadyStale"),TEXT("队伍状态已变化，请重新选择"));
        if(!(Request.bEndless?SelectEndless():SelectDifficulty(Request.Difficulty))) return FDemoCommandResult::Reject(TEXT("ModeLocked"),TEXT("难度未解锁或配置无效"));
        return FDemoCommandResult::Applied(SaveCheckpoint(),TEXT("出发模式已设置，请全员准备"));
    case EDemoGameplayCommand::StartLevel:
        if (!TerminalSystem->ValidateSession(Pawn, false).IsEmpty()) return FDemoCommandResult::Reject(TEXT("TerminalRejected"), TEXT("请与下一关终端交互"));
        if(CoopRoster->GetControllers().Num()>1)
        {
            if(Request.ReadyRevision!=State->TeamReady.Revision||Request.ExpectedLevel!=State->LevelNumber) return FDemoCommandResult::Reject(TEXT("ReadyStale"),TEXT("队伍状态已变化，请按最新状态重新出发"));
            FString Error; // 多人启动不再隐式改难度，避免把刚确认的准备意图替换为另一模式。
            if(!TeamReadySystem->ValidateDeparture(Error)) return FDemoCommandResult::Reject(TEXT("TeamNotReady"),Error);
        }
        else if (State->Phase == EDemoPhase::Hub && !(Request.bEndless ? SelectEndless() : SelectDifficulty(Request.Difficulty))) return FDemoCommandResult::Reject(TEXT("ModeLocked"), TEXT("难度未解锁或配置无效"));
        StartNextLevel(); return State->Phase == EDemoPhase::Combat ? FDemoCommandResult::Applied(true, FString()) : FDemoCommandResult::Reject(TEXT("DepartureRejected"), (State->FailureReason.IsEmpty()?TEXT("出发失败，请确认队伍就绪及检查点保存状态"):State->FailureReason));
    case EDemoGameplayCommand::ReturnHub:
        return ReturnToSafeHub(Request.bConfirmed) ? FDemoCommandResult::Applied(true, FString()) : FDemoCommandResult::Reject(TEXT("ReturnRejected"), TEXT("返回失败或需要确认清除本轮成长"));
    case EDemoGameplayCommand::RestartRun:
        if (State->Phase != EDemoPhase::Victory && State->Phase != EDemoPhase::Defeat) return FDemoCommandResult::Reject(TEXT("Phase"), TEXT("当前不可重开"));
        RestartDemo(); return FDemoCommandResult::Applied(true, FString());
    case EDemoGameplayCommand::ReturnLobby:
        if (!SaveCheckpoint()) return FDemoCommandResult::Reject(TEXT("SaveFailed"), TEXT("检查点保存失败"));
        TravelToRun(false); return FDemoCommandResult::Applied(true, FString());
    case EDemoGameplayCommand::SelectSaveSlot:
    case EDemoGameplayCommand::CreateSave:
        if (State->Phase != EDemoPhase::Lobby || !Saves || !(Request.Command == EDemoGameplayCommand::CreateSave ? Saves->CreateSlot(Request.Choice) : Saves->SelectSlot(Request.Choice)))
            return FDemoCommandResult::Reject(TEXT("SlotRejected"), TEXT("存档无法创建或读取"));
        TravelToRun(true); return FDemoCommandResult::Applied(true, FString());
    default: return FDemoCommandResult::Reject(TEXT("UnknownCommand"), TEXT("无效操作"));
    }
}
