#include "Player/Coop/DemoCoopSessionSubsystem.h"
#include "Player/Online/DemoOnlineIdentity.h" // 公网恢复键必须来自登录身份，不能复用本机匿名GUID。
#include "Save/DemoRunSave.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Debug/DemoLog.h"
void UDemoCoopSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL(); Super::Initialize(Collection);
    Collection.InitializeDependency<UDemoOnlineIdentity>(); // 身份服务只初始化本地状态，不自动联网。
    LocalPlayerKey = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
#if !WITH_EDITOR
    UDemoCoopIdentitySave* Identity = Cast<UDemoCoopIdentitySave>(UGameplayStatics::LoadGameFromSlot(TEXT("DemoCoopIdentity"), 0)); // 小型本机身份文件，不上传货币。
    FGuid Existing; // 拒绝损坏键，避免不同机器意外映射到同一空身份。
    if (Identity && FGuid::Parse(Identity->PlayerKey, Existing) && Existing.IsValid()) LocalPlayerKey = Identity->PlayerKey;
    else
    {
        Identity = NewObject<UDemoCoopIdentitySave>(this); Identity->PlayerKey = LocalPlayerKey;
        if (!UGameplayStatics::SaveGameToSlot(Identity, TEXT("DemoCoopIdentity"), 0)) UE_LOG(LogFPSDemo, Warning, TEXT("COOP identity save failed; reconnect key lasts this process only"));
    }
#endif
}
FString UDemoCoopSessionSubsystem::GetLocalPlayerKey() const
{
    DEMO_LOG_TICK();
    const UDemoOnlineIdentity* Identity = GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>(); // 选择结果跨旅行保持；Public未登录必须返回空键。
    return Identity && Identity->IsPublic() && GetWorld() && GetWorld()->GetNetMode() != NM_Standalone ? Identity->GetOnlinePlayerKey() : LocalPlayerKey; // 浏览公网页不改变单人存档的恢复身份。
}
bool UDemoCoopSessionSubsystem::HostCoop()
{
    DEMO_LOG_CALL();
    UWorld* World = GetWorld(); // 当前本机 World，不能在远端客户端上升格为现有房间房主。
    UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 转换前冻结活动槽恢复意图。
    const ADemoGameState* State = World ? World->GetGameState<ADemoGameState>() : nullptr; // 只允许安全区开房。
    AFPSDemoGameMode* Mode = World ? World->GetAuthGameMode<AFPSDemoGameMode>() : nullptr; // 转换前先保存最新本人库存与成长，再选择同槽旅行。
    if (!World || World->GetNetMode() != NM_Standalone || !State || State->Phase != EDemoPhase::Hub || !Saves || !Mode || !Mode->SaveCheckpoint() || !Saves->SelectSlot(Saves->GetActiveSlot()))
    { UE_LOG(LogFPSDemo, Warning, TEXT("HOST rejected: select a checkpoint and enter hub in standalone first")); return false; }
    UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)), true, TEXT("listen?ResumeRun=1"));
    UE_LOG(LogFPSDemo, Display, TEXT("COOP listen host requested (up to four players)")); return true;
}
bool UDemoCoopSessionSubsystem::JoinCoop(const FString& Address)
{
    DEMO_LOG_CALL();
    const UDemoOnlineIdentity* Identity = GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>(); // 显式公网模式只接受SDK的受限连接格式。
    if (Address.IsEmpty() || Address.Len() > 253 || !GetWorld() || GetWorld()->GetNetMode() != NM_Standalone)
    { UE_LOG(LogFPSDemo, Warning, TEXT("JOIN rejected: invalid address or already connected")); return false; }
    if (Identity->IsPublic() && (!Identity->IsReady() || !UDemoOnlineIdentity::IsEOSAddress(Address)))
    { UE_LOG(LogFPSDemo, Warning, TEXT("JOIN rejected unauthenticated or non-EOS endpoint")); return false; }
    if (!Identity->IsPublic() && Address.StartsWith(TEXT("EOS:")))
    { UE_LOG(LogFPSDemo, Warning, TEXT("JOIN rejected EOS endpoint in LAN mode")); return false; }
    for (const TCHAR Character : Address) // DNS/IP或受限EOS地址；均排除?options、斜杠、空白及命令分隔字符。
        if (!FChar::IsAlnum(Character) && Character != TEXT('.') && Character != TEXT('-') && Character != TEXT(':') && Character != TEXT('[') && Character != TEXT(']'))
        { UE_LOG(LogFPSDemo, Warning, TEXT("JOIN rejected: invalid address character")); return false; }
    APlayerController* Player = GetGameInstance()->GetFirstLocalPlayerController(); // 本地拥有者，旅行只影响本人进程。
    if (!Player) { UE_LOG(LogFPSDemo, Warning, TEXT("JOIN rejected: local controller unavailable")); return false; }
    // DemoCoopLocalPlayer在每次登录报文注入身份/协议，服务器旅行不会丢失首次连接参数。
    Player->ClientTravel(Address, TRAVEL_Absolute);
    UE_LOG(LogFPSDemo, Display, TEXT("COOP join requested")); return true;
}
