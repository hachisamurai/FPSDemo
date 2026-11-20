// 职责：在原CommonUI事务回归之后，补充R04完整表现状态与真实设置/武器比较断言。
// 使用：WITH_EDITOR显式-DemoCommonUITest -DemoUICompletePresentation；必须独立GameUserSettingsINI及DemoUIScreenshotDir。
// 边界：设置/目录比较通过正式按钮和真实UMG读取；Loading、逻辑大厅和阵亡是明确synthetic夹具，不创建真实多人、出发或死亡结算。
#include "Tests/DemoCommonUITest.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Tests/DemoUILayoutChecks.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "Interaction/DemoInteractable.h"
#include "Player/DemoPlayerController.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Player/Online/DemoOnlineIdentity.h"
#include "Settings/DemoGameUserSettings.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/DemoUIPresentation.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/Screens/DemoSettingsScreen.h"
#include "UI/Screens/DemoTerminalScreen.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UnrealClient.h"

namespace
{
    /** 读取正式WidgetTree的TextBlock，不从OwnerSnapshot或预期值补造页面文字。 */
    FString CompleteRenderedText(UDemoMenuScreen* Screen)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CompleteUIPresentation.ReadText"));
        TArray<UWidget*> Widgets; FString Result; // 仅借用当前树；返回字符串不保留Widget引用。
        if(Screen&&Screen->WidgetTree) Screen->WidgetTree->GetAllWidgets(Widgets);
        for(UWidget* Widget:Widgets) if(const UTextBlock* Text=Cast<UTextBlock>(Widget)) Result+=TEXT("|")+Text->GetText().ToString();
        return Result;
    }

    /** 按真实四列比较行提取文字；缺行或重复行返回false，不接受旁边详情中的同值代替比较表。 */
    bool ReadComparison(UDemoMenuScreen* Screen,TMap<FString,TArray<FString>>& OutRows,FString& OutValues)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CompleteUIPresentation.ReadComparison"));
        OutRows.Reset(); OutValues.Reset();
        const TCHAR* Labels[]={TEXT("伤害 / 单弹丸"),TEXT("射速 / RPM"),TEXT("弹匣 / 发"),TEXT("装填 / 秒")}; // 同口径可读行名，不依赖UMG自动对象名。
        TArray<UWidget*> Widgets; // 当前已构建树的同步借用列表。
        if(Screen&&Screen->WidgetTree) Screen->WidgetTree->GetAllWidgets(Widgets);
        for(UWidget* Widget:Widgets)
        {
            const UHorizontalBox* Row=Cast<UHorizontalBox>(Widget); // 比较表每行四个直接TextBlock；目录/参数两列不能匹配。
            if(!Row||Row->GetChildrenCount()!=4) continue;
            const UTextBlock* Label=Cast<UTextBlock>(Row->GetChildAt(0));
            if(!Label) continue;
            const FString Name=Label->GetText().ToString(); // 当前UI真实标签，不修改或附加隐藏测试文本。
            bool Recognized=false; for(const TCHAR* Candidate:Labels) Recognized|=Name==Candidate;
            if(!Recognized) continue;
            if(OutRows.Contains(Name)) return false;
            TArray<FString> Cells; // 本行四个原始文本值，退出后不持有Slate对象。
            for(int32 Column=0;Column<4;++Column)
            {
                const UTextBlock* Cell=Cast<UTextBlock>(Row->GetChildAt(Column));
                if(!Cell) return false;
                Cells.Add(Cell->GetText().ToString());
            }
            OutRows.Add(Name,MoveTemp(Cells));
        }
        for(const TCHAR* Label:Labels)
        {
            const TArray<FString>* Row=OutRows.Find(Label); // 固定语义顺序，避免WidgetTree遍历顺序改变造成误报。
            if(!Row) return false;
            OutValues+=FString::Join(*Row,TEXT("|"))+TEXT("\n");
        }
        return true;
    }

    /** 用CDO原始字段校验显示数值，不读取实际枪械的GAS增益后伤害，也不复制UI的字符串格式算法。 */
    bool ComparisonMatchesDefinitions(const TMap<FString,TArray<FString>>& Rows,const ADemoWeaponBase* Baseline,const ADemoWeaponBase* Preview)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CompleteUIPresentation.ComparisonMatchesDefinitions"));
        if(!Baseline||!Preview||!Baseline->HasAnyFlags(RF_ClassDefaultObject)||!Preview->HasAnyFlags(RF_ClassDefaultObject)) return false;
        const TCHAR* Labels[]={TEXT("伤害 / 单弹丸"),TEXT("射速 / RPM"),TEXT("弹匣 / 发"),TEXT("装填 / 秒")}; // 伤害按单弹丸，绝不把散弹弹丸总和当基础伤害。
        const float ExpectedBase[]={Baseline->Config.BaseDamage,Baseline->Config.RoundsPerMinute,float(Baseline->Config.MagazineCapacity),Baseline->Config.ReloadSeconds};
        const float ExpectedPreview[]={Preview->Config.BaseDamage,Preview->Config.RoundsPerMinute,float(Preview->Config.MagazineCapacity),Preview->Config.ReloadSeconds}; // 独立读取两份已加载目录定义。
        for(int32 Index=0;Index<4;++Index)
        {
            const TArray<FString>* Cells=Rows.Find(Labels[Index]); // 真实UMG行，与CDO值只做显示精度内比较。
            if(!Cells||Cells->Num()!=4||!(*Cells)[1].IsNumeric()||!(*Cells)[2].IsNumeric()) return false;
            const float Tolerance=Index==3?.051f:.501f; // 当前秒数保留一位，整数列保留整数；容差仅覆盖格式舍入。
            if(!FMath::IsNearlyEqual(FCString::Atof(*(*Cells)[1]),ExpectedBase[Index],Tolerance)
                ||!FMath::IsNearlyEqual(FCString::Atof(*(*Cells)[2]),ExpectedPreview[Index],Tolerance)) return false;
            const float Delta=ExpectedPreview[Index]-ExpectedBase[Index]; // 同字段方向必须是预览减基准。
            if(FMath::IsNearlyZero(Delta,.01f)) { if((*Cells)[3]!=TEXT("—")) return false; }
            else if(!(*Cells)[3].IsNumeric()||!FMath::IsNearlyEqual(FCString::Atof(*(*Cells)[3]),Delta,Tolerance)) return false;
        }
        return true;
    }

    /** 截图与几何在同一渲染帧取样；Canvas观战HUD没有UMG树，Geometry=false时仅生成待人工评审图片。 */
    void CaptureComplete(UDemoUIManager* UI,const TCHAR* Name,bool Geometry=true)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CompleteUIPresentation.Capture"));
        if(Geometry) DemoUILayoutChecks::Queue(UI?UI->GetActiveScreen():nullptr);
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(FString(TEXT("Complete-"))+Name+TEXT(".png")),true,false);
    }
}

void ADemoCommonUITest::TickCompletePresentation()
{
    DEMO_LOG_CALL();
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 每步重新借用，不能跨World保留控制器。
    auto* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
    auto* State=GetWorld()->GetGameState<ADemoGameState>();
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>();
    auto* UI=PC?PC->FindComponentByClass<UDemoUIManager>():nullptr;
    auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // 启动者必须提供隔离INI；不访问正式用户配置。
    if(!Check(PC&&Player&&Player->GetDemoASC()&&Player->GetDemoAttributes()&&State&&Mode&&UI&&Settings,TEXT("complete presentation has production local owner, GAS and UI services"))) return;
    auto* Flow=PC->GetMenuFlow(); // 原Flow唯一处理设置、终端、返回，不另设动作旁路。
    UI->Sync();
    switch(CurrentStage)
    {
    case 400:
    {
        if(!Check(State->Phase==EDemoPhase::Hub&&!Flow->HasBlockingOverlay(),TEXT("complete presentation begins after original transaction and input recovery checks"))) return;
        const UDemoUIPresentationAssets* Sounds=GetDefault<UDemoUIPresentationAssets>(); // 生产CDO强引用的五个自产短音，只检查Cook可达资源，不播放或判断主观听感。
        bool SoundAssetsReady=Sounds!=nullptr; for(int32 Index=0;Index<5;++Index) SoundAssetsReady&=Sounds&&Sounds->GetSound(static_cast<EDemoUISound>(Index))!=nullptr;
        if(!Check(SoundAssetsReady,TEXT("five production UI sound assets resolve on real presentation CDO even with nosound"))) return;
        Flow->EscapePressed(); if(!Press(TEXT("PauseSettings"))) return; Next(401); return;
    }
    case 401:
    {
        if(!Check(Cast<UDemoSettingsScreen>(UI->GetActiveScreen())!=nullptr,TEXT("complete settings is the actual active dedicated widget"))) return;
        PresentationOriginalReducedMotion=DemoUI::ReducedMotion(); PresentationOriginalUISound=DemoUI::UISoundEnabled(); PresentationOriginalSensitivity=Settings->GetMouseSensitivity();
        if(!Press(TEXT("Setting2Plus"))) return;
        PresentationUnappliedDraft.Reset(); for(int32 Index=0;Index<5;++Index) PresentationUnappliedDraft.Add(Flow->GetSettingText(Index)); // 未应用草稿与设备设置分别取证。
        if(!Press(TEXT("UI.ToggleReducedMotion"))||!Press(TEXT("UI.ToggleSound"))) return;
        bool Preserved=true; for(int32 Index=0;Index<5;++Index) Preserved&=Flow->GetSettingText(Index)==PresentationUnappliedDraft[Index]; // 两项即时偏好不得覆盖原五项草稿。
        if(!Check(DemoUI::ReducedMotion()!=PresentationOriginalReducedMotion&&DemoUI::UISoundEnabled()!=PresentationOriginalUISound&&Preserved&&Settings->GetMouseSensitivity()==PresentationOriginalSensitivity,
            TEXT("actual preference buttons change only immediate local presentation values, retaining all five unapplied settings"))) return;
        Next(402); return;
    }
    case 402:
        if(!Check(CompleteRenderedText(UI->GetActiveScreen()).Contains(TEXT("即时保存")),TEXT("actual settings text distinguishes immediate preferences from Apply"))) return;
        CaptureComplete(UI,TEXT("01-SettingsPreferences")); Next(403); return;
    case 403:
        if(!Press(TEXT("OverlayBack"))||!Press(TEXT("PauseSettings"))) return; Next(404); return;
    case 404:
        if(!Check(DemoUI::ReducedMotion()!=PresentationOriginalReducedMotion&&DemoUI::UISoundEnabled()!=PresentationOriginalUISound&&Settings->GetMouseSensitivity()==PresentationOriginalSensitivity,
            TEXT("actual settings close and reopen retain UI preferences and discard unapplied sensitivity"))) return;
        if(!Press(TEXT("UI.ToggleReducedMotion"))||!Press(TEXT("UI.ToggleSound"))||!Press(TEXT("OverlayBack"))||!Press(TEXT("OverlayBack"))) return;
        if(!Check(DemoUI::ReducedMotion()==PresentationOriginalReducedMotion&&DemoUI::UISoundEnabled()==PresentationOriginalUISound,TEXT("complete preference regression restores isolated device values"))) return;
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // 所有终端取景使用可站立Capsule高度，避免物理解穿透造成会话超距。
        Mode->GetShopTerminal()->Interact(Player);
        if(!Press(TEXT("TerminalWeapons"))||!Press(TEXT("Weapon1"))) return; Next(405); return;
    case 405:
    {
        const UDemoMenuScreen* Screen=UI->GetActiveScreen(); // 锁定主枪规则直接显示在目录，不能再要求已删除的详情模态。
        const UDemoMenuButton* Equip=Screen?Screen->FindAction(TEXT("UI.WeaponEquip")):nullptr;
        if(!Check(Screen&&UI->GetModalStackCount()==0&&Flow->GetInspectedWeapon()==INDEX_NONE&&Equip&&!Equip->GetIsEnabled()
            &&!Screen->FindAction(TEXT("UI.WeaponDetails"))&&!Screen->FindAction(TEXT("WeaponTipClose"))&&Screen->HasUserFocusedDescendants(PC),
            TEXT("locked rifle stays inline with actual parent focus, disabled direct equip and no removed details actions"))) return;
        Next(406); return;
    }
    case 406:
    {
        TMap<FString,TArray<FString>> Rows; // 只从已绘制正式比较表取四列数值。
        const UDemoWeaponComponent* Equipment=Player->GetWeaponComponent();
        const UDemoTerminalScreen* Terminal=Cast<UDemoTerminalScreen>(UI->GetActiveScreen()); // 查看项仍是步枪，权威主武器依然为空。
        if(!Check(Terminal&&Terminal->GetPresentationWeaponIndex()==1&&PC->GetMenuSnapshot().PrimaryWeaponId.IsNone()&&Equipment&&Equipment->GetActiveWeapon()
            &&ReadComparison(UI->GetActiveScreen(),Rows,ComparisonRenderedValues)&&ComparisonMatchesDefinitions(Rows,Equipment->GetCatalogWeapon(0),Equipment->GetCatalogWeapon(1))
            &&CompleteRenderedText(UI->GetActiveScreen()).Contains(TEXT("基础配置，不含个人增益")),TEXT("real weapon comparison is rifle versus unequipped-primary pistol CDO, with matching damage RPM capacity reload and signed differences"))) return;
        ComparisonOriginalDamageBonus=Player->GetDemoAttributes()->GetWeaponDamageBonus(); ComparisonOriginalMagazineBonus=Player->GetDemoAttributes()->GetMagazineBonus();
        ComparisonOriginalLiveDamage=Equipment->GetActiveWeapon()->GetDamagePerPellet(); // 对照实际玩家枪实例，保证下面的增益夹具不是空操作。
        Player->GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetWeaponDamageBonusAttribute(),ComparisonOriginalDamageBonus+137.f);
        Player->GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetMagazineBonusAttribute(),ComparisonOriginalMagazineBonus+31.f);
        Next(407); return;
    }
    case 407:
    {
        TMap<FString,TArray<FString>> Rows; FString CurrentValues; // GAS变化后的真实新树文字，不能只比较旧树缓存。
        const UDemoWeaponComponent* Equipment=Player->GetWeaponComponent();
        if(!Check(Equipment&&Equipment->GetActiveWeapon()&&Equipment->GetActiveWeapon()->GetDamagePerPellet()>ComparisonOriginalLiveDamage+.1f
            &&ReadComparison(UI->GetActiveScreen(),Rows,CurrentValues)&&CurrentValues==ComparisonRenderedValues
            &&ComparisonMatchesDefinitions(Rows,Equipment->GetCatalogWeapon(0),Equipment->GetCatalogWeapon(1)),TEXT("real player GAS damage increases while all rendered CDO comparison values remain unchanged"))) return;
        CaptureComplete(UI,TEXT("02-WeaponBaseComparison"));
        Player->GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetWeaponDamageBonusAttribute(),ComparisonOriginalDamageBonus);
        Player->GetDemoASC()->SetNumericAttributeBase(UDemoAttributeSet::GetMagazineBonusAttribute(),ComparisonOriginalMagazineBonus);
        Next(408); return;
    }
    case 408:
        if(!Press(TEXT("UI.Back"))||!Press(TEXT("UI.Back"))) return;
        Player->SetActorLocation(Mode->GetNextLevelTerminal()->GetActorLocation()+FVector(100,0,110)); // 复用真实出发终端会话，不调用出发提交。
        Mode->GetNextLevelTerminal()->Interact(Player); Next(409); return;
    case 409:
        if(!Check(Flow->IsNextLevelConfirmationOpen()&&UI->GetActiveScreen()->FindAction(TEXT("Normal")),TEXT("loading fixture begins from real next-level terminal page"))) return;
        PresentationOriginalPreparation=State->Preparation;
        State->Preparation.bActive=true; State->Preparation.bReadyToCommit=false; ++State->Preparation.Generation;
        State->Preparation.Stage=State->LevelNumber+1; State->Preparation.ReadyPlayers=1; State->Preparation.TotalPlayers=4;
        State->Preparation.Status=TEXT("等待落脚点确认（表现夹具）"); // SYNTHETIC：只展示公开复制投影，不伪造准备系统ACK或生成真实多人。
        UE_LOG(LogFPSDemo,Log,TEXT("SYNTHETIC_UI preparation_loading public_view_only ready=1 total=4 no_request_or_commit")); Next(410); return;
    case 410:
    {
        bool Disabled=true; for(const TCHAR* Action:{TEXT("Easy"),TEXT("Normal"),TEXT("Hard")}) { const UDemoMenuButton* Button=UI->GetActiveScreen()->FindAction(Action); Disabled&=Button&&!Button->GetIsEnabled(); } // 检查实际禁用按钮，不触发任何服务器出发事务。
        const FString Text=CompleteRenderedText(UI->GetActiveScreen()); // 读取真实人数与失败/等待原因。
        if(!Check(Disabled&&Text.Contains(TEXT("1 / 4"))&&Text.Contains(TEXT("正在准备入场"))&&Text.Contains(State->Preparation.Status),TEXT("synthetic preparation Loading renders public counts and reason and disables duplicate difficulty submission"))) return;
        CaptureComplete(UI,TEXT("03-PreparationLoading-SYNTHETIC")); Next(411); return;
    }
    case 411:
        State->Preparation=PresentationOriginalPreparation; // 先恢复真实视图，再走原取消动作；不让夹具进入后续服务/Spine。
        if(!Press(TEXT("NextLevelNo"))) return;
        State->Phase=EDemoPhase::Lobby; ++State->PhaseRevision; State->OnRep_RunView(); PC->PublishMenuSnapshot(); PC->OnRunReady();
        Flow->MultiplayerPressed(false); // SYNTHETIC逻辑大厅，仅打开缓存视图；刻意不搜索、不登录、不建立OSS连接。
        UE_LOG(LogFPSDemo,Log,TEXT("SYNTHETIC_UI room_browser logical_lobby_only no_search_login_or_session")); Next(412); return;
    case 412:
    {
        const UDemoRoomSubsystem* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // 原GI缓存；该隔离运行从未搜索/连接。
        if(!Check(Rooms&&Rooms->GetRooms().IsEmpty()&&!Rooms->IsBusy()&&Flow->GetMenuPage()==EDemoMenuPage::Multiplayer
            &&UI->GetActiveScreen()->FindAction(TEXT("RoomLAN"))&&UI->GetActiveScreen()->FindAction(TEXT("RoomPublic"))
            &&CompleteRenderedText(UI->GetActiveScreen()).Contains(TEXT("暂未发现可加入的房间")),TEXT("real empty room cache renders empty state and fixed network navigation without fabricating sessions"))) return;
        CaptureComplete(UI,TEXT("04-RoomsEmpty-SYNTHETIC")); Next(413); return;
    }
    case 413:
    {
        UDemoRoomSubsystem* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // INDEX_NONE由正式服务最前门禁拒绝，绝不进入OSS JoinSession。
        if(!Check(Rooms&&!Rooms->Join(INDEX_NONE,Rooms->GetRevision()),TEXT("invalid room reference is rejected by production service without network join"))) return;
        Next(414); return;
    }
    case 414:
    {
        const UDemoRoomSubsystem* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>();
        if(!Check(Rooms&&!Rooms->GetStatus().IsEmpty()&&CompleteRenderedText(UI->GetActiveScreen()).Contains(Rooms->GetStatus()),TEXT("actual rejected room reason remains readable in room page"))) return;
        CaptureComplete(UI,TEXT("05-RoomsRejected-SYNTHETIC")); Next(415); return;
    }
    case 415:
        if(!Press(TEXT("RoomPublic"))) return; Next(416); return; // Editor生产身份服务明确禁用EOS，无SDK登录/云请求。
    case 416:
    {
        const UDemoRoomSubsystem* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>();
        const UDemoMenuButton* Quick=UI->GetActiveScreen()->FindAction(TEXT("RoomQuick")); const UDemoMenuButton* Create=UI->GetActiveScreen()->FindAction(TEXT("RoomCreate")); // 当前公网不可用操作不能冒充可加入。
        if(!Check(Rooms&&Rooms->IsPublic()&&!Rooms->IsReady()&&Quick&&!Quick->GetIsEnabled()&&Create&&!Create->GetIsEnabled()
            &&CompleteRenderedText(UI->GetActiveScreen()).Contains(Rooms->GetStatus()),TEXT("real Editor public-room restriction remains readable and disables create and quick join"))) return;
        CaptureComplete(UI,TEXT("06-RoomsUnavailable-EDITOR")); Next(417); return;
    }
    case 417:
        if(!Press(TEXT("OverlayBack"))) return;
        if(!Check(GetGameInstance()->GetSubsystem<UDemoOnlineIdentity>()->SelectNetwork(EDemoRoomNetwork::LAN),TEXT("fixture cleanup restores LAN identity without searching"))) return; // 直接清理身份服务避免RoomLAN按钮自动发起不必要网络搜索。
        State->Phase=EDemoPhase::Combat; State->LevelNumber=4; ++State->PhaseRevision; State->OnRep_RunView(); PC->PublishMenuSnapshot(); PC->OnRunReady();
        Player->SetCoopEliminated(true); // SYNTHETIC公开阵亡标记，沿原HUD/输入门禁；不触发真实伤害、全队死亡或奖励结算。
        UE_LOG(LogFPSDemo,Log,TEXT("SYNTHETIC_UI spectator_waiting self_eliminated_only no_remote_player_or_death_resolution")); Next(418); return;
    case 418:
        if(!Check(Player->IsCoopEliminated()&&!Player->CanUsePlayerSkills()&&!UI->UsesWidgetMenu(),TEXT("synthetic eliminated player blocks skills while spectator Canvas presentation has no stale UMG menu"))) return;
        Flow->TabPressed(); if(!Check(Flow->GetMenuPage()!=EDemoMenuPage::PlayerStatus,TEXT("actual Tab cannot display living-player status for eliminated spectator"))) return;
        CaptureComplete(UI,TEXT("07-SpectatorWaiting-SYNTHETIC"),false); Next(419); return; // Canvas截图仍需人工审美/文字裁切评审，不宣称UMG几何通过。
    case 419:
        Player->SetCoopEliminated(false); State->Phase=EDemoPhase::Hub; State->LevelNumber=0; ++State->PhaseRevision; State->OnRep_RunView(); PC->PublishMenuSnapshot(); PC->OnRunReady();
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // 恢复可站立Hub上下文，继续既有Spine及终端销毁/旧按钮回归。
        if(!Check(!Player->IsCoopEliminated()&&!Flow->HasBlockingOverlay()&&!UI->UsesWidgetMenu(),TEXT("complete presentation restores playable owner and clears synthetic state before existing spine"))) return;
        UE_LOG(LogFPSDemo,Log,TEXT("DEMO_COMPLETE_UI_PRESENTATION_SCOPE_DONE continuing_spine_and_original_lifecycle; synthetic_states_are_not_multiplayer_acceptance"));
        Next(300); return;
    default: Check(false,TEXT("unexpected complete presentation fixture stage")); return;
    }
}
#endif
