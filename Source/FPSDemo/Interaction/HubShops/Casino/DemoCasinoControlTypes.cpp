// 职责：读取CP-01实体老虎机独立开关和固定绑定版本；不加载作者资产、不改变正式DA、概率、云或个人进度。
#include "Interaction/HubShops/Casino/DemoCasinoControlTypes.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Debug/DemoStaticLog.h"

bool DemoCasinoControls::IsSlotsPrototypeEnabled(const UWorld* World)
{
    DEMO_LOG_STATIC_TICK(nullptr);
    if (!World || !World->IsGameWorld()) return false;
    if (GetDefault<UDemoCasinoPhysicalSettings>()->bSlotsPrototypeEnabled) return true;
#if WITH_EDITOR
    return FParse::Param(FCommandLine::Get(),TEXT("DemoCasinoPhysicalTest")); // 显式Editor真实夹具，普通PIE/作者读回默认不启用。
#else
    return false;
#endif
}
uint32 DemoCasinoControls::BindingRevision()
{ DEMO_LOG_STATIC_TICK(nullptr); return 2; } // 原A07未接入草案厚盒为1；当前薄键/共面视觉修订为2，旧绑定拒绝，不复用同Mode缓存。

bool DemoCasinoControls::ResolveSemantic(FName ControlId,EDemoCasinoControlAction& Action,int32& Index)
{
    DEMO_LOG_STATIC_TICK(nullptr); Action=EDemoCasinoControlAction::None; Index=INDEX_NONE;
    const FString Text=ControlId.ToString(); // 固定长度十进制键，不把任意后缀转0。
    if (Text.StartsWith(TEXT("amount.digit.")) && Text.Len()==14 && Text[13]>=TEXT('0') && Text[13]<=TEXT('9'))
    { Action=EDemoCasinoControlAction::AmountDigit; Index=Text[13]-TEXT('0'); return true; }
    if (ControlId==TEXT("amount.clear")) Action=EDemoCasinoControlAction::AmountClear;
    else if (ControlId==TEXT("amount.backspace")) Action=EDemoCasinoControlAction::AmountBackspace;
    else if (ControlId==TEXT("amount.commit")) Action=EDemoCasinoControlAction::AmountCommit;
    else if (ControlId==TEXT("slots.pull")) Action=EDemoCasinoControlAction::PullSlots;
    else if (ControlId==TEXT("device.exit")) Action=EDemoCasinoControlAction::Exit;
    return Action!=EDemoCasinoControlAction::None;
}
