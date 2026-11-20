// 职责：Editor七玩法CommonUI/UMG实机回归与含UI截图。使用-DemoCasinoUITest、隔离GameUserSettingsINI和-DemoUIScreenshotDir；实际按钮触发事务，键盘走Slate焦点，不在生产注册。
#include "Tests/DemoViewReelTest.h"
#include "Tests/DemoUILayoutChecks.h" // 七玩法截图前验证真实操作区，禁止用请求尺寸代替渲染尺寸。
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Save/DemoRunSave.h"
#include "Interaction/DemoInteractable.h"
#include "UI/Common/DemoUIManager.h"
#include "UI/Common/DemoMenuScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Screens/DemoCasinoScreen.h" // 仅WITH_EDITOR只读真实控件探针；断言不镜像动画公式。
#include "Blueprint/WidgetTree.h"
#include "Components/EditableTextBox.h"
#include "CommonInputSubsystem.h" // 隔离专项仅注入键鼠，过滤后台实体手柄Back/焦点事件，不修改生产输入策略。
#include "Engine/LocalPlayer.h" // 输入过滤绑定本测试LocalPlayer，进程结束自动释放。
#include "GameFramework/PlayerInput.h" // 真实鼠标训练射击的InputKey参数。
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Settings/DemoGameUserSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "UnrealClient.h"
#include "Debug/DemoLog.h"
#include "Economy/Gambling/DemoCasinoRange.h"
#include "Economy/Gambling/DemoRangeRoom.h" // 测试真实房间地面及射击线碰撞，不能用位置距离代替场地可玩性。
#include "Components/PrimitiveComponent.h"
#include "Economy/Gambling/DemoCasinoRules.h" // 停靠仅比较权威符号名称，不在测试复制动画算法。
#include "Framework/Application/SlateApplication.h" // 测试真实原生文本事件，而非绕过输入控件设置草稿。
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"

#if WITH_EDITOR
void ADemoViewReelTest::TickCasino()
{
    DEMO_LOG_TICK();
    FString LayoutError; const int32 LayoutResult=DemoUILayoutChecks::ProcessPending(LayoutError); // 所有截图请求后一帧、任何等待判断之前检查。
    if(LayoutResult&&!Check(LayoutResult>0,*FString::Printf(TEXT("casino rendered layout: %s"),*LayoutError))) return;
    auto* PC=Cast<ADemoPlayerController>(UGameplayStatics::GetPlayerController(this,0)); // 此专项单人隔离进程。
    auto* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 每帧重新解析生命周期。
    auto* Mode=GetWorld()->GetAuthGameMode<AFPSDemoGameMode>(); // 权威实际GameMode。
    const double Now=FPlatformTime::Seconds(); // World可能被菜单暂停，UI用真实时间。
    if(GetWorld()->GetTimeSeconds()<1||!PC||!Player||!Mode||Now<Deadline) return;
    auto* Flow=PC->GetMenuFlow(); auto* UI=PC->FindComponentByClass<UDemoUIManager>(); // 真实CommonUI协调器，不构造替代页面。
    auto* PlayerState=Player->GetPlayerState<ADemoPlayerState>(); // 等待当前玩家状态就绪，旅行不缓存旧指针。
    if(!PlayerState) return;
    auto* Progress=PlayerState->GetProgressComponent(); // 测试可读私有雷位，生产UI不可读。
    // 捕获this/UI仅用于当前Tick的同步操作；真实CommonUI按钮须在顶层可交互，Expected=false检查隐层或禁用按钮拒绝。
    const auto Press=[this,UI](FName Action,bool Expected=true)->bool
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CasinoUITest.Press action=%s"),*Action.ToString());
        const bool Accepted=UI&&UI->PressActionForTest(Action); // 测试入口只能查找实际当前按钮，不直接调用Flow。
        if(UI) UI->Sync(); // 点击生命周期结束后同步模型，禁止在NativeOnClicked内部销毁正在执行的控件。
        return Check(Accepted==Expected,*FString::Printf(TEXT("real UMG action %s %s"),*Action.ToString(),Expected?TEXT("accepted"):TEXT("rejected")));
    };
    // 捕获this/UI仅在当前Tick使用；向实际UEditableTextBox注入粘贴结果夹具，不修改系统剪贴板，不直接写下注草稿。
    const auto Paste=[this,UI](const TCHAR* Text)->bool
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CasinoUITest.Paste"));
        UDemoMenuScreen* Screen=UI?UI->GetActiveScreen():nullptr; // 当前顶层必须是已生成的金额输入页。
        UEditableTextBox* Entry=Screen&&Screen->WidgetTree?Cast<UEditableTextBox>(Screen->WidgetTree->FindWidget(TEXT("BetEntry"))):nullptr; // 只取得真正控件。
        if(!Check(Entry!=nullptr,TEXT("paste payload targets real UMG editable text"))) return false;
        Entry->SetText(FText::FromString(Text)); return true; // 解析和提交必须由后续实际确定按钮触发。
    };
    // 同步无捕获键盘助手，事件交给Slate焦点路由；没有系统剪贴板修改或跨帧指针。
    const auto Type=[](const TCHAR* Digits)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CasinoUITest.Type")); for(const TCHAR* Char=Digits;*Char;++Char) FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(*Char,FModifierKeysState(),0,false)); };
    // 同步无捕获，仅按下/松开指定键；Enter/Esc用生产回调，不直接调用输入层实现。
    const auto Key=[](FKey Value)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CasinoUITest.Key")); FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Value,FModifierKeysState(),0,false,0,0)); FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Value,FModifierKeysState(),0,false,0,0)); };
    // 捕获this/UI仅同步读取顶层结果页；Out是调用栈快照，读取不会刷新/推进动画，禁用NativeTick必须使跨帧断言失败。
    const auto ReadAnimation=[this,UI](FDemoCasinoAnimationProbe& Out)->bool
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CasinoUITest.ReadAnimation"));
        const UDemoCasinoScreen* Screen=UI?Cast<UDemoCasinoScreen>(UI->GetActiveScreen()):nullptr; // 必须是真实当前赌场页，不创建测试控件。
        if(!Check(Screen!=nullptr,TEXT("animation sample comes from active real casino screen"))) return false;
        Out=Screen->ReadAnimationForTest();
        return Check(Out.ContentId!=0,TEXT("animation sample contains actual result widget identity"));
    };
    if(!Check(UI!=nullptr,TEXT("casino production CommonUI manager ready"))) return;
    UI->Sync();
    switch(Stage)
    {
    case 0:
    {
        if(auto* Input=PC->GetLocalPlayer()->GetSubsystem<UCommonInputSubsystem>()) Input->SetInputTypeFilter(ECommonInputType::Gamepad,TEXT("CasinoUIAutomation"),true); // 只限当前隔离LocalPlayer，不写设备INI、不改变真实手柄验收范围。
        Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())->SetGambleAnimationSkipped(false); // 仅测试指定独立INI。
        Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())->DismissFirstRunGuide(); // 仅此隔离七玩法夹具已读；首次提示与CommonUI/Canvas交接由DemoCommonUITest真实按钮专项验证，不能在靶场关闭菜单后插入教学阻塞射击。
        auto* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 临时槽停止自动删除。
        if(!Check(Saves->CreateSlot(0)&&Mode->StartRun(),TEXT("casino UI temporary slot and hub ready"))) return;
        Saves->ConsumePending(); PC->OnRunReady();
        auto Data=Progress->GetSnapshot(); Data.Coins=4000; Progress->Commit(Data); // 夹具预算，不授予解锁或云权限。
        Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // Capsule中心须高于终端地面，避免嵌入后解穿透推出真实会话距离。
        Mode->GetShopTerminal()->Interact(Player); if(!Press(TEXT("TerminalGamble"))) return;
        if(FParse::Param(FCommandLine::Get(),TEXT("DemoCasinoRangeVisualOnly"))) { if(!Press(TEXT("Casino.Open.6"))) return; Stage=32; Deadline=Now+.4; return; } // 仅Editor靶场视觉修复回归，日志不冒充完整七玩法断言数。
        for(int32 HelpMode=0;HelpMode<7;++HelpMode) // 实际说明按钮与返回，不能仅检查字符串表存在。
        {
            if(!Press(FName(*FString::Printf(TEXT("Casino.Open.%d"),HelpMode)))||!Press(TEXT("Casino.Help"))) return;
            auto* Help=Cast<UDemoCasinoScreen>(UI->GetActiveScreen()); // 当前真实激活页。
            if(!Check(Help&&Help->IsHelpOpen()&&!Help->FindAction(TEXT("Casino.Place")),TEXT("help layer hides betting controls"))) return;
            if(!Press(TEXT("UI.Back"))) return;
            if(!Check(!Help->IsHelpOpen()&&Flow->GetCasinoDraft().Mode==HelpMode&&Progress->GetSnapshot().Coins==4000&&Progress->GetSnapshot().Gambling.Serial==0,TEXT("help back preserves selected mode and economy"))) return;
            if(!Press(TEXT("Casino.Back"))) return;
        }
        if(!Press(TEXT("Casino.Open.0"))) return;
        if(!Press(TEXT("Casino.Number.17"))) return; if(!Press(TEXT("Casino.BetUp"))) return;
        if(!Check(Flow->GetCasinoDraft().Low==17&&Flow->GetCasinoDraft().High==17&&Flow->GetCasinoDraft().Stake==21,TEXT("casino number grid and custom amount route correctly"))) return;
        if(!Press(TEXT("Casino.Odd"))) return;
        if(!Check(Flow->GetCasinoDraft().Selection==1,TEXT("casino odd selection works"))) return;
        if(!Press(TEXT("Casino.Small"))) return; if(!Press(TEXT("Casino.HighDown"))) return; if(!Press(TEXT("Casino.Preset.1"))) return;
        if(!Check(Flow->GetCasinoDraft().High==48&&Flow->GetCasinoDraft().Stake==20,TEXT("casino custom range endpoints and presets work"))) return;
        if(!Press(TEXT("Casino.BetInput"))) return;
        if(!Check(Flow->IsCasinoBetInputOpen(),TEXT("amount field opens actual UMG edit modal"))) return;
        Stage=10; Deadline=Now+.4; return;
    }
    case 1:
        if(!Press(TEXT("Casino.Place"))) return; if(!Press(TEXT("GambleNo"))) return;
        if(!Check(Progress->GetSnapshot().Coins==4000&&!Flow->IsGambleConfirmOpen(),TEXT("casino cancel preserves wallet"))) return;
        if(!Press(TEXT("Casino.Place"))) return; if(!Press(TEXT("GambleYes"))) return;
        if(!Check(Flow->GetCasinoResult().Mode==0&&Flow->GetCasinoRevealTime()<3.,TEXT("casino numeric result committed before UI reveal"))) return;
        Serial=Progress->GetSnapshot().Gambling.Serial;
        if(!Press(TEXT("Casino.Place"),false)) return;
        if(!Check(Progress->GetSnapshot().Gambling.Serial==Serial,TEXT("casino animation blocks repeat wager"))) return;
        if(!ReadAnimation(CasinoAnimation)) return;
        if(!Check(PC->SetLocalMenuPaused(true),TEXT("single-player pause fixture starts after committed result"))) return; // 只验证UI真实时钟，不打开覆盖本页的Esc菜单。
        Stage=101; Deadline=FPlatformTime::Seconds()+.24; return;
    case 101:
    {
        FDemoCasinoAnimationProbe Current; // 同一结果文本跨真正Slate帧采样，排除结束重建造成的变化。
        if(!ReadAnimation(Current)) return;
        if(!Check(GetWorld()->IsPaused()&&Current.ContentId==CasinoAnimation.ContentId&&Current.Value!=CasinoAnimation.Value,
            TEXT("actual UMG number text changes on same widget while world paused"))) return;
        Stage=2; Deadline=FPlatformTime::Seconds()+3.; return;
    }
    case 2:
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-NumberResult.png")),true,false);
        if(!Check(GetWorld()->IsPaused()&&Flow->GetCasinoRevealTime()>=3.,TEXT("casino reveal advances while world paused"))) return;
        PC->SetLocalMenuPaused(false); // 暂停夹具结束，后续靶场仍使用真实正式流程。
        ++Stage; Deadline=Now+.4; return;
    case 3:
        if(!Press(TEXT("Casino.Back"))) return; if(!Press(TEXT("Casino.Open.1"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-Slots.png")),true,false);
        ++Stage; Deadline=Now+.4; return;
    case 4:
        if(!Press(TEXT("Casino.Place"))) return; if(!Press(TEXT("GambleYes"))) return;
        if(!Check(Flow->GetCasinoResult().Symbols.Num()==3,TEXT("casino slot keeps all three authoritative targets"))) return;
        if(!ReadAnimation(CasinoAnimation)||!Check(CasinoAnimation.ReelTranslations.Num()==12,TEXT("three reels own twelve actual moving UMG cells"))) return;
        Stage=40; Deadline=FPlatformTime::Seconds()+.45; return;
    case 40:
    {
        FDemoCasinoAnimationProbe Current; // 初次停靠之前比较各轴中央复用格，不推算任何位置公式。
        if(!ReadAnimation(Current)) return;
        bool Moved=Current.ContentId==CasinoAnimation.ContentId&&Current.ReelTranslations.Num()==12; // 必须保留同一控件树。
        if(Moved) for(int32 Reel=0;Reel<3;++Reel) Moved&=!Current.ReelTranslations[Reel*4+1].Equals(CasinoAnimation.ReelTranslations[Reel*4+1],.001); // 三轴分别必须有真实位移。
        if(!Check(Moved,TEXT("all three existing UMG reels physically move across rendered frames"))) return;
        bool Readable=Current.ReelCellSizes.Num()==12&&Current.ReelLabelSizes.Num()==12&&Current.ReelLabelWrap.Num()==12; // 使用渲染后的实际几何，不能仅检查SizeBox配置值。
        if(Readable) for(int32 Reel=0;Reel<3;++Reel)
        {
            const int32 VisibleCell=Reel*4+1; // 各轴当前中心复用格，其标签始终位于裁切窗口内。
            Readable&=Current.ReelCellSizes[VisibleCell].X>=140.f&&Current.ReelLabelSizes[VisibleCell].X>=120.f
                &&Current.ReelLabelSizes[VisibleCell].Y>0.f&&Current.ReelLabelSizes[VisibleCell].Y<=40.f&&!Current.ReelLabelWrap[VisibleCell];
        }
        if(!Check(Readable,TEXT("three real reel cells retain readable width and single-line Chinese labels"))) return;
        Stage=5; Deadline=FPlatformTime::Seconds()+1.55; return;
    }
    case 5:
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-SlotRolling.png")),true,false);
        if(!Check(Flow->GetCasinoRevealTime()>1.7&&Flow->GetCasinoRevealTime()<3.,TEXT("casino sequential reels have distinct stopping time"))) return;
        ++Stage; Deadline=Now+1.2; return;
    case 6:
    {
        FDemoCasinoAnimationProbe Current; // 最终控件标签必须是权威回执；只核对结果，不复写减速/循环算法。
        if(!ReadAnimation(Current)) return;
        const auto& Result=Flow->GetCasinoResult(); // 当前已保存的三个符号。
        bool Landed=Current.ReelLabels.Num()==12&&Result.Symbols.Num()==3;
        if(Landed) for(int32 Reel=0;Reel<3;++Reel) Landed&=Current.ReelLabels[Reel*4+1]==DemoCasino::SymbolName(Result.Symbols[Reel]);
        if(!Check(Landed,TEXT("actual three reel labels settle on authoritative symbols"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-SlotResult.png")),true,false);
        ++Stage; Deadline=Now+.4; return;
    }
    case 7:
        Serial=Progress->GetSnapshot().Gambling.Serial;
        if(!Press(TEXT("Casino.Again"))) return; if(!Press(TEXT("GambleYes"))) return; if(!Press(TEXT("GambleSkipAnimation"))) return;
        if(!Check(Progress->GetSnapshot().Gambling.Serial==Serial+1&&Flow->GetCasinoRevealTime()>=3.,TEXT("casino continue charges once and skip immediately reveals"))) return;
        if(!Press(TEXT("Casino.Back"))) return; if(!Press(TEXT("Casino.Open.2"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-MineOptions.png")),true,false);
        ++Stage; Deadline=Now+.4; return;
    case 8:
    {
        if(!Press(TEXT("Casino.Place"))) return; if(!Press(TEXT("GambleYes"))) return;
        const auto Mine=Progress->GetSnapshot().Gambling.Mine; // 测试只据此选安全格，不让生产Widget读取答案。
        if(!Check(Mine.Stake>0&&PC->GetMenuSnapshot().Progress.Gambling.Mine.BombMask==0,TEXT("casino mine UI opens without private board"))) return;
        int32 Tile=0; while(Mine.BombMask&(1<<Tile)) ++Tile; // 确定性夹具点击实际CommonUI按钮。
        if(!Press(FName(*FString::Printf(TEXT("Casino.Tile.%d"),Tile)))) return;
        if(!Check(Progress->GetSnapshot().Gambling.Mine.OpenCount()==1,TEXT("casino tile click saves safe reveal"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-MinePending.png")),true,false);
        ++Stage; Deadline=Now+.4; return;
    }
    case 9:
        if(!Press(TEXT("Close"))) return; Mode->GetShopTerminal()->Interact(Player); if(!Press(TEXT("TerminalGamble"))) return; if(!Press(TEXT("Casino.Open.2"))) return;
        if(!Check(PC->GetMenuSnapshot().Progress.Gambling.Mine.OpenCount()==1,TEXT("casino UI reopens same pending box"))) return;
        if(!Press(TEXT("Casino.Cash"))) return;
        if(!Check(Progress->GetSnapshot().Gambling.Mine.Stake==0&&Flow->GetCasinoResult().Paid>0,TEXT("casino cash button credits and ends box"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-MineResult.png")),true,false);
        Stage=20; Deadline=Now+.4; return; // 10..15是开局输入夹具，货箱结束直接进入爆炸覆盖，不重复输入前置断言。
    case 10:
        if(!Check(UI->GetModalStackCount()==1&&UI->GetActiveScreen()&&UI->GetActiveScreen()->HasUserFocusedDescendants(PC),TEXT("UMG amount field owns real Slate keyboard focus"))) return;
        Type(TEXT("5001")); // 原金额已全选，新输入必须替换而非追加。
        Stage=13; Deadline=Now+.2; return; // Slate渲染晚于游戏Tick；截图前等待控件实际呈现新文本。
    case 13:
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-BetInput.png")),true,false); // 必须包含Slate层，确保截图记录实际UMG编辑控件。
        Stage=11; Deadline=Now+.4; return;
    case 11:
    {
        Key(EKeys::Enter);
        if(!Check(!Flow->IsCasinoBetInputOpen()&&Flow->GetCasinoDraft().Stake==5001&&Progress->GetSnapshot().Coins==4000,TEXT("native typing and Enter update amount without charging"))) return;
        if(!Press(TEXT("Casino.Number.17"))) return;
        if(!Check(Flow->GetCasinoDraft().Stake==5001,TEXT("single number switch keeps uncapped wager"))) return;
        if(!Press(TEXT("Casino.BetInput"))) return;
        Stage=12; Deadline=Now+.25; return; // 新模态先经过一次真实Slate布局和焦点应用，不能同Tick假定已经收到焦点。
    }
    case 12:
    {
        Type(TEXT("888")); Key(EKeys::Escape);
        if(!Check(!Flow->IsCasinoBetInputOpen()&&Flow->GetCasinoDraft().Stake==5001&&Flow->IsGambleMenuOpen(),TEXT("Esc cancels text without closing terminal or changing amount"))) return;
        if(!Press(TEXT("Casino.BetInput"))) return;
        Stage=14; Deadline=Now+.25; return;
    }
    case 14:
    {
        if(!Paste(TEXT("2147483648"))||!Press(TEXT("Casino.BetAccept"))) return;
        if(!Check(Flow->IsCasinoBetInputOpen()&&Flow->GetCasinoDraft().Stake==5001,TEXT("overflow text stays editable and preserves previous amount"))) return;
        if(!Paste(TEXT("2147483647"))||!Press(TEXT("Casino.BetAccept"))) return;
        if(!Check(!Flow->IsCasinoBetInputOpen()&&Flow->GetCasinoDraft().Stake==MAX_int32,TEXT("storage maximum accepted through actual amount button"))) return;
        if(!Press(TEXT("Casino.BetUp10"))) return;
        if(!Check(Flow->GetCasinoDraft().Stake==MAX_int32,TEXT("large amount increment never wraps negative"))) return;
        if(!Press(TEXT("Casino.BetInput"))) return;
        Stage=15; Deadline=Now+.25; return;
    }
    case 15:
    {
        if(!Press(TEXT("Casino.Place"),false)) return;
        if(!Check(!Flow->IsGambleConfirmOpen()&&Progress->GetSnapshot().Coins==4000,TEXT("amount modal blocks underlying UMG wager"))) return;
        if(!Paste(TEXT(" 20 "))||!Press(TEXT("Casino.BetAccept"))) return;
        if(!Check(Flow->GetCasinoDraft().Stake==20&&!Flow->IsCasinoBetInputOpen(),TEXT("trimmed numeric paste supported by actual amount button"))) return;
        if(!Press(TEXT("Casino.Small"))) return; if(!Press(TEXT("Casino.HighDown"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-Numbers.png")),true,false);
        Stage=1; Deadline=Now+.4; return;
    }
    case 20:
    {
        if(!Press(TEXT("Casino.Back"))) return; if(!Press(TEXT("Casino.Open.2"))) return;
        if(!Press(TEXT("Casino.Place"))) return; if(!Press(TEXT("GambleYes"))) return;
        if(!Press(TEXT("GambleSkipAnimation"))) return; // 通过实际偏好按钮恢复动画，不在正式回归中直接改设置。
        int32 Tile=0; while(!(Progress->GetSnapshot().Gambling.Mine.BombMask&(1<<Tile))) ++Tile; // 仅测试读取私有雷位来覆盖爆炸UI，不增加生产提示。
        if(!Press(FName(*FString::Printf(TEXT("Casino.Tile.%d"),Tile)))) return;
        if(!Check(Flow->GetCasinoResult().bBomb&&Flow->IsCasinoRolling(),TEXT("mine bomb starts committed UI explosion"))) return;
        if(!ReadAnimation(CasinoAnimation)||!Check(CasinoAnimation.ExplosionScales.Num()==24&&CasinoAnimation.ExplosionTranslations.Num()==24&&CasinoAnimation.ExplosionOpacities.Num()==24,
            TEXT("explosion owns twenty-four real UMG images"))) return;
        Stage=21; Deadline=FPlatformTime::Seconds()+.28; return;
    }
    case 21:
    {
        FDemoCasinoAnimationProbe Current; // 核心变大/变淡与火星移动均来自真实Image，不读Time推断。
        if(!ReadAnimation(Current)) return;
        const bool Pieces=Current.ContentId==CasinoAnimation.ContentId&&Current.ExplosionScales.Num()==24&&Current.ExplosionTranslations.Num()==24&&Current.ExplosionOpacities.Num()==24;
        if(!Check(Pieces&&Current.ExplosionScales[0].X>CasinoAnimation.ExplosionScales[0].X
            &&Current.ExplosionOpacities[0]<CasinoAnimation.ExplosionOpacities[0]
            &&!Current.ExplosionTranslations[4].Equals(CasinoAnimation.ExplosionTranslations[4],.001),
            TEXT("same UMG explosion images expand fade and move across rendered frames"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-MineExplosion.png")),true,false);
        if(!Press(TEXT("Casino.Again"),false)) return;
        if(!Check(!Flow->IsGambleConfirmOpen(),TEXT("explosion blocks stale repeated wager"))) return;
        Stage=22; Deadline=Now+3.; return;
    }
    case 22:
        if(!Press(TEXT("Casino.Back"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-SevenGames.png")),true,false);
        Stage=23; Deadline=Now+.35; return;
    case 23:
        if(!Press(TEXT("Casino.Open.3"))) return; if(!Press(TEXT("Casino.Place"))) return; if(!Press(TEXT("GambleYes"))) return;
        if(!Check(Progress->GetSnapshot().Gambling.Arcade.Mode==3,TEXT("reactor dynamic menu opens saved contract"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-Reactor.png")),true,false);
        Stage=24; Deadline=Now+.4; return;
    case 24:
        if(!Press(TEXT("Casino.Advance"))) return;
        if(Progress->GetSnapshot().Gambling.Arcade.Mode==3) if(!Press(TEXT("Casino.Collect"))) return;
        if(!Check(Progress->GetSnapshot().Gambling.Arcade.Mode<0,TEXT("reactor charge or overload resolves via real buttons"))) return;
        Stage=25; Deadline=Now+3.2; return;
    case 25:
        if(!Press(TEXT("Casino.Back"))) return; if(!Press(TEXT("Casino.Open.4"))) return; if(!Press(TEXT("Casino.Place"))) return; if(!Press(TEXT("GambleYes"))) return;
        if(!Press(TEXT("Casino.Keep.0"))) return; if(!Press(TEXT("Casino.Keep.2"))) return;
        if(!Check(Flow->GetCasinoDraft().KeepMask==5&&Progress->GetSnapshot().Gambling.Arcade.Values.Num()==5,TEXT("dice UI keeps selected positions without rerolling early"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-DiceKeep.png")),true,false);
        Stage=26; Deadline=Now+.4; return;
    case 26:
        if(!Press(TEXT("Casino.Advance"))) return;
        if(!Check(Flow->GetCasinoResult().Symbols.Num()==5&&Progress->GetSnapshot().Gambling.Arcade.Mode<0,TEXT("dice reroll button resolves exactly one final hand"))) return;
        if(!ReadAnimation(CasinoAnimation)||!Check(CasinoAnimation.DiceValues.Num()==5,TEXT("dice result has five real text widgets"))) return;
        Stage=126; Deadline=FPlatformTime::Seconds()+.17; return;
    case 126:
    {
        FDemoCasinoAnimationProbe Current; // 保留0/2的夹具从权威回执核对；其余骰只验证实际文本有变化。
        if(!ReadAnimation(Current)) return;
        const auto& Result=Flow->GetCasinoResult(); // 已保存最终骰面，UI不能修改。
        bool Kept=Current.ContentId==CasinoAnimation.ContentId&&Current.DiceValues.Num()==5&&Result.Symbols.Num()==5,Changed=false;
        if(Kept) for(int32 Index=0;Index<5;++Index)
        {
            if(Index==0||Index==2) Kept&=Current.DiceValues[Index]==FString::FromInt(Result.Symbols[Index])&&Current.DiceValues[Index]==CasinoAnimation.DiceValues[Index];
            else Changed|=Current.DiceValues[Index]!=CasinoAnimation.DiceValues[Index];
        }
        if(!Check(Kept&&Changed,TEXT("actual kept dice stay fixed while unkept dice animate across frames"))) return;
        Stage=27; Deadline=FPlatformTime::Seconds()+3.; return;
    }
    case 27:
    {
        FDemoCasinoAnimationProbe Current; // 结束后五个文字控件全部应与已保存骰面相符。
        if(!ReadAnimation(Current)) return;
        const auto& Result=Flow->GetCasinoResult(); // 最终展示契约。
        bool Landed=Current.DiceValues.Num()==5&&Result.Symbols.Num()==5;
        if(Landed) for(int32 Index=0;Index<5;++Index) Landed&=Current.DiceValues[Index]==FString::FromInt(Result.Symbols[Index]);
        if(!Check(Landed,TEXT("five actual dice text widgets settle on authoritative hand"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-DiceResult.png")),true,false);
        Stage=28; Deadline=Now+.35; return;
    }
    case 28:
        if(!Press(TEXT("Casino.Back"))) return; if(!Press(TEXT("Casino.Open.5"))) return; if(!Press(TEXT("Casino.Side.1"))) return;
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-ArenaOdds.png")),true,false);
        Stage=29; Deadline=Now+.35; return;
    case 29:
        if(!Press(TEXT("Casino.Place"))) return; if(!Press(TEXT("GambleYes"))) return;
        if(!Check(!Flow->GetCasinoResult().Battle.IsEmpty(),TEXT("arena has real battle replay before animation"))) return;
        if(!ReadAnimation(CasinoAnimation)||!Check(CasinoAnimation.ArenaHealth.Num()==2,TEXT("arena replay owns two real UMG health bars"))) return;
        Stage=30; Deadline=FPlatformTime::Seconds()+1.2; return;
    case 30:
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-ArenaCombat.png")),true,false);
        Stage=130; Deadline=FPlatformTime::Seconds()+1.4; return;
    case 130:
    {
        FDemoCasinoAnimationProbe Current; // 接近尾段但仍在3秒结果重建之前，避免起手连续闪避造成不稳定断言。
        if(!ReadAnimation(Current)) return;
        const bool Bars=Current.ContentId==CasinoAnimation.ContentId&&Current.ArenaHealth.Num()==2;
        if(!Check(Bars&&Current.ArenaHealth[0]>=0.f&&Current.ArenaHealth[1]>=0.f
            &&Current.ArenaHealth[0]<=CasinoAnimation.ArenaHealth[0]&&Current.ArenaHealth[1]<=CasinoAnimation.ArenaHealth[1]
            &&Current.ArenaHealth[0]+Current.ArenaHealth[1]<CasinoAnimation.ArenaHealth[0]+CasinoAnimation.ArenaHealth[1],
            TEXT("same actual arena health bars visibly lose health during replay"))) return;
        Stage=31; Deadline=FPlatformTime::Seconds()+.6; return;
    }
    case 31:
        if(!Press(TEXT("Casino.Back"))) return; if(!Press(TEXT("Casino.Open.6"))) return;
        if(!Press(TEXT("Casino.Help"))) return; Stage=131; Deadline=Now+.35; return;
    case 131:
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-RangeHelp.png")),true,false); Stage=132; Deadline=Now+.35; return;
    case 132:
        if(!Press(TEXT("UI.Back"))) return; Stage=133; Deadline=Now+.35; return;
    case 133:
        DemoUILayoutChecks::Queue(UI->GetActiveScreen()); // 截图请求后的下一帧读取真实几何。
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-RangeRules.png")),true,false); Stage=32; Deadline=Now+.35; return;
    case 32:
        RangeReturnLocation=Player->GetActorLocation(); // 记录实际入场位置，不能从靶场实现读取预期值。
        if(!Press(TEXT("Casino.Place"))) return; if(!Press(TEXT("GambleYes"))) return;
        if(!Check(!Flow->IsGambleMenuOpen()&&ADemoCasinoRange::Find(Player),TEXT("range confirmation closes UI and creates actual targets"))) return;
        Stage=33; Deadline=Now+.5; return;
    case 33:
    {
        if(!Check(FVector::Dist(Player->GetActorLocation(),RangeReturnLocation)>10000.f,TEXT("range enters physically isolated scene"))) return;
        FHitResult Barrier,Floor; FCollisionQueryParams Query(SCENE_QUERY_STAT(RangeRoomUIRegression),false,Player); // 真实场景碰撞，忽略玩家自身；不改对象或期望数据。
        const FVector Start=Player->GetActorLocation(); // 入场后真实胶囊中心，已受重力落地。
        GetWorld()->LineTraceSingleByChannel(Barrier,Start,Start+FVector(800,0,0),ECC_Pawn,Query);
        GetWorld()->LineTraceSingleByChannel(Floor,Start,Start-FVector(0,0,200),ECC_Visibility,Query);
        if(!Check(Barrier.GetComponent()&&Barrier.GetComponent()->GetFName()==TEXT("ShootingLine")&&Cast<ADemoRangeRoom>(Floor.GetActor())&&Floor.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block,TEXT("real room has walkable floor and Pawn-blocking shooting line"))) return;
        FScreenshotRequest::RequestScreenshot(DemoUILayoutChecks::ScreenshotPath(TEXT("Casino-RangeWorld.png")),true,false);
        Stage=36; Deadline=Now+.35; return; // 帧末截图先完成，再触发真实射击，不把开枪后的画面误标为入口截图。
    }
    case 36:
    {
        const ADemoCasinoRange* Range=ADemoCasinoRange::Find(Player); // 正式已保存开局生成的目标，不创建测试替身。
        if(!Check(Range!=nullptr,TEXT("real training target survives UI dismissal"))) return;
        FVector Eye; FRotator View; Player->GetActorEyesViewPoint(Eye,View); // 真实Avatar射线起点；不移动训练靶或伪造命中。
        PC->SetControlRotation((Range->GetActorLocation()-Eye).Rotation());
        PC->InputKey(FInputKeyParams(EKeys::LeftMouseButton,IE_Pressed,1.)); // 通过现有EnhancedInput→武器入口→GAS训练射击。
        Stage=37; Deadline=Now+.25; return;
    }
    case 37:
        PC->InputKey(FInputKeyParams(EKeys::LeftMouseButton,IE_Released,0.));
        if(!Check(Progress->GetSnapshot().Gambling.Arcade.Shots==1,TEXT("actual left click records one saved training shot"))) return;
        if(FParse::Param(FCommandLine::Get(),TEXT("DemoCasinoRangePauseTest"))) // 仅Editor专项：验证真实时限与返回不被单人暂停冻结。
        {
            Flow->EscapePressed();
            if(!Check(GetWorld()->IsPaused()&&Flow->HasBlockingOverlay(),TEXT("range test pauses real World before deadline"))) return;
        }
        Stage=34; Deadline=Now+31.; return;
    case 34:
        if(!Check(Progress->GetSnapshot().Gambling.Arcade.Mode<0,TEXT("range timeout automatically saves settlement without terminal"))) return;
        Stage=35; Deadline=Now+3.5; return;
    case 35:
        if(!Check(!ADemoCasinoRange::Find(Player)&&FVector::Dist2D(Player->GetActorLocation(),RangeReturnLocation)<5.f,TEXT("settlement destroys target and returns player to original hub position"))) return;
        if(FParse::Param(FCommandLine::Get(),TEXT("DemoCasinoRangePauseTest"))) Flow->CloseOverlay(); // 验证完成后恢复此测试创建的暂停覆盖层。
        Stage=38; Deadline=Now+.2; return;
    default:
        PC->ConsoleCommand(TEXT("Demo.Debug.Casino.Simulate 12345"),true); // 实际注册回调只模拟，不覆盖正在玩的结果。
        UE_LOG(LogFPSDemo,Display,TEXT("DEMO_CASINO_UI_SUCCESS framework=CommonUI scope=%s"),FParse::Param(FCommandLine::Get(),TEXT("DemoCasinoRangeVisualOnly"))?TEXT("range-only"):TEXT("seven-games")); SetActorTickEnabled(false); FPlatformMisc::RequestExitWithStatus(false,0); return;
    }
}
#endif
