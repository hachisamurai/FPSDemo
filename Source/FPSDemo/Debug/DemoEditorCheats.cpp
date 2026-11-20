#include "Debug/DemoEditorCheats.h"
#if WITH_EDITOR
#include "Debug/DemoLog.h"
#include "Player/DemoPlayerProfile.h"
#include "Characters/DemoCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

bool DemoEditorCheats::Enabled(const UObject* Context,int32 Flag)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const UWorld* World=Context?Context->GetWorld():nullptr; // 同步借用当前World，不缓存跨PIE实例指针。
    const UGameInstance* Instance=World?World->GetGameInstance():nullptr; // 每GI独立持有开关；其他PIE不共享。
    const UDemoPlayerProfile* Profile=Instance?Instance->GetSubsystem<UDemoPlayerProfile>():nullptr; // 不创建额外保存或云服务。
    return World&&World->IsGameWorld()&&World->GetNetMode()==NM_Standalone&&Profile&&Profile->IsDebugTestFlagEnabled(Flag);
}
int32 DemoEditorCheats::Debit(const UObject* Context,bool bGold,int32 QuotedCost)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    return QuotedCost>=0&&Enabled(Context,bGold?0:1)?0:QuotedCost; // 非法负报价不被调试开关修复为合法值。
}
bool DemoEditorCheats::Invulnerable(const AActor* Avatar)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const ADemoCharacter* Player=Cast<ADemoCharacter>(Avatar); // 只有玩家类型可以受本地开关保护。
    return Player&&Player->IsLocallyControlled()&&Enabled(Player,2);
}
FString DemoEditorCheats::Status(const UObject* Context)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    FString Result; // 本帧值，HUD不持账户或Profile引用。
    if(Enabled(Context,0)) Result+=TEXT("无限金币  ");
    if(Enabled(Context,1)) Result+=TEXT("无限银币  ");
    if(Enabled(Context,2)) Result+=TEXT("无敌  ");
    return Result.IsEmpty()?Result:TEXT("EDITOR TEST · ")+Result;
}
#endif
