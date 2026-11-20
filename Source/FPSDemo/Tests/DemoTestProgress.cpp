#include "Tests/DemoTestProgress.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerProfile.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"
namespace
{
    /** Context仅为明确单人测试World定位，本助手不注册控制台命令或RPC。 */
    UDemoPlayerProgressComponent* ResolveTestProgress(const UObject* Context)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        const APawn* Pawn=Context?UGameplayStatics::GetPlayerPawn(Context,0):nullptr; // 单人夹具明确使用第0位玩家。
        const ADemoPlayerState* State=Pawn?Pawn->GetPlayerState<ADemoPlayerState>():nullptr; // 个人数据源。
        return State?State->GetProgressComponent():nullptr;
    }
}
const FDemoPlayerProgressSnapshot& DemoTestProgress::Read(const UObject* Context)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
    const UDemoPlayerProgressComponent* Progress=ResolveTestProgress(Context); // 同步借用，不保存跨World指针。
    static const FDemoPlayerProgressSnapshot Missing; // 只读失败值，绝不允许测试往兼容GS写入以掩盖未就绪。
    if (!Progress) UE_LOG(LogFPSDemo,Error,TEXT("TEST_PROGRESS player not ready"));
    return Progress?Progress->GetSnapshot():Missing;
}
bool DemoTestProgress::SetGold(const UObject* Context,int32 Amount)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
#if !UE_BUILD_SHIPPING
    UDemoPlayerProgressComponent* Progress=ResolveTestProgress(Context); // 非Shipping单人测试才可设定夹具余额。
    if (!Progress || !Context->GetWorld() || Context->GetWorld()->GetNetMode()!=NM_Standalone) return false;
    FDemoPlayerProgressSnapshot Next=Progress->GetSnapshot(); // 保留其余来源字段，提交仍走生产校验。
    Next.Coins=Amount; return Progress->Commit(Next);
#else
    return false;
#endif
}
bool DemoTestProgress::SetSilver(const UObject* Context,int32 Amount)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
#if !UE_BUILD_SHIPPING
    UDemoPlayerProgressComponent* Progress=ResolveTestProgress(Context); // 严格单人夹具，不为联网测试伪造队友钱包。
    if (!Progress || !Context->GetWorld() || Context->GetWorld()->GetNetMode()!=NM_Standalone) return false;
    FDemoPlayerProgressSnapshot Next=Progress->GetSnapshot(); // 银币修改不改变金币或升级来源。
    Next.SilverCoins=Amount; return Progress->Commit(Next);
#else
    return false;
#endif
}
bool DemoTestProgress::ImportLocalPermissions(const UObject* Context)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
#if !UE_BUILD_SHIPPING
    UDemoPlayerProgressComponent* Progress=ResolveTestProgress(Context); // 只适配老测试显式创建的本机通关事实。
    UWorld* World=Context?Context->GetWorld():nullptr; // 不影响联网房间权限。
    if (!Progress || !World || World->GetNetMode()!=NM_Standalone || !World->GetGameInstance()) return false;
    const UDemoPlayerProfile* Profile=World->GetGameInstance()->GetSubsystem<UDemoPlayerProfile>(); // 测试GI本来就使用隔离存档。
    if (!Profile) return false;
    FDemoPlayerProgressSnapshot Next=Progress->GetSnapshot(); // 不覆盖当前测试经济与GAS。
    for (int32 Index=0; Index<DemoWeaponCatalog::Num(); ++Index)
        if (Profile->IsUnlocked(DemoWeaponCatalog::IdAt(Index))) Next.UnlockedWeaponIds.AddUnique(DemoWeaponCatalog::IdAt(Index));
    Next.bHellUnlocked=Profile->IsHellUnlocked(); Next.bEndlessUnlocked=Profile->IsEndlessUnlocked(); return Progress->Commit(Next);
#else
    return false;
#endif
}
