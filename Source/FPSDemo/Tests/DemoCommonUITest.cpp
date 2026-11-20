// 职责：真实 CommonUI 栈、UMG 焦点/文本及终端事务回归。使用 -DemoCommonUITest；另加 -DemoGambleHomeTest 可独立复测关间抽取返回。
#include "Tests/DemoCommonUITest.h"
#include "Tests/DemoUILayoutChecks.h" // 已绘制帧的点击区域与固定导航几何回归。
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Characters/DemoCharacter.h"
#include "Components/Image.h" // 读取真实布局后的弹药图标，不把设置DesiredSize视为最终几何验证。
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h" // R04检查实际已绘制Widget引用项目持久化字体，拒绝静默系统回退。
#include "Components/EditableTextBox.h" // 金额专项必须验证编辑框本身获得焦点，不能仅用整页有焦点蒙混通过。
#include "CommonInputSubsystem.h" // 本专项只注入键盘/鼠标；过滤外部实体手柄事件，避免后台按键改变当前焦点。
#include "Engine/LocalPlayer.h"
#include "Economy/DemoShopComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "InputCoreTypes.h"
#include "Interaction/DemoInteractable.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h" // 可选主流程表现夹具不改变原83条菜单/交易回归路径。
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerState.h"
#include "Progression/Builds/DemoRunBuildComponent.h" // 关间夹具恢复合法的已锁核心，真实抽取仍须通过检查点校验。
#include "Save/DemoRunSave.h"
#include "Settings/DemoGameUserSettings.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/Screens/DemoTerminalScreen.h" // R02核心只读目录索引，不要求历史三卡同时暴露确认按钮。
#include "UI/Screens/DemoSessionScreen.h" // 首次提示也已迁移，必须通过当前真实按钮关闭。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UnrealClient.h"
namespace
{
    /** 当前模态默认安全动作必须持有真实Slate焦点；不先手工聚焦来制造通过。 */
    bool SafeActionHasFocus(UDemoMenuScreen* Screen,FName Action,APlayerController* Owner)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonUITest.SafeActionHasFocus"));
        const UDemoMenuButton* Button=Screen?Screen->FindAction(Action):nullptr; // 当前活动树拥有，仅同步借用。
        return Button&&Button->GetIsEnabled()&&(Button->HasUserFocus(Owner)||Button->HasUserFocusedDescendants(Owner));
    }
}
#endif

ADemoCommonUITest::ADemoCommonUITest()
{
    DEMO_LOG_CALL();
#if WITH_EDITOR
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
#endif
}

#if WITH_EDITOR
bool ADemoCommonUITest::Check(bool Value,const TCHAR* Reason)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Display,TEXT("COMMON_UI_TEST %s stage=%d %s"),Value?TEXT("PASS"):TEXT("FAIL"),CurrentStage,Reason);
    if(Value) ++Passed;
    else { Failed=true; SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,1); }
    return Value;
}

bool ADemoCommonUITest::Press(FName Action)
{
    DEMO_LOG_CALL();
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 此隔离进程唯一本地拥有者。
    UDemoUIManager* UI=PC?PC->FindComponentByClass<UDemoUIManager>():nullptr; // 组件由生产 PC 创建，测试不临时伪造栈。
    if(UI) UI->Sync();
    const bool Pressed=UI&&UI->PressActionForTest(Action); // 真实按钮须仍在当前顶层且可交互。
    if(UI) UI->Sync();
    return Check(Pressed,*FString::Printf(TEXT("actual CommonUI button %s accepted"),*Action.ToString()));
}

void ADemoCommonUITest::Type(const TCHAR* Text)
{
    DEMO_LOG_CALL();
    for(const TCHAR* Character=Text;*Character;++Character)
        FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(*Character,FModifierKeysState(),0,false));
}

void ADemoCommonUITest::Key(FKey InputKey,bool Control)
{
    DEMO_LOG_CALL();
    const FModifierKeysState Modifiers(false,false,Control,false,false,false,false,false,false); // 左 Ctrl 仅用于真实 Ctrl+A 编辑行为。
    FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(InputKey,Modifiers,0,false,0,0));
    FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(InputKey,Modifiers,0,false,0,0));
}

bool ADemoCommonUITest::RejectSave()
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Log,TEXT("COMMON_UI_TEST injected checkpoint failure"));
    return false;
}

void ADemoCommonUITest::Next(int32 Stage,double Delay)
{
    DEMO_LOG_CALL();
    CurrentStage=Stage;
    Deadline=FPlatformTime::Seconds()+Delay;
}
#endif

void ADemoCommonUITest::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
#if WITH_EDITOR
    if(Failed) return;
    FString LayoutError; const int32 LayoutResult=DemoUILayoutChecks::ProcessPending(LayoutError); // 读取上次截图帧末的实测结果，不采样正在重建的控件。
    if(LayoutResult&&!Check(LayoutResult>0,*FString::Printf(TEXT("common UI rendered layout: %s"),*LayoutError))) return;
    const double Now=FPlatformTime::Seconds(); // 真实时间同时覆盖暂停、动画与焦点落定。
    if(StartedAt==0) StartedAt=Now;
    if(Now-StartedAt>120.) { Check(false,TEXT("startup or UI state machine timed out")); return; }
    if(Now<Deadline) return;
    if(CurrentStage>=400) { TickCompletePresentation(); return; } // 最完整表现路径为独立可选分支，不替代原交易回归。
    if(CurrentStage>=300) { TickGameplaySpine(); return; } // 额外夹具只在明确参数及原输入恢复验证后进入。
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 每帧重新借用，控制器销毁不会留下裸指针。
    ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 当前 Avatar，夹具不替换正式 Pawn。
    AFPSDemoGameMode* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 实际服务器交易入口。
    ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 仅最后阶段变化夹具会修改公开阶段。
    if(!PC||!Player||!Mode||!State||GetWorld()->GetTimeSeconds()<1.) return;
    UDemoUIManager* UI=PC->FindComponentByClass<UDemoUIManager>(); // PC 生命周期内唯一 UI 协调器。
    UDemoMenuFlowComponent* Flow=PC->GetMenuFlow(); // 仍是页面意图真值，不由测试复制第二套状态。
    ADemoPlayerState* PlayerState=Player->GetPlayerState<ADemoPlayerState>(); // 真实拥有者，等待初始角色就绪。
    if(!PlayerState) return;
    UDemoPlayerProgressComponent* Progress=PlayerState->GetProgressComponent(); // 仅测试断言读取个人账本。
    if(!UI||!Flow||!Progress) { Check(false,TEXT("production controller UI components missing")); return; }
    UI->Sync();
    switch(CurrentStage)
    {
    case 0:
    {
        if(auto* Input=PC->GetLocalPlayer()->GetSubsystem<UCommonInputSubsystem>()) Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("CommonUIAutomation"),true); // 只在隔离测试LocalPlayer生效，进程退出释放，不写设备INI、不更改生产手柄规则。
        if(!Check(UI&&Flow&&Progress,TEXT("production controller UI components exist"))) return;
        UDemoRunSaves* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // WITH_EDITOR 每 GI 本地临时槽；退出进程清理。
        if(!Check(Saves&&Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("isolated checkpoint starts in hub"))) return;
        Saves->ConsumePending(); PC->OnRunReady();
        FDemoPlayerProgressSnapshot Data=Progress->GetSnapshot(); // 仅提供本专项预算，不修改结果或解锁。
        const bool bTestGambleHome=FParse::Param(FCommandLine::Get(),TEXT("DemoGambleHomeTest")); // 只在本Editor夹具选择关间银币场景。
        if(bTestGambleHome)
        {
            UDemoRunBuildComponent* Build=UDemoRunBuildComponent::FindForPawn(Player); // 当前隔离存档的个人流派组件。
            FDemoRunBuildSnapshot FirstIntermission; // 第一关清关后的合法流派，尚无分支；不覆盖抽取回执或经济结果。
            FirstIntermission.CoreId=TEXT("B01"); FirstIntermission.bLocked=true; FirstIntermission.Revision=1;
            if(!Check(Build&&Build->RestoreSnapshot(FirstIntermission),TEXT("fixture restores locked first-intermission core"))) return;
            State->Phase=EDemoPhase::Intermission; State->LevelNumber=1;
            Data.SilverCoins=1000;
        }
        else Data.Coins=4000;
        Progress->Commit(Data);
        Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())->SetGambleAnimationSkipped(false); // 调用方必须传独立设置 INI。
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // Capsule中心须高于终端地面，避免嵌入后物理解穿透将角色推出会话距离。
        Mode->GetShopTerminal()->Interact(Player);
        UI->Sync(); // 与后续Press助手一致：先让真实菜单构建，再开始跨帧等待；冷启动长帧不能把“刚创建”当作“已绘制/已获焦点”。
        Next(bTestGambleHome?60:1,.5); return;
    }
    case 60: // 用户日志里的天赋抽取，先跳过动画，再单击页首返回。
    case 61: // 同一终端再次抽取，改为滚动中返回；两种模式均走实际按钮与权威交易。
    {
        if(!Press(TEXT("TerminalGamble"))||!Press(TEXT("Gamble3"))||!Press(TEXT("GambleSkipAnimation"))) return;
        const auto& Offers=PC->GetMenuSnapshot().GambleOffers; // 断言使用真实服务端报价，兼容费用调整。
        if(!Check(Offers.IsValidIndex(3),TEXT("terminal publishes talent draw offer"))) return;
        const int32 Cost=Offers[3].Cost; // 确认前冻结，用于核对单次扣费。
        Wallet=Progress->GetSnapshot().SilverCoins; Serial=Progress->GetSnapshot().Gambling.Serial;
        if(!Press(TEXT("GambleAction"))||!Press(TEXT("GambleYes"))) return;
        const FDemoPlayerProgressSnapshot Committed=Progress->GetSnapshot(); // 整笔个人账本，包括天赋和升级来源，须与返回后相同。
        if(!Check(Committed.SilverCoins==Wallet-Cost&&Committed.Gambling.Serial==Serial+1&&!Flow->GetGambleReel().Cards.IsEmpty()
            &&Flow->GetGambleReel().bRolling==(CurrentStage==61),TEXT("one paid draw reached requested rolling or skipped result"))) return;
        const uint64 Generation=PC->GetMenuSnapshot().TerminalGeneration; // 当前权威会话，应持续到返回首页之后。
        if(!Press(TEXT("UI.Back"))) return;
        const UDemoMenuScreen* Home=UI->GetActiveScreen(); // 重新查询实际重建后的WBP及其首页购买按钮。
        if(!Check(Flow->IsUpgradeMenuOpen()&&!Flow->IsGambleMenuOpen()&&Flow->GetGambleReel().Cards.IsEmpty()
            &&Home&&Home->FindAction(TEXT("Upgrade0"))&&!Home->FindAction(TEXT("GambleContinue"))
            &&PC->GetMenuSnapshot().Terminal==Mode->GetShopTerminal()&&PC->GetMenuSnapshot().TerminalGeneration==Generation
            &&FDemoPlayerProgressSnapshot::StaticStruct()->CompareScriptStruct(&Committed,&Progress->GetSnapshot(),0),
            TEXT("header Back restores terminal home in same session and preserves entire committed progress"))) return;
        Next(CurrentStage+1); return;
    }
    case 62:
        if(!Press(TEXT("TerminalGamble"))||!Press(TEXT("Gamble3"))||!Press(TEXT("GambleAction"))||!Press(TEXT("GambleYes"))) return;
        if(!Check(Flow->GetGambleReel().bRolling,TEXT("natural reveal starts on returning to same terminal"))) return;
        Serial=Progress->GetSnapshot().Gambling.Serial; Wallet=Progress->GetSnapshot().SilverCoins;
        Next(63); return;
    case 63:
        if(Flow->GetGambleReel().bRolling) return; // 等正式Tick自然停靠；外层真实时间总超时会报告失败。
        Key(EKeys::Tab); Next(64); return;
    case 64:
        if(!Check(UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("Upgrade0"))&&Flow->IsUpgradeMenuOpen()&&!Flow->IsGambleMenuOpen()
            &&Progress->GetSnapshot().Gambling.Serial==Serial&&Progress->GetSnapshot().SilverCoins==Wallet,
            TEXT("keyboard Back from naturally settled result restores home without another charge"))) return;
        if(!Press(TEXT("TerminalGamble"))||!Press(TEXT("Gamble3"))||!Press(TEXT("GambleSkipAnimation"))
            ||!Press(TEXT("GambleAction"))||!Press(TEXT("GambleYes"))||!Press(TEXT("GambleNo"))) return;
        if(!Check(Flow->IsGambleMenuOpen()&&Flow->GetGambleReel().Cards.IsEmpty()&&UI->GetActiveScreen()&&UI->GetActiveScreen()->FindAction(TEXT("Gamble3")),
            TEXT("footer Back to list preserves adventure upgrades page"))) return;
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_GAMBLE_HOME_TEST_SUCCESS checks=%d"),Passed);
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); return;
    case 1:
    {
        if(!Check(UI->UsesWidgetMenu()&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0
            &&UI->GetActiveScreen()&&UI->GetActiveScreen()->IsActivated(),TEXT("terminal occupies real active CommonUI menu stack"))) return;
        if(!Check(UI->GetActiveScreen()->GetClass()->GetPathName()==TEXT("/Game/UI/Screens/WBP_Terminal.WBP_Terminal_C")
            &&UI->GetActiveScreen()->GetWidgetFromName(TEXT("PageBody"))&&UI->GetActiveScreen()->GetWidgetFromName(TEXT("PageHeading")),
            TEXT("active terminal is generated WBP instance with Designer body and heading bindings"))) return;
        if(!Check(UI->GetActiveScreen()->HasUserFocusedDescendants(PC)||UI->GetActiveScreen()->HasAnyUserFocus(),TEXT("active terminal receives real Slate user focus"))) return;
        if(!Check(PC->IsMoveInputIgnored()&&PC->IsLookInputIgnored(),TEXT("CommonUI menu retains movement and look input guards"))) return;
        const UTextBlock* Heading=Cast<UTextBlock>(UI->GetActiveScreen()->GetWidgetFromName(TEXT("PageHeading"))); // 实际WBP实例标题，不能只验证字体资产存在。
        if(!Check(Heading&&Heading->GetFont().FontObject&&Heading->GetFont().FontObject->GetPathName()==TEXT("/Game/UI/RedesignR04/Fonts/F_BreachText.F_BreachText"),TEXT("R04 actual WBP heading uses packaged Noto composite font instead of engine fallback"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-TerminalStats.png")),true,false);
        Next(101); return; // 截图在帧末完成，不能同一帧切页导致文件名与实际画面不符。
    }
    case 101:
        if(!Press(TEXT("TerminalCore"))) return;
        Next(201); return;
    case 201:
    {
        const UDemoTerminalScreen* Terminal=Cast<UDemoTerminalScreen>(UI->GetActiveScreen()); // 真实R02目录将七项预览与唯一当前确认分开。
        if(!Check(Flow->IsCoreMenuOpen()&&Terminal&&Terminal->FindAction(TEXT("UI.CorePreview0"))&&Terminal->FindAction(TEXT("UI.CorePreview6"))
            &&Terminal->GetPresentationCoreIndex()>=0&&Terminal->FindAction(FName(*FString::Printf(TEXT("Core%d"),Terminal->GetPresentationCoreIndex()))),
            TEXT("actual R02 seven-core preview directory exposes the current original core confirmation"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-Cores.png")),true,false);
        Next(202); return;
    }
    case 202:
        if(!Press(TEXT("TerminalWeapons"))) return;
        Next(2); return;
    case 2:
        if(!Check(Flow->IsWeaponMenuOpen(),TEXT("weapon tab preserves original Flow state"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-Weapons.png")),true,false);
        Next(203); return;
    case 203:
    {
        UWidget* Tabs=UI->GetActiveScreen()->GetWidgetFromName(TEXT("PageCategories")); // 已经过上一截图帧的真实页签几何。
        if(!Check(Tabs&&Tabs->GetCachedGeometry().GetLocalSize().X>0,TEXT("R04 weapon navigation has rendered geometry"))) return;
        WeaponTabsOrigin=Tabs->GetCachedGeometry().GetAbsolutePosition(); WeaponTabsSize=Tabs->GetCachedGeometry().GetAbsoluteSize();
        if(!Press(TEXT("Weapon0"))) return;
        Next(3); return;
    }
    case 3:
    {
        const UDemoMenuScreen* Screen=UI->GetActiveScreen(); // 固定手枪信息直接留目录中，用户删除的详情/返回层不可重建。
        const UDemoMenuButton* Equip=Screen?Screen->FindAction(TEXT("UI.WeaponEquip")):nullptr;
        if(!Check(UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0&&Flow->GetInspectedWeapon()==INDEX_NONE&&Equip&&!Equip->GetIsEnabled()
            &&!Screen->FindAction(TEXT("UI.WeaponDetails"))&&!Screen->FindAction(TEXT("WeaponTipClose")),TEXT("pistol preview stays inline with disabled primary equip and no removed weapon modal actions"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-WeaponInlinePistol.png")),true,false);
        Next(103); return;
    }
    case 103:
        if(!Check(!UI->PressActionForTest(TEXT("UI.WeaponEquip"))&&UI->GetModalStackCount()==0,TEXT("actual fixed-pistol disabled direct equip rejects without opening a modal"))) return;
        Next(4); return;
    case 4:
        if(!Check(UI->GetModalStackCount()==0&&UI->GetActiveScreen()->HasUserFocusedDescendants(PC),TEXT("inline weapon catalogue retains actual page focus without an extra Back step"))) return;
        if(!Press(TEXT("TerminalAmmo"))||!Press(TEXT("Ammo1"))) return;
        StaleButton=UI->GetActiveScreen()->FindAction(TEXT("AmmoAction"));
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-Ammo.png")),true,false);
        Next(204); return;
    case 204:
    {
        UWidget* Tabs=UI->GetActiveScreen()->GetWidgetFromName(TEXT("PageCategories")); // 弹药页已绘制后重新借用，不查询旧武器Widget。
        if(!Check(Tabs&&Tabs->GetCachedGeometry().GetAbsolutePosition().Equals(WeaponTabsOrigin,1.f)&&Tabs->GetCachedGeometry().GetAbsoluteSize().Equals(WeaponTabsSize,1.f),TEXT("R04 weapon and ammo tabs keep identical rendered screen anchors"))) return;
        if(!Press(TEXT("AmmoAction"))) return;
        Next(5); return;
    }
    case 5:
    {
        if(!Check(Flow->GetPendingAmmoPrice()>0&&UI->GetModalStackCount()==1,TEXT("ammo purchase quote opens real confirmation modal"))) return;
        if(!Check(SafeActionHasFocus(UI->GetActiveScreen(),TEXT("AmmoBuyNo"),PC),TEXT("ammo purchase defaults to actual Cancel focus"))) return;
        if(!Check(StaleButton&&!StaleButton->PressForTest()&&Progress->GetSnapshot().Coins==4000,TEXT("retained underlying ammo button cannot penetrate modal"))) return;
        UPanelWidget* Body=Cast<UPanelWidget>(UI->GetActiveScreen()->GetWidgetFromName(TEXT("PageBody"))); // 检查当前实际弹药模态的正文容器。
        bool IconAspectPreserved=false; // 必须找到含真实纹理且完成Slate布局的图标，缺失不能当作比例通过。
        if(Body) for(UWidget* Child:Body->GetAllChildren())
        {
            const UImage* Icon=Cast<UImage>(Child); // 当前页面直接添加图标，不假设自动对象名。
            const UTexture2D* Texture=Icon?Cast<UTexture2D>(Icon->GetBrush().GetResourceObject()):nullptr; // 源纹理宽高比是比较基线。
            if(!Texture) continue;
            const FVector2D Actual=Icon->GetCachedGeometry().GetLocalSize(); // Slate最终分配尺寸，能检出父槽Fill横拉，不是DesiredSize缓存。
            const double ExpectedRatio=static_cast<double>(Texture->GetSizeX())/FMath::Max(1,Texture->GetSizeY()); // 原图可能不是正方形。
            IconAspectPreserved=Actual.X>0&&Actual.Y>0&&FMath::Max(Actual.X,Actual.Y)<=72.1&&FMath::IsNearlyEqual(Actual.X/Actual.Y,ExpectedRatio,.01);
            UE_LOG(LogFPSDemo,Log,TEXT("COMMON_UI_ICON actual=%.2fx%.2f expectedRatio=%.4f"),Actual.X,Actual.Y,ExpectedRatio);
            break;
        }
        if(!Check(IconAspectPreserved,TEXT("rendered ammo confirmation icon preserves texture aspect and 72-unit maximum edge"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-AmmoConfirm.png")),true,false);
        Next(105); return;
    }
    case 105:
        if(!Press(TEXT("AmmoBuyNo"))) return;
        if(!Check(Flow->GetPendingAmmoPrice()==INDEX_NONE&&Progress->GetSnapshot().Coins==4000,TEXT("ammo cancel preserves quote semantics and wallet"))) return;
        if(!Press(TEXT("TerminalGamble"))) return;
        Next(6); return;
    case 6:
    {
        bool HasSeven=true; // 检查真实七个按钮，不仅检查菜单枚举/业务报价。
        for(int32 Index=0;Index<7;++Index) HasSeven&=UI->GetActiveScreen()->FindAction(FName(*FString::Printf(TEXT("Casino.Open.%d"),Index)))!=nullptr;
        if(!Check(HasSeven&&Flow->IsGambleMenuOpen(),TEXT("seven casino games have actual UMG action buttons"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-SevenGames.png")),true,false);
        Next(106); return;
    }
    case 106:
    {
        if(!Press(TEXT("Casino.Open.0"))||!Press(TEXT("Casino.Number.17"))) return;
        if(!Check(Flow->GetCasinoDraft().Low==17&&Flow->GetCasinoDraft().High==17,TEXT("number selection uses existing casino intent"))) return;
        if(!Press(TEXT("Casino.BetInput"))) return;
        Next(7,.35); return;
    }
    case 7:
    {
        if(!Check(Flow->IsCasinoBetInputOpen()&&UI->GetModalStackCount()==1&&UI->GetActiveScreen()->HasUserFocusedDescendants(PC),TEXT("amount modal owns real editable text focus"))) return;
        const auto* Entry=Cast<UEditableTextBox>(UI->GetActiveScreen()->GetWidgetFromName(TEXT("BetEntry"))); // 实际编辑框，不能在测试里强制重设焦点掩盖生产问题。
        if(!Check(Entry&&(Entry->HasUserFocus(PC)||Entry->HasUserFocusedDescendants(PC)),TEXT("actual amount entry rather than its Back action owns Slate focus"))) return;
        if(!Check(!UI->PressActionForTest(TEXT("Casino.Place"))&&!Flow->IsGambleConfirmOpen(),TEXT("hidden wager action rejected below amount modal"))) return;
        Type(TEXT("5001")); Next(8); return;
    }
    case 8:
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-BetInput.png")),true,false);
        Next(108); return;
    case 108:
        Key(EKeys::Enter); Next(9); return;
    case 9:
        if(!Check(!Flow->IsCasinoBetInputOpen()&&Flow->GetCasinoDraft().Stake==5001&&Progress->GetSnapshot().Coins==4000,TEXT("actual keyboard Enter edits uncapped wager without debit"))) return;
        if(!Press(TEXT("Casino.BetInput"))) return;
        Next(10); return;
    case 10:
        Type(TEXT("888")); Key(EKeys::Escape); Next(11); return;
    case 11:
        if(!Check(!Flow->IsCasinoBetInputOpen()&&Flow->GetCasinoDraft().Stake==5001&&Flow->IsGambleMenuOpen(),TEXT("actual Escape cancels text only and preserves terminal"))) return;
        if(!Press(TEXT("Casino.BetInput"))) return;
        Next(12); return;
    case 12:
        Type(TEXT("2147483648")); Key(EKeys::Enter); Next(13); return;
    case 13:
        if(!Check(Flow->IsCasinoBetInputOpen()&&Flow->GetCasinoDraft().Stake==5001,TEXT("overflow text remains editable and preserves prior draft"))) return;
        Key(EKeys::A,true); Type(TEXT("20")); Key(EKeys::Enter); Next(14); return;
    case 14:
        if(!Check(!Flow->IsCasinoBetInputOpen()&&Flow->GetCasinoDraft().Stake==20,TEXT("Ctrl+A replaces invalid text and Enter commits valid amount"))) return;
        StaleButton=UI->GetActiveScreen()->FindAction(TEXT("Casino.Place"));
        if(!Press(TEXT("Casino.Place"))) return;
        Next(15); return;
    case 15:
        if(!Check(UI->GetModalStackCount()==1&&Flow->IsGambleConfirmOpen(),TEXT("wager confirmation is a real activatable modal"))) return;
        if(!Check(SafeActionHasFocus(UI->GetActiveScreen(),TEXT("GambleNo"),PC),TEXT("wager confirmation defaults to actual Cancel without forced focus"))) return;
        if(!Check(StaleButton&&!StaleButton->PressForTest()&&Progress->GetSnapshot().Coins==4000,TEXT("retained parent wager button cannot double-submit behind confirmation"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-WagerConfirm.png")),true,false);
        Next(115); return;
    case 115:
    {
        UDemoMenuButton* Cancel=UI->GetActiveScreen()->FindAction(TEXT("GambleNo")); // 真实当前模态取消按钮；不强制重设默认焦点。
        if(!Check(Cancel&&Cancel->GetIsEnabled(),TEXT("actual wager cancel button can receive keyboard focus"))) return;
        if(!Check(SafeActionHasFocus(UI->GetActiveScreen(),TEXT("GambleNo"),PC),TEXT("safe default focus remains on Cancel across the rendered modal frame"))) return;
        Next(116); return;
    }
    case 116:
    {
        const UDemoMenuButton* Cancel=UI->GetActiveScreen()->FindAction(TEXT("GambleNo")); // 跨帧重新查找当前控件，避免焦点重建后引用旧实例。
        if(!Check(Cancel&&(Cancel->HasUserFocus(PC)||Cancel->HasUserFocusedDescendants(PC)),TEXT("cancel button owns real Slate focus before Enter"))) return;
        Key(EKeys::Enter); // 经真实 Slate/CommonUI 按下与松开处理内部点击委托，禁止替代为 PressForTest。
        Next(117); return;
    }
    case 117:
        if(!Check(!Flow->IsGambleConfirmOpen()&&Progress->GetSnapshot().Coins==4000,TEXT("real keyboard Enter activates cancel without transaction"))) return;
        if(!Press(TEXT("Casino.Place"))) return;
        Next(16); return;
    case 16:
    {
        UDemoShopComponent* Shop=Mode->FindComponentByClass<UDemoShopComponent>(); // 只替换生产保存委托，UI 仍提交真实服务器交易。
        if(!Check(Shop!=nullptr,TEXT("real shop save boundary is available"))) return;
        Wallet=Progress->GetSnapshot().Coins; Serial=Progress->GetSnapshot().Gambling.Serial;
        Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(this,&ADemoCommonUITest::RejectSave));
        const bool Clicked=Press(TEXT("GambleYes")); // 保存同步回调，下面无条件恢复正常委托。
        Shop->InitializeServices(Mode->GetTerminalSystem(),FDemoSaveCheckpointRequest::CreateUObject(Mode,&AFPSDemoGameMode::SaveCheckpoint));
        if(!Clicked) return;
        if(!Check(Progress->GetSnapshot().Coins==Wallet&&Progress->GetSnapshot().Gambling.Serial==Serial
            &&!Flow->IsGambleWaiting()&&Flow->GetCasinoResult().Mode==INDEX_NONE,TEXT("save failure rolls back wallet and serial without success animation"))) return;
        if(!Press(TEXT("Casino.Place"))) return;
        Next(17); return;
    }
    case 17:
        StaleButton=UI->GetActiveScreen()->FindAction(TEXT("GambleYes"));
        if(!Press(TEXT("GambleYes"))) return;
        if(!Check(Progress->GetSnapshot().Gambling.Serial==Serial+1&&Flow->GetCasinoResult().Mode==0&&Flow->IsCasinoRolling(),TEXT("actual confirm commits once before UMG reveal"))) return;
        if(!Check(StaleButton&&!StaleButton->PressForTest()&&Progress->GetSnapshot().Gambling.Serial==Serial+1,TEXT("retained old confirm cannot duplicate completed transaction"))) return;
        Next(18,.65); return;
    case 18:
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-NumberRolling.png")),true,false);
        if(!Check(!UI->PressActionForTest(TEXT("Casino.Place"))&&Progress->GetSnapshot().Gambling.Serial==Serial+1,TEXT("rolling state rejects stale wager action"))) return;
        Next(19,2.65); return;
    case 19:
        if(!Check(!Flow->IsCasinoRolling()&&UI->GetActiveScreen()->FindAction(TEXT("Casino.Again")),TEXT("settled result exposes real continue button"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // OnEndFrame检查与截图同帧的真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("CommonUI-NumberResult.png")),true,false);
        Next(119); return;
    case 119:
        if(!Press(TEXT("GambleSkipAnimation"))||!Press(TEXT("Casino.Again"))||!Press(TEXT("GambleYes"))) return;
        if(!Check(Flow->IsGambleAnimationSkipped()&&!Flow->IsCasinoRolling()&&Progress->GetSnapshot().Gambling.Serial==Serial+2,TEXT("continue and skip preserve one new committed result"))) return;
        Next(20); return;
    case 20:
        Key(EKeys::Tab); Next(21); return;
    case 21:
        if(!Check(Flow->IsGambleMenuOpen()&&Flow->GetInspectedGamble()==INDEX_NONE&&UI->GetMenuStackCount()==1&&UI->GetModalStackCount()==0,TEXT("first actual Tab returns casino detail to game list"))) return;
        Key(EKeys::Tab); Next(121); return;
    case 121:
        if(!Check(Flow->IsUpgradeMenuOpen()&&!Flow->IsGambleMenuOpen(),TEXT("second Tab returns game list to terminal home"))) return;
        Key(EKeys::Tab); Next(123); return;
    case 123:
        if(!Check(!Flow->IsUpgradeMenuOpen(),TEXT("third Tab closes terminal session"))) return;
        if(Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide)
        {
            if(!Check(Flow->HasBlockingOverlay()&&PC->bShowMouseCursor&&PC->IsMoveInputIgnored()&&PC->IsLookInputIgnored()
                &&Cast<UDemoSessionScreen>(UI->GetActiveScreen()),TEXT("CommonUI first-run guide retains menu input ownership"))) return;
            if(!Press(TEXT("OverlayBack"))) return;
            if(!Check(Flow->GetMenuPage()==EDemoMenuPage::None&&Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())->HasDismissedFirstRunGuide(),
                TEXT("UMG guide button records acknowledgement in isolated device settings"))) return;
        }
        Next(122); return; // 让CommonUI回交输入完成，再验证严格游戏输入恢复。
    case 122:
        if(!Check(!UI->UsesWidgetMenu()&&UI->GetMenuStackCount()==0&&UI->GetModalStackCount()==0,TEXT("all menu/guide stacks clear after final back"))) return;
        if(!Check(!PC->bShowMouseCursor&&!PC->IsMoveInputIgnored()&&!PC->IsLookInputIgnored(),TEXT("terminal close restores game input ownership"))) return;
        if(FParse::Param(FCommandLine::Get(),TEXT("DemoUICompletePresentation"))) { Next(400); return; }
        if(FParse::Param(FCommandLine::Get(),TEXT("DemoGameplayUISpine"))) { Next(300); return; }
        Mode->GetShopTerminal()->Interact(Player); Next(22); return;
    case 22:
        if(!Check(UI->UsesWidgetMenu(),TEXT("same terminal can reopen after CommonUI shutdown"))) return;
        StaleButton=UI->GetActiveScreen()->FindAction(TEXT("Upgrade0"));
        State->Phase=EDemoPhase::Combat; State->OnRep_RunView(); // 生命周期夹具走正式阶段发布通知，不能用只写字段且无通知的伪阶段验证 UI。
        Next(23,.35); return;
    case 23:
        if(!Check(!Flow->IsUpgradeMenuOpen()&&!UI->UsesWidgetMenu()&&UI->GetMenuStackCount()==0,TEXT("phase transition invalidates terminal and deactivates widgets"))) return;
        if(!Check(StaleButton&&!StaleButton->PressForTest(),TEXT("retained prior-phase purchase button is inert"))) return;
        State->Phase=EDemoPhase::Hub; State->OnRep_RunView(); Next(24); return;
    case 24:
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // 生命周期/表现夹具可能经历多秒非暂停；重新定位到安全高度，不能把下落远离终端误判为UI无法重开。
        Mode->GetShopTerminal()->Interact(Player); Next(25); return;
    case 25:
        if(!Check(UI->UsesWidgetMenu(),TEXT("hub lifecycle resumes valid terminal after phase reset"))) return;
        Mode->GetShopTerminal()->Destroy(); // 仅测试临时 World 的真实终端；生产 Flow 弱引用应主动撤销。
        Next(26,.35); return;
    case 26:
        if(!Check(!Flow->IsUpgradeMenuOpen()&&!UI->UsesWidgetMenu()&&UI->GetMenuStackCount()==0&&UI->GetModalStackCount()==0,TEXT("destroyed terminal clears pages and modal input ownership"))) return;
        StaleButton=nullptr;
        if(FParse::Param(FCommandLine::Get(),TEXT("DemoUICompletePresentation"))) UE_LOG(LogFPSDemo,Display,TEXT("DEMO_COMPLETE_UI_PRESENTATION_SUCCESS assertions=%d keyboard_mouse_local_transactions_actual_CDO_comparison; loading_rooms_spectator_stage_fixtures_only"),Passed);
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_COMMON_UI_SUCCESS assertions=%d keyboard_mouse_and_local_transactions_only"),Passed);
        SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); return;
    default:
        Check(false,TEXT("unexpected UI test stage")); return;
    }
#endif
}
