#include "Game/FPSDemoGameMode.h"
// 本文件只保留模式资格与配置查询；生成调度统一位于Spawning/DemoEnemySpawnComponent。
#include "Player/DemoPlayerProfile.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"

bool AFPSDemoGameMode::GetEndlessConfig(FDemoEndlessRow& OutConfig, FString& Error) const
{
    DEMO_LOG_CALL();
    const FDemoEndlessRow* Row = EndlessTable && EndlessTable->GetRowStruct() == FDemoEndlessRow::StaticStruct()
        ? EndlessTable->FindRow<FDemoEndlessRow>(TEXT("Default"),TEXT("Endless"),false) : nullptr; // 只借用正确原生类型的Default行。
    const FDemoLevelRow* Boss = LevelTable ? LevelTable->FindRow<FDemoLevelRow>(DemoCombatConfig::LevelName(10),TEXT("EndlessBoss"),false) : nullptr; // Boss每10关出现，复用第十关模板与银币基数。
    if (!Row || !Boss || Boss->BossRow.IsNone() || Row->MaxAlive < 4 || Row->MaxAlive > 16)
    { Error=TEXT("无尽配置缺失、并发数量不在4到16之间或第十关Boss模板无效"); UE_LOG(LogFPSDemo,Warning,TEXT("%s"),*Error); return false; } // Boss关必须同时容纳三种小怪与Boss。
    for (const float Rate : {Row->HealthGrowth,Row->DamageGrowth,Row->CountGrowth,Row->SilverGrowth}) // 值快照，拒绝NaN/无限倍率。
        if (!FMath::IsFinite(Rate) || Rate < 1.f || Rate > 3.f)
        { Error=TEXT("无尽成长倍率必须在1到3之间"); UE_LOG(LogFPSDemo,Warning,TEXT("%s"),*Error); return false; }
    OutConfig=*Row;
    return true;
}
bool AFPSDemoGameMode::SelectEndless()
{
    DEMO_LOG_CALL();
    ADemoGameState* State=GetGameState<ADemoGameState>(); // 仅权威初始安全区允许修改模式。
    UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 账号永久通关事实，不读取当前武器。
    FDemoEndlessRow Config; FString Error; // 先验证配置再提交选择，避免进入不可生成的模式。
    if (!State || State->Phase!=EDemoPhase::Hub || State->LevelNumber!=0 || !Profile || !Profile->IsEndlessUnlocked() || !GetEndlessConfig(Config,Error))
    { UE_LOG(LogFPSDemo,Warning,TEXT("ENDLESS select rejected: locked/phase/config %s"),*Error); return false; }
    State->bEndless=true; State->Difficulty=EDemoDifficulty::Hell;
    UE_LOG(LogFPSDemo,Log,TEXT("ENDLESS selected best=%d"),State->BestEndlessLevel);
    return true;
}
bool AFPSDemoGameMode::RegisterWeaponShot(FName WeaponId)
{
    DEMO_LOG_CALL();
    ADemoGameState* State=GetGameState<ADemoGameState>(); // 在命中前记录成功开火来源，最后一发造成胜利也不会漏记。
    if (!State || State->Phase!=EDemoPhase::Combat) { UE_LOG(LogFPSDemo,Warning,TEXT("Challenge shot rejected outside Combat")); return false; }
    const int32 Next = WeaponId==TEXT("pistol") ? FMath::Max(1,State->PistolChallenge) : 2; // 失格2在同轮不可逆；空弹/拒绝射击不调用。
    if (Next!=State->PistolChallenge) UE_LOG(LogFPSDemo,Log,TEXT("PISTOL_CHALLENGE %d -> %d weapon=%s"),State->PistolChallenge,Next,*WeaponId.ToString());
    State->PistolChallenge=Next;
    return GetGameInstance()->GetSubsystem<UDemoRunSaves>()->StoreChallenge(Next,State->BestEndlessLevel);
}
