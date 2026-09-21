#include "UI/DemoHUD.h"
#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"

namespace SessionUI
{
    // 与战斗HUD相同色系；单独定义供本翻译单元使用，无运行时状态。
    const FLinearColor Surface(FColor(24,38,45));
    const FLinearColor Ink(FColor(230,240,243));
    const FLinearColor Muted(FColor(154,178,188));
    const FLinearColor Accent(FColor(96,219,204));
    const FLinearColor Gold(FColor(248,208,118));
}
void ADemoHUD::DrawSessionMenu(ADemoPlayerController* PC, const ADemoGameState* State)
{
    DEMO_LOG_TICK();
    if (State->Phase == EDemoPhase::Lobby) DrawLobbyBackground();
    Panel(0,0,ViewWidth,ViewHeight,FLinearColor(0,0,0,.65f),0);
    // 所有子页共用居中720×520布局，最小4:3视口按UIScale等比缩放。
    const float X = ViewWidth*.5f-360.f;
    const float Y = ViewHeight*.5f-260.f;
    const EDemoMenuPage Page = PC->GetMenuPage(); // 本帧状态，不在HUD改变游戏逻辑。
    const UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 只读GI缓存，不读盘。
    Panel(X,Y,720,520,SessionUI::Surface,12);
    Label(TEXT("B R E A C H  /  作战终端"),X+32,Y+24,14,SessionUI::Accent);
    if (Page == EDemoMenuPage::Saves)
    {
        Label(TEXT("选择开始的存档"),X+32,Y+57,30,SessionUI::Ink);
        Label(TEXT("继续最近的检查点，或选择空栏位创建新存档"),X+32,Y+102,16,SessionUI::Muted);
        for (int32 Index=0; Index<3; ++Index) // 三个独立槽，文件损坏也保留占用状态。
        {
            const UDemoRunSave* Data = Saves->GetSlot(Index); // 当前栏位只读快照。
            const float Row = Y+145+Index*87.f; // 每栏保持足够鼠标点击面积。
            const FString Title = Data ? FString::Printf(TEXT("存档 %d   ·   %s"),Index+1,*Data->CreatedLocal.ToString(TEXT("%Y-%m-%d %H:%M:%S")))
                : FString::Printf(TEXT("栏位 %d   ·   %s"),Index+1,Saves->SlotExists(Index)?TEXT("无法读取，文件已保护"):TEXT("空存档  +"));
            Button(FName(*FString::Printf(TEXT("Save%d"),Index)),Title,X+32,Row,656,48,!Saves->SlotExists(Index)||Data,true);
            // 旧Victory读入也清空临时成长/银币回Hub；普通检查点恢复两钱包。
            if (Data) Label(Data->bEndless ? FString::Printf(TEXT("无尽已通关 %d · 最高 %d · 金币 %d · 银币 %d"),Data->CompletedLevel,Data->BestEndlessLevel,Data->Coins,Data->SilverCoins) : FString::Printf(TEXT("%s · %d / 10 关 · 金币 %d · 银币 %d"),Data->Phase==EDemoPhase::Hub?TEXT("安全区"):Data->Phase==EDemoPhase::Victory?TEXT("已通关"):TEXT("关间检查点"),Data->CompletedLevel,Data->Coins,Data->SilverCoins),X+45,Row+54,13,SessionUI::Muted);
        }
        Button(TEXT("OverlayBack"),TEXT("返回大厅"),X+500,Y+436,188,42,true,true);
    }
    else if (Page == EDemoMenuPage::EndlessUnlock)
    {
        Label(TEXT("已通关最高难度"),X+360,Y+120,32,SessionUI::Ink,true);
        Label(TEXT("解锁无尽模式"),X+360,Y+195,28,SessionUI::Accent,true);
        Label(TEXT("返回安全区，在下一关终端选择无尽挑战。"),X+360,Y+260,17,SessionUI::Muted,true);
        Button(TEXT("OverlayBack"),TEXT("知道了"),X+240,Y+358,240,52);
    }
    else if (Page == EDemoMenuPage::Pause)
    {
        Label(TEXT("游戏已暂停"),X+32,Y+57,32,SessionUI::Ink);
        Label(TEXT("按 ESC 继续游戏"),X+32,Y+105,16,SessionUI::Accent);
        Button(TEXT("PauseHub"),TEXT("1. 回到安全区"),X+130,Y+156,460,52);
        Button(TEXT("PauseSettings"),TEXT("2. 设置"),X+130,Y+220,460,52,true,true);
        Button(TEXT("PauseLobby"),TEXT("3. 回到大厅"),X+130,Y+284,460,52,true,true);
        Button(TEXT("PauseQuit"),TEXT("4. 退出游戏"),X+130,Y+348,460,52,true,true);
        Label(TEXT("返回大厅 / 退出会保留最近检查点，当前未完成战斗将重来"),X+32,Y+433,14,SessionUI::Muted);
    }
    else if (Page == EDemoMenuPage::CreateSave || Page == EDemoMenuPage::ReturnHub)
    {
        const bool Creating = Page == EDemoMenuPage::CreateSave; // 共用是/否排版，分离路由防止确认串页。
        Label(Creating?TEXT("是否创建新存档？"):TEXT("是否确认返回安全区？"),X+32,Y+95,30,SessionUI::Ink);
        Label(Creating?TEXT("创建后进入安全区，从第一关开始。"):TEXT("返回将清空临时成长、银币与关卡进度。"),X+32,Y+171,17,SessionUI::Muted);
        Label(Creating?TEXT("存档记录本次开始游戏的本地时间。"):TEXT("金币余额、永久成长与武器解锁保留。"),X+32,Y+212,16,SessionUI::Muted);
        Button(Creating?TEXT("CreateYes"):TEXT("HubYes"),TEXT("是，确认"),X+100,Y+330,240,52);
        Button(Creating?TEXT("CreateNo"):TEXT("HubNo"),TEXT("否，取消"),X+380,Y+330,240,52,true,true);
    }
    else if (Page == EDemoMenuPage::QuitSaving)
    {
        const EDemoQuitState Saving = PC->GetQuitState(); // 只读取类型化阶段；显示文字不参与退出判定。
        const bool Failed = Saving == EDemoQuitState::Failed; // 本地失败禁用离线退出，云失败允许明确选择。
        Label(Failed ? TEXT("保存需要处理") : Saving == EDemoQuitState::Saved ? TEXT("保存完成") : TEXT("正在保存进度"),X+32,Y+65,32,SessionUI::Ink);
        Label(PC->IsQuitLocalSaved() ? TEXT("01  本地检查点与永久进度  ·  已保存") : Failed ? TEXT("01  本地保存未完成") : TEXT("01  正在写入本地存档……"),X+32,Y+145,19,SessionUI::Accent);
#if WITH_EDITOR
        Label(TEXT("02  编辑器试玩仅使用本地数据，不连接云端"),X+32,Y+197,18,SessionUI::Muted);
        Label(TEXT("结束试玩后，本次试玩存档和解锁将清空。"),X+32,Y+255,16,SessionUI::Gold);
#else
        Label(Saving == EDemoQuitState::WaitingCloud ? TEXT("02  等待云端确认（最长 30 秒）……") : TEXT("02  云端状态请查看下方提示"),X+32,Y+197,18,SessionUI::Muted);
        Label(TEXT("未完成的战斗不保存，继续游戏时从最近检查点开始。"),X+32,Y+255,16,SessionUI::Muted);
#endif
        if (Failed)
        {
            Button(TEXT("QuitRetry"),TEXT("重试保存"),X+32,Y+342,190,50);
            Button(TEXT("QuitLocal"),TEXT("仅保存本地并退出"),X+242,Y+342,235,50,PC->IsQuitLocalSaved(),true);
            Button(TEXT("QuitCancel"),TEXT("取消退出"),X+497,Y+342,190,50,true,true);
            Label(PC->IsQuitLocalSaved() ? TEXT("云端失败可下次联网补传；存档冲突需回大厅处理。") : TEXT("本地写入未完成，请恢复磁盘权限或空间后重试。"),X+32,Y+422,14,SessionUI::Muted);
        }
        else Button(TEXT("QuitCancel"),TEXT("取消退出"),X+250,Y+352,220,50,true,true);
    }
    else if (Page == EDemoMenuPage::Settings)
    {
        Label(TEXT("设置"),X+32,Y+57,30,SessionUI::Ink);
        if (PC->GetDisplayConfirmSeconds() > 0)
        {
            Label(TEXT("是否保留新的显示模式？"),X+32,Y+159,25,SessionUI::Ink);
            Label(FString::Printf(TEXT("%.0f 秒后自动恢复原显示模式"),FMath::CeilToFloat(PC->GetDisplayConfirmSeconds())),X+32,Y+217,19,SessionUI::Gold);
            Button(TEXT("DisplayYes"),TEXT("保留"),X+100,Y+330,240,52);
            Button(TEXT("DisplayNo"),TEXT("恢复"),X+380,Y+330,240,52,true,true);
        }
        else
        {
            const TCHAR* Names[] = {TEXT("游戏分辨率"),TEXT("画质"),TEXT("鼠标移动速度"),TEXT("显示模式")}; // 与Controller四个设置编号一致。
            for (int32 Index=0; Index<4; ++Index) // 每项用明确的前/后按钮，不依赖键盘快捷键。
            {
                const float Row = Y+128+Index*65.f; // 设计像素，文字和点击区域同一坐标变换。
                Label(Names[Index],X+32,Row+13,17,SessionUI::Ink);
                Button(FName(*FString::Printf(TEXT("Setting%dMinus"),Index)),TEXT("－"),X+265,Row,48,44,true,true);
                Label(PC->GetSettingText(Index),X+470,Row+13,18,SessionUI::Accent,true);
                Button(FName(*FString::Printf(TEXT("Setting%dPlus"),Index)),TEXT("＋"),X+630,Row,48,44,true,true);
            }
            Button(TEXT("SettingsApply"),TEXT("应用设置"),X+267,Y+423,200,48);
            Button(TEXT("OverlayBack"),TEXT("返回"),X+489,Y+423,200,48,true,true);
            Label(TEXT("显示改变需确认；返回放弃尚未应用的改动"),X+32,Y+393,13,SessionUI::Muted);
        }
    }
    // 状态反馈与错误常驻页底；只展示摘要，完整错误同时写日志。
    Label(PC->GetMenuMessage().IsEmpty()?Saves->GetStatus():PC->GetMenuMessage(),X+32,Y+491,12,SessionUI::Gold);
}
bool ADemoHUD::HandleSessionClick(FName Name)
{
    DEMO_LOG_CALL();
    ADemoPlayerController* PC = Cast<ADemoPlayerController>(PlayerOwner); // 当前HUD拥有者，只在此同步分派借用。
    if (!PC || !PC->HasBlockingOverlay()) return false;
    if (Name == TEXT("OverlayBack")) PC->CloseOverlay();
    else if (Name == TEXT("Save0")) PC->SelectSaveSlot(0);
    else if (Name == TEXT("Save1")) PC->SelectSaveSlot(1);
    else if (Name == TEXT("Save2")) PC->SelectSaveSlot(2);
    else if (Name == TEXT("CreateYes")) PC->ConfirmCreateSave(true);
    else if (Name == TEXT("CreateNo")) PC->ConfirmCreateSave(false);
    else if (Name == TEXT("PauseHub")) PC->ReturnHubPressed();
    else if (Name == TEXT("HubYes")) PC->ConfirmReturnHub(true);
    else if (Name == TEXT("HubNo")) PC->ConfirmReturnHub(false);
    else if (Name == TEXT("PauseSettings")) PC->SettingsPressed();
    else if (Name == TEXT("PauseLobby")) PC->ReturnLobbyPressed();
    else if (Name == TEXT("PauseQuit")) PC->QuitPressed();
    // 保存页独立命名和Controller阶段校验，防止旧暂停热区越过保存屏障。
    else if (Name == TEXT("QuitRetry")) PC->RetryQuitSave();
    else if (Name == TEXT("QuitLocal")) PC->QuitWithLocalSave();
    else if (Name == TEXT("QuitCancel")) PC->CancelQuitSave();
    else if (Name == TEXT("SettingsApply")) PC->ApplyUserSettings();
    else if (Name == TEXT("DisplayYes")) PC->ConfirmDisplaySettings(true);
    else if (Name == TEXT("DisplayNo")) PC->ConfirmDisplaySettings(false);
    else
    {
        for (int32 Index=0; Index<4; ++Index) // 只识别本模块固定设置键，不能用任意文本写配置。
        {
            if (Name == FName(*FString::Printf(TEXT("Setting%dMinus"),Index))) PC->CycleSetting(Index,-1);
            if (Name == FName(*FString::Printf(TEXT("Setting%dPlus"),Index))) PC->CycleSetting(Index,1);
        }
    }
    return true; // 即使未知旧热区也消费，防止穿透到下层奖励/商店。
}
