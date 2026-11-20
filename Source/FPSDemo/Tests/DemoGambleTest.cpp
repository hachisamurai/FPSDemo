// 职责：Editor抽取事务与存档回归。使用：-DemoGambleTest运行真实报价、提交与失败回滚；进度使用本次GI临时目录。
// 首次引导通过正式关闭动作完成；运行时用-GameUserSettingsINI=<隔离绝对路径>，避免已读标记写入开发者设备设置。
#include "Tests/DemoGambleTest.h"
#include "Characters/DemoCharacter.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIActions.h" // 首次引导走玩家实际关闭动作，不能绕过开火的菜单门禁。
#include "Economy/DemoShopComponent.h"
#include "Economy/Gambling/DemoGambleService.h"
#include "Economy/Gambling/DemoGambleSettings.h"
#include "Economy/Gambling/DemoCasinoSettings.h" // 验证新金币路线与旧V6账本兼容。
#include "GAS/Gambling/DemoGambleEffects.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/Abilities/DemoPlayerAbilityTypes.h"
#include "GAS/DemoTags.h"
#include "Interaction/DemoInteractable.h"
#include "Save/DemoRunSave.h"
#include "Save/Codecs/DemoCheckpointJsonCodec.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Debug/DemoLog.h"
#include "Dom/JsonObject.h" // Codec输出纯JSON对象的显式字段断言。
#include "Weapons/Projectiles/DemoShotContext.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "GAS/Ammo/DemoAmmoStatus.h"
#include "AI/DemoEnemy.h"
#include "Combat/DemoEnemyHitZones.h"
ADemoGambleTest::ADemoGambleTest()
{ DEMO_LOG_CALL(); PrimaryActorTick.bCanEverTick=WITH_EDITOR; PrimaryActorTick.TickInterval=.05f; }
#if WITH_EDITOR
bool ADemoGambleTest::Check(bool Condition,const TCHAR* Message)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Display,TEXT("GAMBLE_TEST %s step=%d %s"),Condition?TEXT("PASS"):TEXT("FAIL"),Step,Message);
    if(!Condition) { bFailed=true; FPlatformMisc::RequestExitWithStatus(false,1); }
    return Condition;
}
bool ADemoGambleTest::RejectSave()
{
    DEMO_LOG_CALL();
    const auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 正在保存候选时，本机菜单仍必须保持旧提交结果。
    const auto* Player=Cast<ADemoCharacter>(PC->GetPawn()); // 当前权威候选仅保存器可读。
    Check(PC->GetMenuSnapshot().Progress.Gambling.Serial<Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot().Gambling.Serial,TEXT("uncommitted random result not published through menu callback"));
    UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_TEST injected synchronous save failure")); return false;
}
FDemoCommandResult ADemoGambleTest::Place(int32 Choice,int32 Serial)
{
    DEMO_LOG_CALL();
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 此专项是明确的单人夹具。
    auto* Player=Cast<ADemoCharacter>(PC->GetPawn()); // 当前Avatar同步借用。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 生产业务网关。
    auto* Shop=Mode->FindComponentByClass<UDemoShopComponent>(); // 不替换真正商店。
    const auto Offer=DemoGambleService::Quote(*Shop,Player,Choice); // 使用生产权威报价。
    FDemoGameplayCommandRequest Request; // 通过正式终端/拥有者命令解析，不直接修改开奖结果。
    Request.Command=EDemoGameplayCommand::Gamble; Request.Choice=Choice; Request.ExpectedPrice=Offer.Cost;
    Request.GambleSerial=Serial==INDEX_NONE?Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->GetSnapshot().Gambling.Serial:Serial;
    Request.Terminal=Mode->GetShopTerminal(); Request.TerminalGeneration=Mode->BuildPlayerMenuSnapshot(PC).TerminalGeneration;
    return Mode->ExecutePlayerCommand(PC,Request);
}
#endif
void ADemoGambleTest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK(); Super::Tick(DeltaSeconds);
#if WITH_EDITOR
    if(bFailed||GetWorld()->GetTimeSeconds()<Next) return;
    if(!Check(GetWorld()->GetTimeSeconds()<35,TEXT("bounded editor-only runtime"))) return;
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 本步骤借用本机真实控制器。
    auto* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 每步重新解析，不跨World保存。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 权威组件宿主。
    auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 隔离阶段夹具，不伪造清关奖励。
    auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // WITH_EDITOR每GI临时目录。
    if(!Check(PC&&Player&&Mode&&State&&Saves,TEXT("world and player ready"))) return;
    auto* Progress=Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 真实个人账本。
    auto* Shop=Mode->FindComponentByClass<UDemoShopComponent>(); // 测试注入只替换保存委托。
    auto* ASC=Player->GetDemoASC(); // 真实ASC效果，不模拟属性数字。
    auto* Flow=PC->GetMenuFlow(); // 本地UI动作走拥有者请求。
    switch(Step)
    {
    case 0:
    {
        const auto* Settings=GetDefault<UDemoGambleSettings>(); // 命令行ini只对隔离进程覆盖概率边界，不改CDO。
        if(!Check(Settings->AttributeChance==1&&Settings->TalentChance==1,TEXT("deterministic test config loaded"))) return;
        Flow->UpdateRealtime(0.f); // 完成本地大厅的首次引导检查，避免测试与PC Tick顺序决定它在后续Hub等待期间才弹出。
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide)
        {
            UE_LOG(LogFPSDemo,Log,TEXT("GAMBLE_TEST dismiss initial guide through production OverlayBack"));
            Flow->DispatchUIAction(EDemoUIAction::OverlayBack); // 正式关闭记录隔离设备配置已读；不写MenuPage或弱化生产输入守卫。
        }
        if(!Check(!Flow->HasBlockingOverlay(),TEXT("initial guide dismissed before gameplay fixture"))) return;
        if(!Check(Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("create local temporary slot and hub"))) return;
        Saves->ConsumePending(); PC->OnRunReady();
        FDemoPlayerProgressSnapshot Data=Progress->GetSnapshot(); // 夹具币源不依赖通关，正式API不提供此赋值。
        Data.Coins=6000; Data.SilverCoins=1000; Progress->Commit(Data);
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,0));
        Mode->GetShopTerminal()->Interact(Player); Flow->OpenGambleMenu(); Flow->InspectGamble(5); Flow->RequestGamble();
        if(!Check(Flow->IsGambleMenuOpen()&&Flow->IsGambleConfirmOpen(),TEXT("real terminal page and confirmation opened"))) return;
        Flow->ConfirmGamble(false);
        if(!Check(Progress->GetSnapshot().Coins==6000&&!Flow->IsGambleConfirmOpen(),TEXT("real menu cancellation costs nothing"))) return;
        if(!CheckCasino()) return; // 独立模块验证正式命令、棋盘私密性、落盘和回滚。
        const auto* Recovery=GetDefault<UDemoCasinoSettings>(); // 原遗物价格迁至新独立配置。
        const int32 BeforeRelic=Progress->GetSnapshot().Coins; // 验证替代旧研究的直购，不依赖金币随机结果。
        if(!Check(Place(10).IsApplied()&&DemoGambleEffects::Has(ASC,TEXT("rescue"))
            &&Progress->GetSnapshot().Coins==BeforeRelic-Recovery->RelicGoldCosts[2],TEXT("direct gold relic purchase equips GAS effect and saves once"))) return;
        if(!Check(Place(7).IsApplied()&&Place(10).IsApplied()&&Progress->GetSnapshot().Coins==BeforeRelic-Recovery->RelicGoldCosts[2],TEXT("owned relic re-equip is free"))) return;
        Data=Progress->GetSnapshot(); Data.Coins=5000; // 后续旧GAS/Codec回归使用固定余额，独立于新路线调参。
        if(!Check(Progress->Commit(Data)&&Mode->SaveCheckpoint(),TEXT("save isolated follow-up fixture"))) return;
        FString Error; // 显式JSON输出，不创建云子系统或发HTTP。
        auto Json=Saves->ExportCloud(0); // 同一生产Codec用于本地纯往返。
        auto* Decoded=NewObject<UDemoRunSave>(this); // 本测试拥有的独立候选。
        if(!Check(DemoCheckpointJsonCodec::Decode(Json,*Decoded,Error)&&Decoded->Version==GetDefault<UDemoRunSave>()->Version&&Decoded->Gambling.EquippedRelic==TEXT("rescue")&&Decoded->Coins==5000,TEXT("V6 cloud codec preserves wallet and research"))) return;
        auto* Old=DuplicateObject<UDemoRunSave>(Saves->GetSlot(0),this); // 独立V5夹具不覆写活动槽。
        Old->Version=5; Old->Gambling=FDemoGambleProgress();
        Old->CampaignId=NAME_None; Old->ContentRevision=0; Old->RunSeed=0; Old->Build=FDemoRunBuildSnapshot(); // V5夹具必须去掉V8专属字段，不能只改版本号。
        Old->ContentSnapshot.Empty(); Old->ContentFingerprint.Empty(); // V9冻结内容也不存在于V5，旧档夹具不能夹带新战役合同。
        for(auto& Member:Old->TeamMembers) Member.Build=FDemoRunBuildSnapshot(); // 合作成员同样遵循旧协议边界。
        auto LegacyJson=DemoCheckpointJsonCodec::Encode(*Old,Error); // V5编码不输出新字段，保持历史hash。
        auto* Migrated=NewObject<UDemoRunSave>(this); // 迁移结果只用于断言。
        if(!Check(LegacyJson.IsValid()&&!LegacyJson->HasField(TEXT("gambling"))&&DemoCheckpointJsonCodec::Decode(LegacyJson,*Migrated,Error)&&Migrated->Version==GetDefault<UDemoRunSave>()->Version&&Migrated->Gambling.Relics.IsEmpty()&&Migrated->Coins==5000,TEXT("V5 migrates empty research without losing wallet"))) return;
        auto* LegacyRuntime=DuplicateObject<UDemoRunSave>(Saves->GetSlot(0),this); // 后续旧银币/GAS夹具使用旧战役，避免人工Intermission绕过V8核心出发契约。
        LegacyRuntime->CampaignId=NAME_None; LegacyRuntime->ContentRevision=0; LegacyRuntime->RunSeed=0; LegacyRuntime->Build=FDemoRunBuildSnapshot();
        LegacyRuntime->ContentSnapshot.Empty(); LegacyRuntime->ContentFingerprint.Empty(); // 正式旧模式要求内容快照与指纹为空；不放宽生产存档校验。
        LegacyRuntime->Phase=EDemoPhase::Intermission; LegacyRuntime->CompletedLevel=1; // 旧Hub会正式迁移新内容；旧中途检查点才继续旧战役，后续旧测试从清关节点开始。
        for(auto& Member:LegacyRuntime->TeamMembers) Member.Build=FDemoRunBuildSnapshot();
        PC->CloseUpgradeMenu(); State->Phase=EDemoPhase::Lobby;
        if(!Check(Mode->RestoreCheckpoint(*LegacyRuntime),TEXT("legacy GAS fixture restores through production checkpoint boundary"))) return;
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,0)); Mode->GetShopTerminal()->Interact(Player);
        PC->PublishMenuSnapshot(); Flow->OpenGambleMenu(); Flow->InspectGamble(5);
        FScreenshotRequest::RequestScreenshot(TEXT("Gambling-Hub.png"),false,false);
        ++Step; Next=GetWorld()->GetTimeSeconds()+.4; break;
    }
    case 1:
    {
        PC->CloseUpgradeMenu(); State->Phase=EDemoPhase::Intermission; State->LevelNumber=1; // 显式阶段夹具，仅测试节点交易边界。
        Mode->GetShopTerminal()->Interact(Player);
        const float DamageBefore=Player->GetDemoAttributes()->GetWeaponDamageBonus(); // 验证真正GAS基础值回滚。
        Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(this,&ADemoGambleTest::RejectSave));
        const auto Failure=Place(0); // 100%中奖但写盘失败，不能留下免费属性。
        Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(Mode,&AFPSDemoGameMode::SaveCheckpoint));
        if(!Check(!Failure.IsApplied()&&Progress->GetSnapshot().SilverCoins==1000&&Player->GetDemoAttributes()->GetWeaponDamageBonus()==DamageBefore,TEXT("failed save rolls back silver and successful GAS reward"))) return;
        if(!Check(Place(0).IsApplied()&&Place(1).IsApplied()&&Place(2).IsApplied(),TEXT("three same-node attribute draws commit without attempt cap"))) return; // 明确跨过原两次限制，照常逐笔扣款/保存。
        if(!Check(Progress->GetSnapshot().SilverCoins==910&&Progress->GetSnapshot().Gambling.Attempts==0&&Player->GetDemoAttributes()->GetWeaponDamageBonus()==DamageBefore+5
            &&Player->GetDemoAttributes()->GetMaxHealth()==120&&Progress->GetSnapshot().UpgradeProgress.Total(false)==0,TEXT("GAS gains saved, ordinary shop prices unaffected"))) return;
        if(!Check(!Place(6).IsApplied(),TEXT("gold recovery rejected outside hub"))) return;
        State->LevelNumber=2; // 旧V6满计数夹具用于验证移除次数限制后仍可购买和保存。
        FDemoPlayerProgressSnapshot Legacy=Progress->GetSnapshot(); // 不修改收益/钱包，仅模拟旧版合法的最高节点计数。
        Legacy.Gambling.Node=2;Legacy.Gambling.Attempts=100;Progress->Commit(Legacy);
        // 先持有完整目录，再连续抽取，保证所有中奖必为重复升级；同时跨越原三槽限制。
        for(int32 Index=0;Index<DemoGamble::TalentCount();++Index) Legacy.Gambling.UpgradeTalent(DemoGamble::Talent(Index)); // 仅测试夹具，不改正式奖池。
        if(!Check(Progress->Commit(Legacy),TEXT("all catalog talents accepted without slot capacity"))) return;
        if(!Check(Place(3).IsApplied()&&Place(3).IsApplied()&&Place(3).IsApplied()&&Place(3).IsApplied(),TEXT("four repeated talent draws commit without capacity or no-replacement limit"))) return;
        const auto& Ranked=Progress->GetSnapshot().Gambling; // 正式交易后的等级快照。
        int32 RankTotal=0; // 每次成功只加一级，四笔合计目录大小+4。
        for(int32 Rank:Ranked.TalentRanks) RankTotal+=Rank;
        if(!Check(Ranked.Talents.Num()==DemoGamble::TalentCount()&&RankTotal==DemoGamble::TalentCount()+4&&Ranked.Attempts==100&&Ranked.Validate(),TEXT("duplicate results increment exactly one level and legacy attempts remain inert"))) return;
        for(FName Id:Ranked.Talents) // 每个已拥有项都保留原生Tag，重复升级不能丢失其他天赋。
            if(!Check(DemoGambleEffects::Has(ASC,Id),TEXT("each owned talent retains its GAS tag"))) return;
        auto* Saved=Cast<UDemoRunSave>(UGameplayStatics::LoadGameFromSlot(Saves->SlotName(0),0)); // 实际磁盘往返。
        if(!Check(Saved&&Saved->Gambling.TalentRanks==Ranked.TalentRanks&&Saved->DamageBonus==DamageBefore+5&&Saved->SilverCoins==670,TEXT("disk atomically stores all ranks and four wager debits"))) return; // 1000-3*30-4*60。
        Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(this,&ADemoGambleTest::RejectSave));
        const FDemoGambleProgress BeforeRankFailure=Ranked; // 失败交易前完整等级/序号副本。
        const auto RankFailure=Place(3); // 必然中奖但写盘失败，必须回滚等级与GAS聚合。
        Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(Mode,&AFPSDemoGameMode::SaveCheckpoint));
        if(!Check(!RankFailure.IsApplied()&&Progress->GetSnapshot().Gambling.TalentRanks==BeforeRankFailure.TalentRanks
            &&Progress->GetSnapshot().Gambling.Serial==BeforeRankFailure.Serial&&Progress->GetSnapshot().SilverCoins==670,TEXT("failed save rolls back repeated rank, cost and serial"))) return;
        FDemoPlayerProgressSnapshot Expanded=Progress->GetSnapshot(); // 固定新ID往返夹具，不让随机中奖恰好只抽旧项而漏测兼容。
        Expanded.Gambling.Talents={TEXT("overdrive"),TEXT("twin"),TEXT("precision")}; Expanded.Gambling.TalentRanks.Reset(); // 模拟缺少等级的旧档，各项按1级恢复。
        if(!Check(Progress->Commit(Expanded)&&Mode->SaveCheckpoint(),TEXT("new talent IDs committed and saved"))) return;
        FString DecodeError; // V6固定ID编解码失败原因，独立于正式云网络。
        UDemoRunSave* ExpandedDecoded=NewObject<UDemoRunSave>(this); // 临时候选，不替换活动槽。
        if(!Check(DemoCheckpointJsonCodec::Decode(Saves->ExportCloud(0),*ExpandedDecoded,DecodeError)
            &&ExpandedDecoded->Gambling.Talents==Expanded.Gambling.Talents&&Player->GetDemoAttributes()->GetFireRateMultiplier()==2,TEXT("new IDs survive V6 JSON and grant current GAS rate"))) return;
        PC->PublishMenuSnapshot(); Mode->GetShopTerminal()->Interact(Player); Flow->OpenGambleMenu(); Flow->InspectGamble(3); // 人工换关号触发菜单撤销后，必须重新交互。
        if(!Check(Flow->IsGambleMenuOpen(),TEXT("intermission terminal page reopened after node transition"))) return;
        Flow->ChangeGambleRewardPage(1); Flow->ChangeGambleRewardPage(1); Flow->ChangeGambleRewardPage(1); // 权威候选池第4项为虹吸；保留完整说明供真实HUD截图。
        if(!Check(Flow->GetGambleRewardPage()==3&&!PC->GetMenuSnapshot().GambleOffers[3].Rewards[3].Description.IsEmpty(),TEXT("expanded reward pool pages expose authoritative descriptions"))) return;
        FScreenshotRequest::RequestScreenshot(TEXT("Gambling-Silver.png"),false,false);
        ++Step; Next=GetWorld()->GetTimeSeconds()+.4; break;
    }
    case 2:
    {
        PC->CloseUpgradeMenu(); State->Phase=EDemoPhase::Hub; State->LevelNumber=0; // 允许真实Hub技能，不用打开菜单绕过技能门控。
        DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),-10000);
        if(!Check(Player->GetDemoAttributes()->GetHealth()==1&&Progress->GetSnapshot().Gambling.bRescueUsed&&!Player->IsCoopEliminated(),TEXT("rescue intercepts lethal GE before death"))) return;
        DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),-10000);
        if(!Check(Player->GetDemoAttributes()->GetHealth()==1,TEXT("duration protection rejects further damage"))) return;
        Mode->GetShopTerminal()->Interact(Player);
        if(!Check(Place(7).IsApplied()&&Place(10).IsApplied()&&Progress->GetSnapshot().Gambling.bRescueUsed,TEXT("re-equip cannot refill consumed rescue"))) return;
        PC->CloseUpgradeMenu();
        FDemoPlayerProgressSnapshot Data=Progress->GetSnapshot(); // 只给测试解锁其余遗物，后续实际装配/GE路径不替换。
        Data.Gambling.Relics={TEXT("rescue"),TEXT("overload"),TEXT("catalyst"),TEXT("propulsion")}; Progress->Commit(Data);
        Mode->GetShopTerminal()->Interact(Player);
        const int32 Capacity=Player->GetWeaponComponent()->GetActiveWeapon()->GetCapacity(); // 装配前真实当前枪容量。
        if(!Check(Place(8).IsApplied()&&Player->GetWeaponComponent()->GetActiveWeapon()->GetCapacity()==FMath::FloorToInt(Capacity*1.2f),TEXT("overload increases real weapon capacity"))) return;
        Player->GetWeaponComponent()->GetActiveWeapon()->Refill();
        if(!Check(Place(11).IsApplied()&&Player->GetWeaponComponent()->GetActiveWeapon()->GetAmmo()<=Capacity,TEXT("removing overload reconciles excess ammunition"))) return;
        PC->CloseUpgradeMenu();
        if(!Check(ASC->ActivateDemoAbility(UDemoDashAbility::StaticClass())&&FMath::IsNearlyEqual(ASC->GetCooldownRemaining(DemoTags::DashCooldown),3.4f,.02f),TEXT("propulsion modifies actual dash cooldown GE"))) return;
        ++Step; Next=GetWorld()->GetTimeSeconds()+1.2; break;
    }
    case 3:
    {
        // 通过真实出发创建CombatSession，测试已经飞出的子弹冻结遗物/天赋数值。
        DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),1000);
        Mode->StartNextLevel();
        if(!Check(State->Phase==EDemoPhase::Combat,TEXT("production combat session started"))) return;
        FDemoPlayerProgressSnapshot ShotData=Progress->GetSnapshot(); // 本轮三种元素天赋夹具，已装备遗物来源合法。
        ShotData.Gambling.Talents={TEXT("ember"),TEXT("hunter"),TEXT("pierce")}; ShotData.Gambling.EquippedRelic=TEXT("catalyst"); Progress->Commit(ShotData);
        auto* Weapon=Player->GetWeaponComponent()->GetActiveWeapon(); // 生产武器资源/配置。
        const auto Ammo=FDemoAmmoEffectSnapshot::FromCatalog(Player->FindComponentByClass<UDemoAmmoComponent>()->GetCatalog()); // 原配置值。
        auto* Shot=NewObject<UDemoShotContext>(this); // 测试Actor保活上下文，未创建假子弹命中。
        // 快照冻结前先验证真实开火许可；引导/菜单残留不能被误报为元素天赋数值错误，也不能在这里自动关闭未知页面。
        if(!Check(!Flow->HasBlockingOverlay()&&Player->CanUseFirearmActions(),TEXT("combat shot fixture has no blocking overlay and permits firearm actions"))) return;
        if(!Check(Shot->Initialize(Player,Weapon->GetWeaponId(),Weapon->Config,Weapon->EnemyHitProfile?Weapon->EnemyHitProfile.Get():GetDefault<UDemoEnemyHitProfile>(),1,Ammo,25)
            &&FMath::IsNearlyEqual(Shot->GetAmmoSnapshot().ExplosionDamage,Ammo.ExplosionDamage*1.08f,.01f)
            &&Shot->GetAmmoSnapshot().BurnDuration==Ammo.BurnDuration+1
            &&FMath::IsNearlyEqual(Shot->GetAmmoSnapshot().SecondaryDamageRatio,.6f)
            &&FMath::IsNearlyEqual(Shot->GetFrozenDamageMultiplier(),1.15f),TEXT("shot freezes catalyst and all three elemental talent modifiers"))) return;
        ShotData.Gambling.Talents.Reset(); ShotData.Gambling.TalentRanks.Reset(); // 清理完整临时等级账本。
         ShotData.Gambling.EquippedRelic=TEXT("propulsion"); Progress->Commit(ShotData); // 后续改变来源不能篡改旧弹。
        if(!Check(FMath::IsNearlyEqual(Shot->GetFrozenDamageMultiplier(),1.15f)&&Shot->GetAmmoSnapshot().BurnDuration==Ammo.BurnDuration+1,TEXT("existing projectile retains modifiers after source changes"))) return;
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; // 独立高空靶不注册刷怪奖励。
        auto* Target=GetWorld()->SpawnActor<ADemoEnemy>(ADemoEnemy::StaticClass(),FVector(80000,80000,10000),FRotator::ZeroRotator,Spawn); // World管理测试靶。
        FDemoEnemySpawnStats Stats; Stats.Health=1000; Stats.AttackPower=0; // 足够容纳一次爆炸，无自然死亡干扰。
        Target->Configure(Stats); Target->SetActorTickEnabled(false);
        const float Before=Target->GetHealth(); // 不推进World，周期DOT不掺入爆炸。
        for(int32 Stack=0;Stack<Ammo.BurnThreshold;++Stack) Target->FindComponentByClass<UDemoAmmoStatus>()->ApplySnapshot(ASC,1,Shot->GetAmmoSnapshot()); // 真实GAS叠层/消费阈值。
        if(!Check(FMath::IsNearlyEqual(Target->GetHealth(),Before-Ammo.ExplosionDamage*1.08f,.01f),TEXT("enhanced fire snapshot applies actual GAS explosion damage"))) return;
        Target->Destroy();
        // 保护真实到期后再次受致死伤害；团灭生产流程必须清理临时成长并写回永久研究。
        DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),-10000);
        const auto& Data=Progress->GetSnapshot(); // 死亡同步流程已经完成，检查真实个人账本。
        const auto* Saved=Saves->GetSlot(0); // 死亡自动重置检查点。
        if(!Check(State->Phase==EDemoPhase::Defeat&&Data.SilverCoins==0&&Data.Gambling.Talents.IsEmpty()&&Data.Gambling.Relics.Num()==4
            &&Saved&&Saved->Gambling.Relics.Num()==4&&Saved->Gambling.EquippedRelic==TEXT("propulsion")&&Saved->Coins==5000&&Saved->DamageBonus==0,TEXT("real defeat clears temporary gains and preserves permanent research"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_GAMBLE_TEST_SUCCESS")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
    }
#endif
}
