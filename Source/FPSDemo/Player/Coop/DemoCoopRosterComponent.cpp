// 职责：固定队伍身份和个人检查点事务。使用：房主登记/重连时恢复本人钱包、装备、核心；依赖PS组件，不跨账号合并数据。
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerProfile.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "GAS/DemoAttributeSet.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Debug/DemoLog.h"
#include "Progression/Builds/DemoRunBuildComponent.h" // 核心与个人检查点同事务，离线成员清轮次时一并重置。
namespace
{
    /** Data 为值快照；Out 是本次转换用 V5 适配器，运行时组件不保留它。 */
    void ToLegacy(const FDemoCoopPlayerCheckpoint& Data, UDemoRunSave& Out)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
        Out.CreatedLocal = FDateTime::Now(); Out.RunId = Data.PlayerKey;
        Out.Version=9; // 个人数值适配器没有根槽身份；拥有权在ValidatePlayer独立验证，避免递归根镜像校验。
        Out.Gambling=Data.Progress.Gambling; // 个人检查点映射不可遗漏遗物。
        Out.Coins = Data.Progress.Coins; Out.SilverCoins = Data.Progress.SilverCoins; Out.Kills = Data.Progress.Kills;
        Out.UpgradeProgress = Data.Progress.UpgradeProgress; Out.GoldPurchases = Out.UpgradeProgress.Total(true);
        Out.SilverPurchases = Out.UpgradeProgress.Total(false); Out.Purchases = Out.GoldPurchases + Out.SilverPurchases;
        Out.Health = Data.Health; Out.MaxHealth = Data.MaxHealth; Out.DamageBonus = Data.DamageBonus;
        Out.MagazineBonus = Data.MagazineBonus; Out.HealAmount = Data.HealAmount; Out.DashSpeed = Data.DashSpeed;
        Out.PrimaryId = Data.Loadout.PrimaryId; Out.ActiveSlot = Data.Loadout.ActiveSlot;
        Out.Weapons.Reset(); // 同一适配器在回滚时复用，不能追加第二份重复库存。
        Out.UnlockedAmmoIds = Data.Loadout.UnlockedAmmoIds; Out.SelectedAmmoId = Data.Loadout.SelectedAmmoId;
        for (const FDemoWeaponSnapshot& Weapon : Data.Loadout.Weapons) // 领域/旧磁盘格式边界只显式复制已知字段。
        { FDemoSavedWeapon Saved; /* 旧持久格式临时行，Actor 不跨边界。 */ Saved.Id = Weapon.Id; Saved.Ammo = Weapon.Ammo; Saved.Reserve = Weapon.Reserve; Out.Weapons.Add(Saved); }
    }
}
UDemoCoopRosterComponent::UDemoCoopRosterComponent() { DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick = false; }
bool UDemoCoopRosterComponent::IsLeader(const ADemoPlayerController* Player) const
{ DEMO_LOG_TICK(); return Player && Player->HasAuthority() && Player->IsLocalController(); }
FString UDemoCoopRosterComponent::GetPlayerKey(const ADemoCharacter* Player) const
{
    DEMO_LOG_TICK();
    const FString* Key = Player ? Connections.Find(Cast<ADemoPlayerController>(Player->GetController())) : nullptr; // Pawn 替换后仍由当前控制器映射。
    return Key ? *Key : FString();
}
TArray<ADemoPlayerController*> UDemoCoopRosterComponent::GetControllers() const
{
    DEMO_LOG_TICK();
    TArray<ADemoPlayerController*> Players; // 在线身份，缺Pawn仍保留用于阻塞出发。
    for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if(ADemoPlayerController* PC=Cast<ADemoPlayerController>(It->Get()); PC&&(PC->IsLocalController()||Connections.Contains(PC))) Players.Add(PC);
    Players.Sort([](const ADemoPlayerController& A,const ADemoPlayerController& B) // 捕获为空，同步稳定显示排序，不跨帧保留对象。
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ReadyRosterSort")); return A.GetUniqueID()<B.GetUniqueID(); });
    return Players;
}
TArray<ADemoCharacter*> UDemoCoopRosterComponent::GetPlayers(bool bAliveOnly) const
{
    DEMO_LOG_TICK();
    TArray<ADemoCharacter*> Players; // 返回本调用期借用列表，所有业务仍验证权威与阶段。
    for (TActorIterator<ADemoCharacter> It(GetWorld()); It; ++It) // 注册在握手前可能晚到；只接受有效 Controller 与 ASC Avatar。
        if (ADemoPlayerController* PC = Cast<ADemoPlayerController>(It->GetController()); PC && (PC->IsLocalController() || Connections.Contains(PC))
            && It->GetDemoAttributes() && (!bAliveOnly || (!It->IsCoopEliminated() && It->GetDemoAttributes()->GetHealth() > 0.f))) Players.Add(*It); // 未登记远端不能参战/领奖。
    return Players;
}
bool UDemoCoopRosterComponent::ValidatePlayer(const FDemoCoopPlayerCheckpoint& Data)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    FGuid Parsed; // 稳定键必须是有效GUID，不能使用路径或空字符串碰撞其他成员。
    if (!FGuid::Parse(Data.PlayerKey, Parsed) || !Parsed.IsValid() || Data.Progress.LastRewardLevel < 0 || Data.Progress.Revision < 0
        || !FMath::IsFinite(Data.Loadout.MagazineBonus) || Data.Loadout.MagazineBonus != Data.MagazineBonus || !Data.Build.Validate()) return false;
    UDemoRunSave* Legacy = NewObject<UDemoRunSave>(); // 利用旧格式数值约束，不写盘、不递归 TeamMembers。
    ToLegacy(Data, *Legacy);
    if (!Legacy->Validate() || Data.Progress.PistolChallenge < 0 || Data.Progress.PistolChallenge > 2 || Data.Progress.BestEndlessLevel < 0
        || Data.Progress.UnlockedWeaponIds.Num() > DemoWeaponCatalog::Num() || !Data.Progress.UnlockedWeaponIds.Contains(TEXT("pistol"))) return false;
    TSet<FName> WeaponIds; // 去重后才可持久化，数组重复不能产生不同的协议哈希。
    if (!Data.Progress.Ownership.LegacyMigrationId.IsEmpty())
    {
        if (!Data.Progress.Ownership.Validate(true) || Data.Progress.Ownership.EffectiveIds()!=Data.Progress.UnlockedWeaponIds) return false;
        for (const FDemoWeaponOwnershipEntry& Entry:Data.Progress.Ownership.Entries)
        { bool bHeld=false; for (const FDemoWeaponSnapshot& Weapon:Data.Loadout.Weapons) if (Weapon.Id==Entry.WeaponId) bHeld=true; if (Entry.bInitialSupplyGranted!=bHeld) return false; }
    }
    for (const FName WeaponId : Data.Progress.UnlockedWeaponIds) // 固定目录验证，拒绝虚构权限。
    { if (DemoWeaponCatalog::IndexOf(WeaponId) == INDEX_NONE || WeaponIds.Contains(WeaponId)) return false; WeaponIds.Add(WeaponId); }
    for (const FDemoWeaponSnapshot& Weapon : Data.Loadout.Weapons) // 拥有库存必须有对应的个人权限。
        if (!Data.Progress.UnlockedWeaponIds.Contains(Weapon.Id)) return false;
    return true;
}
void UDemoCoopRosterComponent::ResetTemporaryMembers()
{
    DEMO_LOG_CALL();
    if (!GetOwner()->HasAuthority()) return;
    for (FDemoCoopPlayerCheckpoint& Member : SavedMembers) // 防止战斗中掉线的成员在通关后带着旧临时成长重连。
    {
        Member.Build = FDemoRunBuildSnapshot(); // 离线成员的核心/分支同属本轮，不影响永久解锁。
        Member.Progress.SilverCoins = 0; Member.Progress.PistolChallenge = 0; Member.Progress.LastRewardLevel = 0;
        Member.Progress.Gambling.ResetRun(); // 包含离线队员，避免重连恢复旧轮天赋。
        Member.Progress.UpgradeProgress.ResetTemporary();
        Member.MaxHealth = 100.f + Member.Progress.UpgradeProgress.PermanentHealth; Member.Health = Member.MaxHealth;
        Member.DamageBonus = Member.Progress.UpgradeProgress.PermanentDamage; Member.MagazineBonus = Member.Progress.UpgradeProgress.PermanentMagazine;
        Member.Loadout.MagazineBonus = Member.MagazineBonus; Member.HealAmount = 35.f; Member.DashSpeed = 1300.f;
    }
}
void UDemoCoopRosterComponent::ResetBuilds()
{
    DEMO_LOG_CALL();
    if (!GetOwner()->HasAuthority() || bRestoring)
    { UE_LOG(LogFPSDemo, Log, TEXT("COOP_BUILDS_RESET rejected authority/restore")); return; }
    TGuardValue<bool> Guard(bRestoring, true); // ResetRun的同步表现回调不能捕获半队核心或重入恢复。
    for (FDemoCoopPlayerCheckpoint& Member : SavedMembers) Member.Build = FDemoRunBuildSnapshot(); // 缓存包含离线队员，先清以免重连带回旧核心。
    for (ADemoCharacter* Player : GetPlayers()) // 借用本World有效Avatar，真实选择仍由本人PS组件重置。
    {
        ADemoPlayerState* PS = Player->GetPlayerState<ADemoPlayerState>(); // 只用此玩家身份，不查询玩家0。
        UDemoRunBuildComponent* Build = PS ? PS->FindComponentByClass<UDemoRunBuildComponent>() : nullptr; // PS拥有，重置不销毁组件或刷新基础技能冷却。
        if (Build) Build->ResetRun();
        else UE_LOG(LogFPSDemo, Warning, TEXT("COOP_BUILDS_RESET missing component player=%s"), *GetNameSafe(Player));
    }
}
bool UDemoCoopRosterComponent::CapturePlayer(const ADemoCharacter* Player, const FString& Key, FDemoCoopPlayerCheckpoint& Out)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    const ADemoPlayerState* PS = Player ? Player->GetPlayerState<ADemoPlayerState>() : nullptr; // PS 是经济唯一源。
    const UDemoAttributeSet* Attributes = Player ? Player->GetDemoAttributes() : nullptr; // 排除短期 GE 的基础值由属性系统接口提供。
    if (!PS || !Attributes || !Player->GetWeaponComponent()) return false;
    const UDemoRunBuildComponent* Build = PS->FindComponentByClass<UDemoRunBuildComponent>(); // PS持有，仅在此捕获栈借用。
    if (!Build) { UE_LOG(LogFPSDemo, Warning, TEXT("COOP_PLAYER_CAPTURE missing build")); return false; }
    Out.Build = Build->GetSnapshot();
    Out.PlayerKey = Key; Out.Progress = PS->GetProgressComponent()->GetSnapshot();
    Out.MaxHealth = Attributes->GetMaxHealth(); Out.Health = FMath::Clamp(Attributes->GetHealth(), 1.f, Out.MaxHealth);
    Out.DamageBonus = Attributes->GetWeaponDamageBonus(); Out.MagazineBonus = Attributes->GetMagazineBonus();
    Out.HealAmount = Player->GetHealAmount(); Out.DashSpeed = Player->GetDashSpeed();
    Out.Loadout.MagazineBonus = Out.MagazineBonus; Player->GetWeaponComponent()->CaptureSnapshot(Out.Loadout);
    return ValidatePlayer(Out);
}
bool UDemoCoopRosterComponent::ApplyPlayer(ADemoCharacter* Player, const FDemoCoopPlayerCheckpoint& Data)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    if (!Player || !Player->HasAuthority() || !ValidatePlayer(Data) || !Player->GetDemoAttributes() || !Player->GetWeaponComponent()) return false;
    ADemoPlayerState* PS=Player->GetPlayerState<ADemoPlayerState>(); // 本人个人进度唯一源。
    UDemoRunBuildComponent* Build = PS ? PS->FindComponentByClass<UDemoRunBuildComponent>() : nullptr; // 本人核心，恢复失败必须和钱包一起回滚。
    FDemoCoopPlayerCheckpoint Before; // 在线重连的个人事务也先捕获，再准备资源。
    if (!PS || !Build || !CapturePlayer(Player,Data.PlayerKey,Before) || !Player->GetWeaponComponent()->PrepareSnapshot(Data.Loadout,Data.Progress.UnlockedWeaponIds)) return false;
    UDemoRunSave* Values=NewObject<UDemoRunSave>(Player); // 角色旧接口适配器，不写盘。
    ToLegacy(Data,*Values);
    bool bSuccess=PS->GetProgressComponent()->Commit(Data.Progress); // 权限与属性只在资源全部准备成功后应用。
    if (bSuccess) { Player->RestoreRunProgress(*Values); bSuccess=Player->GetWeaponComponent()->ApplyPreparedSnapshot(); }
    if (bSuccess) bSuccess = Build->RestoreSnapshot(Data.Build);
    if (!bSuccess)
    {
        ToLegacy(Before,*Values); PS->GetProgressComponent()->Commit(Before.Progress); Player->RestoreRunProgress(*Values);
        if (!Build->RestoreSnapshot(Before.Build)) UE_LOG(LogFPSDemo, Error, TEXT("COOP_PLAYER_RESTORE build rollback failed"));
        Player->GetWeaponComponent()->FinishSnapshot(false); UE_LOG(LogFPSDemo,Warning,TEXT("COOP_PLAYER_RESTORE rolled back")); return false;
    }
    return Player->GetWeaponComponent()->FinishSnapshot(true);
}
bool UDemoCoopRosterComponent::CanAdmit(const FString& Key) const
{
    DEMO_LOG_CALL();
    FGuid Parsed; // 限定规范GUID，不允许空键占用或大小写变体绕过重复判断。
    if (!FGuid::Parse(Key, Parsed) || !Parsed.IsValid() || Parsed.ToString(EGuidFormats::DigitsWithHyphens) != Key) return false;
    for (const auto& Connection : Connections) // 在线身份不可被新连接顶替。
        if (Connection.Key.IsValid() && Connection.Value == Key) return false;
    for (const FDemoCoopPlayerCheckpoint& Member : SavedMembers) // 满固定队伍仍允许离线成员重连。
        if (Member.PlayerKey == Key) return true;
    return SavedMembers.Num() < 4;
}
bool UDemoCoopRosterComponent::RegisterPlayer(ADemoPlayerController* Player, const FString& Key)
{
    DEMO_LOG_CALL();
    FGuid Parsed; // GUID 校验不能被任意空键或路径字符串绕过。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 首次服务器启动允许本地房主在 Lobby 登记。
    ADemoCharacter* Pawn = Player ? Cast<ADemoCharacter>(Player->GetPawn()) : nullptr; // 必须是此 RPC 拥有者当前 Avatar。
    if (bRestoring || !GetOwner()->HasAuthority() || !Player || Player->GetWorld() != GetWorld() || !Pawn || !Pawn->GetDemoAttributes() || !FGuid::Parse(Key, Parsed) || !Parsed.IsValid() || !State) return false;
    if (const FString* Existing = Connections.Find(Player)) return *Existing == Key; // 已登记控制器不得中途切换身份。
    if (State->Phase != EDemoPhase::Hub && !(IsLeader(Player) && State->Phase == EDemoPhase::Lobby)) return false; // 阶段限制只针对新连接，已有拥有者迟到握手保持幂等。
    for (const auto& Connection : Connections) // 不允许占用在线队友的关联键。
        if (Connection.Key.IsValid() && Connection.Value == Key) return false;
    FDemoCoopPlayerCheckpoint* Saved = nullptr; // 仅同步查找，数组变动前不跨调用保存裸指针。
    for (FDemoCoopPlayerCheckpoint& Member : SavedMembers) if (Member.PlayerKey == Key) { Saved = &Member; break; }
    if (!Saved && SavedMembers.Num() >= 4) { UE_LOG(LogFPSDemo, Warning, TEXT("COOP roster full")); return false; }
    if (Saved && !ApplyPlayer(Pawn, *Saved)) return false;
    Connections.Add(Player, Key);
    if (!Saved)
    {
        // 仅房主可以从自己的本机 Profile 导入永久权限；远端加入从该房主固定队伍的默认角色起步。
        if (IsLeader(Player))
        {
            UDemoPlayerProgressComponent* Progress = Pawn->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 本人 PS。
            FDemoPlayerProgressSnapshot Next = Progress->GetSnapshot(); // 保留已恢复的旧单人钱包。
            const UDemoPlayerProfile* Profile = GetWorld()->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 不用于其他玩家。
            if (Profile)
            {
                if (Next.Ownership.LegacyMigrationId.IsEmpty() && !Profile->MakeLegacyOwnership(Next.Ownership)) { Connections.Remove(Player); return false; }
                Next.UnlockedWeaponIds=Next.Ownership.EffectiveIds(); // 只导入一次冻结旧权，已恢复Paid不与账号反复求并集。
                Next.bHellUnlocked = Profile->IsHellUnlocked(); Next.bEndlessUnlocked = Profile->IsEndlessUnlocked();
            }
            Progress->Commit(Next);
        }
        else
        {
            UDemoPlayerProgressComponent* Progress=Pawn->GetPlayerState<ADemoPlayerState>()->GetProgressComponent(); // 新远端从此栏手枪起步，不导入房主账号。
            FDemoPlayerProgressSnapshot Next=Progress->GetSnapshot();
            if (Next.Ownership.LegacyMigrationId.IsEmpty() && !Next.Ownership.FreezeLegacy({FName(TEXT("pistol"))},FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens),0)) { Connections.Remove(Player); return false; }
            Next.UnlockedWeaponIds=Next.Ownership.EffectiveIds(); if (!Progress->Commit(Next)) { Connections.Remove(Player); return false; }
        }
        FDemoCoopPlayerCheckpoint NewMember; // 默认成员作为此房主栏位的一部分持久保存。
        if (!CapturePlayer(Pawn, Key, NewMember)) { Connections.Remove(Player); return false; }
        SavedMembers.Add(NewMember);
    }
    UE_LOG(LogFPSDemo, Display, TEXT("COOP_MEMBER joined count=%d"), Connections.Num()); return true;
}
void UDemoCoopRosterComponent::UnregisterPlayer(ADemoPlayerController* Player)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 只在安全区刷新离线快照，战斗离线保留上次检查点。
    if (State && State->Phase == EDemoPhase::Hub) { TArray<FDemoCoopPlayerCheckpoint> Snapshot; /* 同步安全值临时结果。 */ Capture(Snapshot); }
    Connections.Remove(Player);
}
bool UDemoCoopRosterComponent::Capture(TArray<FDemoCoopPlayerCheckpoint>& Out, bool bPublishCache)
{
    DEMO_LOG_CALL();
    if (bRestoring) { UE_LOG(LogFPSDemo,Warning,TEXT("COOP_CAPTURE blocked during restore")); return false; }
    TArray<FDemoCoopPlayerCheckpoint> Next = SavedMembers; // 包含离线成员的完整候选；磁盘保存前不能污染重连缓存。
    for (const auto& Connection : Connections) // 玩家断开后只保留上一份可恢复值。
    {
        if (!Connection.Key.IsValid()) continue;
        FDemoCoopPlayerCheckpoint Value; // 在线成员当前服务器值。
        if (!CapturePlayer(Cast<ADemoCharacter>(Connection.Key->GetPawn()), Connection.Value, Value)) return false;
        int32 Index = INDEX_NONE; // 按稳定键匹配，不能按 PlayerArray 的可变顺序。
        for (int32 I = 0; I < Next.Num(); ++I) if (Next[I].PlayerKey == Connection.Value) { Index = I; break; }
        if (Index == INDEX_NONE) Next.Add(Value); else Next[Index] = Value;
    }
    // 协议固定第0项为拥有此存档的房主，便于账号换设备后重新绑定本机LAN键；队友仍按自己的固定键匹配。
    for (const auto& Connection : Connections)
        if (Connection.Key.IsValid() && IsLeader(Connection.Key.Get()))
            for (int32 Index = 0; Index < Next.Num(); ++Index)
                if (Next[Index].PlayerKey == Connection.Value && Index != 0) { Next.Swap(0, Index); break; }
    if (Next.Num() > 4) { UE_LOG(LogFPSDemo, Warning, TEXT("COOP_CAPTURE rejected: roster size")); return false; }
    if (bPublishCache) SavedMembers = Next; // Hub注销仍可即时冻结；整槽事务传false并在保存成功后单独发布。
    Out = MoveTemp(Next); return true;
}
void UDemoCoopRosterComponent::PublishSavedMembers(const TArray<FDemoCoopPlayerCheckpoint>& Members)
{
    DEMO_LOG_CALL();
    if (!GetOwner() || !GetOwner()->HasAuthority() || bRestoring)
    { UE_LOG(LogFPSDemo, Error, TEXT("COOP_CACHE_PUBLISH rejected authority/restore after save; committed disk must not be rolled back")); return; }
    SavedMembers=Members; // 只复制调用者同次已提交值，不重新捕获尚未发布/可能回滚的Progress或候选库存。
    UE_LOG(LogFPSDemo, Log, TEXT("COOP_CACHE_PUBLISH members=%d"), SavedMembers.Num()); // 磁盘/无槽内存提交由调用者区分，不能仅凭此日志宣称耐久。
}
bool UDemoCoopRosterComponent::Restore(const TArray<FDemoCoopPlayerCheckpoint>& Members)
{
    DEMO_LOG_CALL();
    if (bRestoring || !GetOwner()->HasAuthority() || Members.Num()>4) return false;
    if (Members.IsEmpty()) return true; // 旧单人档不覆盖已经登记的本机成员缓存。
    TGuardValue<bool> Guard(bRestoring,true); // 整个同步准备/提交/回滚阶段防止回调重入。
    TArray<FDemoCoopPlayerCheckpoint> Next=Members; // 重绑定在副本上进行，失败不污染原离线缓存。
    TSet<FString> Keys; // 校验全部成员含离线项，不能只检查当前在线成员。
    for (const FDemoCoopPlayerCheckpoint& Member : Next)
    { if (!ValidatePlayer(Member) || Keys.Contains(Member.PlayerKey)) return false; Keys.Add(Member.PlayerKey); }
    for (const auto& Connection : Connections) // 第0项属于本机存档房主，跨设备仅允许重绑定这一项。
        if (Connection.Key.IsValid() && IsLeader(Connection.Key.Get()))
        {
            for (int32 Index=1; Index<Next.Num(); ++Index)
                if (Next[Index].PlayerKey==Connection.Value) { UE_LOG(LogFPSDemo,Warning,TEXT("COOP_RESTORE host key collision")); return false; }
            Next[0].PlayerKey=Connection.Value;
        }
    struct FPreparedMember // 同步调用栈数据，玩家由World持有，装备由每个组件事务强持有。
    {
        ADemoCharacter* Player = nullptr; // 本次已登记Avatar，不跨帧保存。
        FDemoCoopPlayerCheckpoint Before; // 精确回滚个人经济、权限与属性。
        FDemoCoopPlayerCheckpoint Target; // 目标成员副本，不借用可能重排的数组。
    };
    TArray<FPreparedMember> Prepared; // 最多4项，准备失败时撤销所有已经生成的资源。
    bool bSuccess=true; // 任一阶段失败均进入同一回滚出口。
    for (const auto& Connection : Connections)
    {
        if (!Connection.Key.IsValid()) continue;
        const FDemoCoopPlayerCheckpoint* Target=nullptr; // 必须匹配稳定身份，不能让不属于存档的在线玩家继承旧值。
        for (const FDemoCoopPlayerCheckpoint& Member : Next) if (Member.PlayerKey==Connection.Value) { Target=&Member; break; }
        FPreparedMember Entry; // 只有完整准备成功后加入回滚列表。
        Entry.Player=Cast<ADemoCharacter>(Connection.Key->GetPawn());
        if (!Target || !CapturePlayer(Entry.Player,Connection.Value,Entry.Before)) { bSuccess=false; break; }
        Entry.Target=*Target;
        if (!Entry.Player->GetWeaponComponent()->PrepareSnapshot(Entry.Target.Loadout,Entry.Target.Progress.UnlockedWeaponIds)) { bSuccess=false; break; }
        Prepared.Add(MoveTemp(Entry));
    }
    if (bSuccess)
        for (FPreparedMember& Entry : Prepared) // 无加载、无新Actor分配；所有成员都通过准备阶段后才进入写入。
        {
            UDemoRunSave* Values=NewObject<UDemoRunSave>(this); // 兼容角色初始化接口的瞬时值对象。
            ToLegacy(Entry.Target,*Values);
            if (!Entry.Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->Commit(Entry.Target.Progress)) { bSuccess=false; break; }
            Entry.Player->RestoreRunProgress(*Values);
            if (!Entry.Player->GetWeaponComponent()->ApplyPreparedSnapshot()) { bSuccess=false; break; }
            UDemoRunBuildComponent* Build = Entry.Player->GetPlayerState<ADemoPlayerState>()->FindComponentByClass<UDemoRunBuildComponent>(); // 此成员PS同步借用，Capture已验证存在。
            if (!Build || !Build->RestoreSnapshot(Entry.Target.Build)) { bSuccess = false; break; }
        }
    if (!bSuccess)
    {
        for (FPreparedMember& Entry : Prepared) // 旧装备实例尚未销毁，无需重新Spawn旧资源就能恢复。
        {
            UDemoRunSave* Values=NewObject<UDemoRunSave>(this); // 此成员原基础值。
            ToLegacy(Entry.Before,*Values);
            Entry.Player->GetPlayerState<ADemoPlayerState>()->GetProgressComponent()->Commit(Entry.Before.Progress);
            Entry.Player->RestoreRunProgress(*Values);
            UDemoRunBuildComponent* Build = Entry.Player->GetPlayerState<ADemoPlayerState>()->FindComponentByClass<UDemoRunBuildComponent>(); // 回滚原成员选择，不能遗留半队新核心。
            if (!Build || !Build->RestoreSnapshot(Entry.Before.Build)) UE_LOG(LogFPSDemo, Error, TEXT("COOP_RESTORE build rollback failed"));
            if (!Entry.Player->GetWeaponComponent()->FinishSnapshot(false)) UE_LOG(LogFPSDemo,Error,TEXT("COOP_RESTORE rollback presentation failed player=%s"),*GetNameSafe(Entry.Player));
        }
        UE_LOG(LogFPSDemo,Warning,TEXT("COOP_RESTORE transaction aborted; cached checkpoint preserved")); return false;
    }
    for (FPreparedMember& Entry : Prepared) Entry.Player->GetWeaponComponent()->FinishSnapshot(true); // 确认后才销毁旧库存/开启新Actor复制。
    SavedMembers=MoveTemp(Next);
    UE_LOG(LogFPSDemo,Display,TEXT("COOP_RESTORE committed online=%d fixed=%d"),Prepared.Num(),SavedMembers.Num()); return true;
}
