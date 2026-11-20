// 职责：捕获及事务恢复单人/房主检查点。使用：属性、装备、钱包和核心一起提交/回滚；依赖PS领域组件，不负责战役选择。
#include "Save/DemoCheckpointAssembler.h"
#include "Save/DemoRunSave.h"
#include "Game/DemoGameState.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Debug/DemoLog.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Engine/GameInstance.h"
#include "Player/Coop/DemoCoopSessionSubsystem.h"
#include "Progression/Builds/DemoRunBuildComponent.h" // 核心状态由本人PS独占，恢复不重建第二个ASC。
bool DemoCheckpointAssembler::Capture(const FString& RunId, const ADemoGameState& State, const ADemoCharacter& Player, UDemoRunSave& Out)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    const UDemoAttributeSet* Attributes = Player.GetDemoAttributes(); // 借用当前ASC属性，不保存瞬时GE。
    if (!Attributes || !Player.GetWeaponComponent()) { UE_LOG(LogFPSDemo, Warning, TEXT("CHECKPOINT_CAPTURE missing Avatar state")); return false; }
    Out.CreatedLocal = FDateTime::Now(); // Store替换为原槽创建时间，先提供合法值用于校验。
    Out.RunId = RunId;
    Out.bEndless = State.bEndless; Out.BestEndlessLevel = State.BestEndlessLevel; Out.PistolChallenge = State.PistolChallenge; // V5资格与当前关卡原子保存。
    Out.Phase = State.Phase; Out.Difficulty = State.Difficulty; Out.CompletedLevel = State.LevelNumber;
    const ADemoPlayerState* PS = Player.GetPlayerState<ADemoPlayerState>(); // 只采集此玩家个人经济，不用共享 GS 旧兼容字段。
    if (!PS) return false;
    const UDemoRunBuildComponent* Build = PS->FindComponentByClass<UDemoRunBuildComponent>(); // 同步只读本人核心，组件不存在不能保存空值冒充成功。
    if (!Build || !Build->GetSnapshot().Validate()) { UE_LOG(LogFPSDemo, Warning, TEXT("CHECKPOINT_CAPTURE missing/invalid build")); return false; }
    Out.Build = Build->GetSnapshot();
    const FDemoPlayerProgressSnapshot& Progress = PS->GetProgressComponent()->GetSnapshot(); // 同步只读当前个人权威账本。
    Out.Ownership=Progress.Ownership; // 根与成员同一权利源；Store不再从Profile覆盖付费权。
    const UDemoRunSaves* Saves=Player.GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 同步借用当前活动槽持久身份。
    const UDemoRunSave* Existing=Saves?Saves->GetSlot(Saves->GetActiveSlot()):nullptr;
    Out.SlotStorageKey=Existing?Existing->SlotStorageKey:FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
    Out.OwnerPlayerKey=Existing?Existing->OwnerPlayerKey:Player.GetGameInstance()->GetSubsystem<UDemoCoopSessionSubsystem>()->GetLocalPlayerKey();
    Out.Gambling=Progress.Gambling; // V6与钱包同一原子检查点。
    Out.Coins = Progress.Coins; Out.Kills = Progress.Kills; Out.SilverCoins = Progress.SilverCoins;
    Out.UpgradeProgress = Progress.UpgradeProgress; Out.GoldPurchases = Progress.UpgradeProgress.Total(true); Out.SilverPurchases = Progress.UpgradeProgress.Total(false);
    Out.Purchases = Out.GoldPurchases + Out.SilverPurchases; Out.PistolChallenge = Progress.PistolChallenge; Out.BestEndlessLevel = Progress.BestEndlessLevel;
    Out.Health = Attributes->GetHealth(); Out.MaxHealth = Attributes->GetMaxHealth();
    Out.DamageBonus = Attributes->GetWeaponDamageBonus(); Out.MagazineBonus = Attributes->GetMagazineBonus();
    Out.HealAmount = Player.GetHealAmount(); Out.DashSpeed = Player.GetDashSpeed();
    Player.GetWeaponComponent()->CaptureLoadout(Out);
    return true;
}
bool DemoCheckpointAssembler::Apply(const UDemoRunSave& Data, ADemoGameState& State, ADemoCharacter& Player, FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    if (!Data.Validate() || !Player.GetDemoAttributes() || !Player.GetWeaponComponent())
    { Error = TEXT("检查点或当前玩家无效"); UE_LOG(LogFPSDemo, Warning, TEXT("CHECKPOINT_APPLY invalid input")); return false; }
    ADemoPlayerState* PS = Player.GetPlayerState<ADemoPlayerState>(); // 所有预检完成后才写活体状态。
    if (!PS) { Error = TEXT("玩家状态尚未就绪"); return false; }
    UDemoRunBuildComponent* Build = PS->FindComponentByClass<UDemoRunBuildComponent>(); // 同一PS拥有；同步回滚使用Before快照，不刷新基础技能冷却。
    if (!Build) { Error = TEXT("核心组件尚未就绪"); UE_LOG(LogFPSDemo, Warning, TEXT("CHECKPOINT_APPLY build missing")); return false; }
    UDemoRunSave* Before = NewObject<UDemoRunSave>(&Player); // 恢复失败的同步回滚快照，不写盘、不跨异步持有。
    if (!Capture(Data.RunId, State, Player, *Before)) { Error = TEXT("无法准备恢复事务"); return false; }
    const FDemoPlayerProgressSnapshot PreviousProgress = PS->GetProgressComponent()->GetSnapshot(); // 包含权限和领奖标记，不能只回滚余额。
    FDemoLoadoutSnapshot Target; // 新旧磁盘格式在此转换，领域库存不读取SaveGame。
    Target.PrimaryId=Data.PrimaryId; Target.ActiveSlot=Data.ActiveSlot; Target.MagazineBonus=Data.MagazineBonus;
    Target.UnlockedAmmoIds=Data.UnlockedAmmoIds; Target.SelectedAmmoId=Data.SelectedAmmoId;
    for (const FDemoSavedWeapon& Saved : Data.Weapons)
    { FDemoWeaponSnapshot Item; /* 目标库存值，不持Actor。 */ Item.Id=Saved.Id; Item.Ammo=Saved.Ammo; Item.Reserve=Saved.Reserve; Target.Weapons.Add(Item); }
    if (Target.Weapons.IsEmpty()) // 合法新槽没有库存字段，补成已初始化默认手枪的完整快照。
    {
        FDemoLoadoutSnapshot Defaults; // 借用当前初始化库存值，不把空合作成员当作合法新档。
        Player.GetWeaponComponent()->CaptureSnapshot(Defaults);
        for (const FDemoWeaponSnapshot& Item : Defaults.Weapons) if (Item.Id==TEXT("pistol")) Target.Weapons.Add(Item);
    }
    if (!Player.GetWeaponComponent()->PrepareSnapshot(Target,Data.Ownership.EffectiveIds()))
    { Error=TEXT("检查点武器或弹药资源无法准备，玩家原状态未修改"); return false; }
    FDemoPlayerProgressSnapshot Progress=PreviousProgress; // 单人权限保留本人Profile导入值，经济来自所选槽。
    Progress.Ownership=Data.Ownership; Progress.UnlockedWeaponIds=Data.Ownership.EffectiveIds(); // V10当前槽拥有权替代跨槽账号推导。
    Progress.Gambling=Data.Gambling; // 在恢复Avatar属性/库存前重建策略Tag。
    Progress.Coins=Data.Coins; Progress.SilverCoins=Data.SilverCoins; Progress.Kills=Data.Kills;
    Progress.UpgradeProgress=Data.UpgradeProgress; Progress.PistolChallenge=Data.PistolChallenge; Progress.BestEndlessLevel=Data.BestEndlessLevel;
    Progress.LastRewardLevel=Data.Phase==EDemoPhase::Intermission?Data.CompletedLevel:FMath::Max(0,Data.CompletedLevel-1);
    bool bApplied=PS->GetProgressComponent()->Commit(Progress); // 所有资源就绪后才进入写入阶段。
    if (bApplied) { Player.RestoreRunProgress(Data); bApplied=Player.GetWeaponComponent()->ApplyPreparedSnapshot(); }
    if (bApplied) bApplied = Build->RestoreSnapshot(Data.Build); // 核心与钱包/武器同事务；只恢复选择，战斗窗口不续存。
    if (!bApplied)
    {
        PS->GetProgressComponent()->Commit(PreviousProgress); Player.RestoreRunProgress(*Before);
        if (!Build->RestoreSnapshot(Before->Build)) UE_LOG(LogFPSDemo, Error, TEXT("CHECKPOINT_APPLY build rollback failed"));
        Player.GetWeaponComponent()->FinishSnapshot(false); // 旧Actor仍在事务中，回滚不重新生成旧装备。
        Error=TEXT("检查点恢复失败，玩家和库存已回滚，原文件保留"); UE_LOG(LogFPSDemo,Warning,TEXT("CHECKPOINT_APPLY rolled back")); return false;
    }
    Player.GetWeaponComponent()->FinishSnapshot(true);
    State.bEndless=Data.bEndless; State.BestEndlessLevel=Data.BestEndlessLevel;
    State.Difficulty=Data.Difficulty; State.LevelNumber=Data.CompletedLevel; // 个人值提交成功后才发布公开进度。
    return true;
}
