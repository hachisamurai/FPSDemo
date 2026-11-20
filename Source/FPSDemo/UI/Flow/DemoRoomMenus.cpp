#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

void UDemoMenuFlowComponent::MultiplayerPressed(bool bSearch)
{
    DEMO_LOG_CALL();
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 本机大厅才能替换为房间页。
    if (bStopped || !GetController()->IsLocalController() || !State || State->Phase != EDemoPhase::Lobby || GetNetMode() != NM_Standalone || HasBlockingOverlay())
    { UE_LOG(LogFPSDemo, Log, TEXT("ROOM_MENU rejected phase/owner/overlay")); return; }
    MenuPage = EDemoMenuPage::Multiplayer; RoomPage = 0; bChoosingHostSave = false; MenuMessage.Empty();
    if (bSearch) GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->Search();
    RefreshMenuInput();
}
void UDemoMenuFlowComponent::RefreshRooms()
{
    DEMO_LOG_CALL();
    if (bStopped || MenuPage != EDemoMenuPage::Multiplayer) return;
    if (GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->Search()) RoomPage = 0;
}
void UDemoMenuFlowComponent::SelectRoomNetwork(bool bPublic)
{
    DEMO_LOG_CALL();
    if (bStopped || MenuPage != EDemoMenuPage::Multiplayer) return;
    if (GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->SelectNetwork(bPublic ? EDemoRoomNetwork::PublicEOS : EDemoRoomNetwork::LAN)) RoomPage = 0;
}
void UDemoMenuFlowComponent::RoomAccountPressed()
{
    DEMO_LOG_CALL();
    if (bStopped || MenuPage != EDemoMenuPage::Multiplayer) return;
    GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->LoginOrLogout();
}
void UDemoMenuFlowComponent::QuickJoinRoom()
{
    DEMO_LOG_CALL();
    if (bStopped || MenuPage != EDemoMenuPage::Multiplayer) return;
    GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->QuickJoin(); RoomPage = 0;
}
void UDemoMenuFlowComponent::CreateRoomPressed()
{
    DEMO_LOG_CALL();
    UDemoRoomSubsystem* Rooms = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // GI服务跨选槽旅行存活。
    if (bStopped || MenuPage != EDemoMenuPage::Multiplayer || Rooms->IsBusy() || !Rooms->IsReady()) return; // 公网未登录不得提前创建存档。
    GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>()->RefreshSlots();
    bChoosingHostSave = true; MenuPage = EDemoMenuPage::Saves; MenuMessage.Empty(); RefreshMenuInput();
}
void UDemoMenuFlowComponent::JoinRoom(int32 Index, uint32 Revision)
{
    DEMO_LOG_CALL();
    if (bStopped || !GetController()->IsLocalController() || MenuPage != EDemoMenuPage::Multiplayer) return;
    GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->Join(Index, Revision);
}
bool UDemoMenuFlowComponent::JoinDirectRoom(const FString& RawAddress, FString& OutError)
{
    DEMO_LOG_CALL();
    OutError.Empty();
    if (bStopped || !GetController()->IsLocalController() || MenuPage != EDemoMenuPage::Multiplayer)
    {
        OutError = TEXT("当前不在多人房间页面，无法发起直连");
        UE_LOG(LogFPSDemo, Log, TEXT("ROOM_DIRECT_MENU rejected lifecycle/owner/page"));
        return false;
    }
    UDemoRoomSubsystem* Rooms = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>() : nullptr;
    if (!Rooms)
    {
        OutError = TEXT("房间服务不可用");
        UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_DIRECT_MENU rejected missing room subsystem"));
        return false;
    }
    return Rooms->JoinDirect(RawAddress, OutError);
}
void UDemoMenuFlowComponent::ChangeRoomPage(int32 Direction)
{
    DEMO_LOG_CALL();
    if (bStopped || MenuPage != EDemoMenuPage::Multiplayer || FMath::Abs(Direction) != 1) return;
    const int32 LastPage = FMath::Max(0, (GetWorld()->GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->GetRooms().Num() - 1) / 5); // 空列表仍保留第1页。
    RoomPage = FMath::Clamp(RoomPage + Direction, 0, LastPage);
}
int32 UDemoMenuFlowComponent::GetRoomPage() const { DEMO_LOG_TICK(); return RoomPage; }
bool UDemoMenuFlowComponent::IsChoosingHostSave() const { DEMO_LOG_TICK(); return bChoosingHostSave; }
