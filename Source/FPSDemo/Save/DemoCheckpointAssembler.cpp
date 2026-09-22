#include "Save/DemoCheckpointAssembler.h"
#include "Save/DemoRunSave.h"
#include "Game/DemoGameState.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Debug/DemoLog.h"
bool DemoCheckpointAssembler::Capture(const FString& RunId, const ADemoGameState& State, const ADemoCharacter& Player, UDemoRunSave& Out)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    const UDemoAttributeSet* Attributes = Player.GetDemoAttributes(); // 借用当前ASC属性，不保存瞬时GE。
    if (!Attributes || !Player.GetWeaponComponent()) { UE_LOG(LogFPSDemo, Warning, TEXT("CHECKPOINT_CAPTURE missing Avatar state")); return false; }
    Out.CreatedLocal = FDateTime::Now(); // Store替换为原槽创建时间，先提供合法值用于校验。
    Out.RunId = RunId;
    Out.bEndless = State.bEndless; Out.BestEndlessLevel = State.BestEndlessLevel; Out.PistolChallenge = State.PistolChallenge; // V5资格与当前关卡原子保存。
    Out.Phase = State.Phase; Out.Difficulty = State.Difficulty; Out.CompletedLevel = State.LevelNumber;
    Out.Coins = State.Coins; Out.Kills = State.TotalKills; Out.Purchases = State.Purchases;
    Out.SilverCoins = State.SilverCoins; Out.GoldPurchases = State.GoldPurchases; Out.SilverPurchases = State.SilverPurchases; // 双币余额与独立价格作为同一检查点提交。
    Out.UpgradeProgress = State.UpgradeProgress; // 永久来源与逐项次数必须和GAS合计原子保存。
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
    State.bEndless = Data.bEndless; State.BestEndlessLevel = Data.BestEndlessLevel; // 资格投影由Challenge.RestoreProgress唯一写入，避免两份运行时真值。
    State.Difficulty = Data.Difficulty; State.LevelNumber = Data.CompletedLevel;
    State.Coins = Data.Coins; State.TotalKills = Data.Kills; State.Purchases = Data.Purchases;
    State.SilverCoins = Data.SilverCoins; State.GoldPurchases = Data.GoldPurchases; State.SilverPurchases = Data.SilverPurchases; // GI在读盘时已完成旧格式到V3迁移。
    State.UpgradeProgress = Data.UpgradeProgress; // 读档只恢复账本，不重放金币GE，避免永久加成叠加两次。
    Player.RestoreRunProgress(Data);
    if (!Player.GetWeaponComponent()->RestoreLoadout(Data))
    { Error = TEXT("存档武器无法恢复，原文件未覆盖"); UE_LOG(LogFPSDemo, Warning, TEXT("CHECKPOINT_APPLY loadout failed")); return false; }
    return true;
}
