// 职责：大厅子页与暂停覆盖层的Canvas显示。使用：HUD按MenuFlow当前页绘制；仅分派意图，不在绘制中保存或修改战局。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h" // 存档/设置/暂停/房间/退出页统一复用大厅灰阶透明主题。
#include "UI/DemoUIActions.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"

void ADemoHUD::DrawSessionMenu(ADemoPlayerController* PC, const ADemoGameState* State)
{
    DEMO_LOG_TICK();
    if(PC->GetMenuPage()==EDemoMenuPage::PlayerStatus) { DrawPlayerStatus(PC); return; } // 状态页独占点击热区。
    if (!State || State->Phase == EDemoPhase::Lobby) DrawLobbyBackground();
    if(PC->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { DrawFirstRunGuide(); return; } // 提示独占热区，不访问大厅尚未就绪的存档服务。
    Panel(0,0,ViewWidth,ViewHeight,DemoUI::Overlay,0);
    // 所有子页共用居中720×520布局，最小4:3视口按UIScale等比缩放。
    const float X = ViewWidth*.5f-360.f;
    const float Y = ViewHeight*.5f-260.f;
    const EDemoMenuPage Page = PC->GetMenuPage(); // 本帧状态，不在HUD改变游戏逻辑。
    const UDemoRunSaves* Saves = GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 只读GI缓存，不读盘。
    Panel(X,Y,720,520,GetPanelSurface(),12);
    Label(TEXT("B R E A C H  /  作战终端"),X+32,Y+24,14,DemoUI::Accent);
    if (Page == EDemoMenuPage::Multiplayer)
    {
        const UDemoRoomSubsystem* Rooms = GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>(); // GI状态只读投影；DrawHUD不得启动搜索或连接。
        const bool bBusy = Rooms->IsBusy(); // 操作中禁用刷新/开房，返回按钮可撤销后续意图。
        const bool bReady = Rooms->IsReady(); // 公网需要完成配置和登录，LAN直接可用。
        const TArray<FDemoRoomView>& Items = Rooms->GetRooms(); // 借用到本帧结束，不跨异步回调保存引用。
        DrawnRoomRevision = Rooms->GetRevision(); DrawnRoomOffset = PC->GetMenuFlow()->GetRoomPage() * 5;
        Label(TEXT("多人游戏"),X+32,Y+57,30,DemoUI::Ink);
        Button(TEXT("RoomLAN"),TEXT("局域网"),X+270,Y+57,120,36,!bBusy,true); // 选中页也保留标签，切换逻辑在服务校验。
        Button(TEXT("RoomPublic"),TEXT("公网"),X+400,Y+57,120,36,!bBusy,true);
        Button(TEXT("RoomAccount"),bReady?TEXT("退出账号"):TEXT("登录账号"),X+540,Y+57,148,36,!bBusy&&Rooms->IsPublic(),true);
        Label(Rooms->IsPublic()?TEXT("公网 · EOS · 房主保存检查点 · 仅安全区加入"):TEXT("局域网 · 2～4人合作 · 仅安全区加入"),X+32,Y+102,16,DemoUI::Muted);
        for (int32 Row=0; Row<5 && Items.IsValidIndex(DrawnRoomOffset+Row); ++Row) // 分页显示任意数量结果，不截断为固定5个房间。
        {
            const FDemoRoomView& Item = Items[DrawnRoomOffset+Row]; // 本行只读广播，最终准入以服务器为准。
            const float RowY = Y+140+Row*52.f; // 五行与底部操作固定留白，避免点击区重叠。
            const FString Ping = Item.PingMs == INDEX_NONE ? TEXT("延迟未测") : FString::Printf(TEXT("%dms"),Item.PingMs); // 公网目录结果不得冒充实测Ping。
            Label(FString::Printf(TEXT("%s  %d/4  %s"),*Item.Name,Item.Players,*Ping),X+32,RowY,16,DemoUI::Ink);
            Label(Item.Stage,X+32,RowY+24,12,DemoUI::Muted);
            Button(FName(*FString::Printf(TEXT("Room%d"),Row)),TEXT("加入"),X+556,RowY,132,40,!bBusy&&bReady&&Item.bJoinable,true);
        }
        if (Items.IsEmpty()) Label(bBusy?TEXT("正在查找房间……"):TEXT("暂无可用房间，可刷新或创建房间"),X+360,Y+255,19,DemoUI::Muted,true);
        Button(TEXT("RoomPrevious"),TEXT("上一页"),X+32,Y+408,96,30,!bBusy&&DrawnRoomOffset>0,true);
        Label(FString::Printf(TEXT("%d / %d"),PC->GetMenuFlow()->GetRoomPage()+1,FMath::Max(1,(Items.Num()+4)/5)),X+360,Y+416,13,DemoUI::Muted,true);
        Button(TEXT("RoomNext"),TEXT("下一页"),X+592,Y+408,96,30,!bBusy&&DrawnRoomOffset+5<Items.Num(),true);
        Button(TEXT("RoomRefresh"),TEXT("刷新"),X+32,Y+448,140,36,!bBusy&&bReady,true);
        Button(TEXT("RoomQuick"),TEXT("快速加入"),X+184,Y+448,156,36,!bBusy&&bReady,true);
        Button(TEXT("RoomCreate"),TEXT("创建房间"),X+352,Y+448,160,36,!bBusy&&bReady);
        Button(TEXT("OverlayBack"),bBusy?TEXT("取消"):TEXT("返回大厅"),X+524,Y+448,164,36,true,true);
    }
    else if (Page == EDemoMenuPage::Saves)
    {
        // 开房与单机共享槽布局，但提示不同，避免误认为战斗中也能开放加入。
        Label(PC->GetMenuFlow()->IsChoosingHostSave()?TEXT("选择房主存档"):TEXT("选择开始的存档"),X+32,Y+57,30,DemoUI::Ink);
        Label(PC->GetMenuFlow()->IsChoosingHostSave()?TEXT("选择安全区存档或创建新存档，进入后公开房间"):TEXT("继续最近的检查点，或选择空栏位创建新存档"),X+32,Y+102,16,DemoUI::Muted);
        for (int32 Index=0; Index<3; ++Index) // 三个独立槽，文件损坏也保留占用状态。
        {
            const UDemoRunSave* Data = Saves->GetSlot(Index); // 当前栏位只读快照。
            const float Row = Y+145+Index*87.f; // 每栏保持足够鼠标点击面积。
            const FString Title = Data ? FString::Printf(TEXT("存档 %d   ·   %s"),Index+1,*Data->CreatedLocal.ToString(TEXT("%Y-%m-%d %H:%M:%S")))
                : FString::Printf(TEXT("栏位 %d   ·   %s"),Index+1,Saves->SlotExists(Index)?TEXT("无法读取，文件已保护"):TEXT("空存档  +"));
            Button(FName(*FString::Printf(TEXT("Save%d"),Index)),Title,X+32,Row,656,48,!Saves->SlotExists(Index)||Data,true);
            // 旧Victory读入也清空临时成长/银币回Hub；普通检查点恢复两钱包。
            if (Data) Label(Data->bEndless ? FString::Printf(TEXT("无尽已通关 %d · 最高 %d · 金币 %d · 银币 %d"),Data->CompletedLevel,Data->BestEndlessLevel,Data->Coins,Data->SilverCoins) : FString::Printf(TEXT("%s · %d / 10 关 · 金币 %d · 银币 %d"),Data->Phase==EDemoPhase::Hub?TEXT("安全区"):Data->Phase==EDemoPhase::Victory?TEXT("已通关"):TEXT("关间检查点"),Data->CompletedLevel,Data->Coins,Data->SilverCoins),X+45,Row+54,13,DemoUI::Muted);
        }
        Button(TEXT("OverlayBack"),TEXT("返回大厅"),X+500,Y+436,188,42,true,true);
    }
    else if (Page == EDemoMenuPage::EndlessUnlock)
    {
        Label(TEXT("已通关最高难度"),X+360,Y+120,32,DemoUI::Ink,true);
        Label(TEXT("解锁无尽模式"),X+360,Y+195,28,DemoUI::Accent,true);
        Label(TEXT("返回安全区，在下一关终端选择无尽挑战。"),X+360,Y+260,17,DemoUI::Muted,true);
        Button(TEXT("OverlayBack"),TEXT("知道了"),X+240,Y+358,240,52);
    }
    else if (Page == EDemoMenuPage::Pause)
    {
        Label(GetNetMode()==NM_Standalone?TEXT("游戏已暂停"):TEXT("游戏菜单 · 战斗继续"),X+32,Y+57,32,DemoUI::Ink);
        Label(PC->GetConnectionStatus().IsEmpty()?TEXT("按 ESC 返回游戏"):PC->GetConnectionStatus(),X+32,Y+105,16,DemoUI::Accent);
        Button(TEXT("PauseHub"),TEXT("1. 回到安全区"),X+130,Y+156,460,52);
        Button(TEXT("PauseSettings"),TEXT("2. 设置"),X+130,Y+220,460,52,true,true);
        Button(TEXT("PauseLobby"),TEXT("3. 回到大厅"),X+130,Y+284,460,52,true,true);
        Button(TEXT("PauseQuit"),TEXT("4. 退出游戏"),X+130,Y+348,460,52,true,true);
        Label(TEXT("返回大厅 / 退出会保留最近检查点，当前未完成战斗将重来"),X+32,Y+433,14,DemoUI::Muted);
    }
    else if (Page == EDemoMenuPage::CreateSave || Page == EDemoMenuPage::ReturnHub)
    {
        const bool Creating = Page == EDemoMenuPage::CreateSave; // 共用是/否排版，分离路由防止确认串页。
        Label(Creating?TEXT("是否创建新存档？"):TEXT("是否确认返回安全区？"),X+32,Y+95,30,DemoUI::Ink);
        Label(Creating?TEXT("创建后进入安全区，从第一关开始。"):TEXT("返回将清空临时成长、银币与关卡进度。"),X+32,Y+171,17,DemoUI::Muted);
        Label(Creating?TEXT("存档记录本次开始游戏的本地时间。"):TEXT("金币余额、永久成长与武器解锁保留。"),X+32,Y+212,16,DemoUI::Muted);
        Button(Creating?TEXT("CreateYes"):TEXT("HubYes"),TEXT("是，确认"),X+100,Y+330,240,52);
        Button(Creating?TEXT("CreateNo"):TEXT("HubNo"),TEXT("否，取消"),X+380,Y+330,240,52,true,true);
    }
    else if (Page == EDemoMenuPage::QuitSaving)
    {
        const EDemoQuitState Saving = PC->GetQuitState(); // 只读取类型化阶段；显示文字不参与退出判定。
        const bool Failed = Saving == EDemoQuitState::Failed; // 本地失败禁用离线退出，云失败允许明确选择。
        Label(Failed ? TEXT("保存需要处理") : Saving == EDemoQuitState::Saved ? TEXT("保存完成") : TEXT("正在保存进度"),X+32,Y+65,32,DemoUI::Ink);
        Label(PC->IsQuitLocalSaved() ? (GetNetMode()==NM_Client?TEXT("01  本机档案已保存 · 战局检查点由房主保存"):TEXT("01  本地检查点与永久进度  ·  已保存")) : Failed ? TEXT("01  本地保存未完成") : TEXT("01  正在写入本地存档……"),X+32,Y+145,19,DemoUI::Accent);
#if WITH_EDITOR
        Label(TEXT("02  编辑器试玩仅使用本地数据，不连接云端"),X+32,Y+197,18,DemoUI::Muted);
        Label(TEXT("结束试玩后，本次试玩存档和解锁将清空。"),X+32,Y+255,16,DemoUI::Gold);
#else
        Label(Saving == EDemoQuitState::WaitingCloud ? TEXT("02  等待云端确认（最长 30 秒）……") : TEXT("02  云端状态请查看下方提示"),X+32,Y+197,18,DemoUI::Muted);
        Label(TEXT("未完成的战斗不保存，继续游戏时从最近检查点开始。"),X+32,Y+255,16,DemoUI::Muted);
#endif
        if (Failed)
        {
            Button(TEXT("QuitRetry"),TEXT("重试保存"),X+32,Y+342,190,50);
            Button(TEXT("QuitLocal"),TEXT("仅保存本地并退出"),X+242,Y+342,235,50,PC->IsQuitLocalSaved(),true);
            Button(TEXT("QuitCancel"),TEXT("取消退出"),X+497,Y+342,190,50,true,true);
            Label(PC->IsQuitLocalSaved() ? TEXT("云端失败可下次联网补传；存档冲突需回大厅处理。") : TEXT("本地写入未完成，请恢复磁盘权限或空间后重试。"),X+32,Y+422,14,DemoUI::Muted);
        }
        else Button(TEXT("QuitCancel"),TEXT("取消退出"),X+250,Y+352,220,50,true,true);
    }
    else if (Page == EDemoMenuPage::Settings)
    {
        Label(TEXT("设置"),X+32,Y+57,30,DemoUI::Ink);
        if (PC->GetDisplayConfirmSeconds() > 0)
        {
            Label(TEXT("是否保留新的显示模式？"),X+32,Y+159,25,DemoUI::Ink);
            Label(FString::Printf(TEXT("%.0f 秒后自动恢复原显示模式"),FMath::CeilToFloat(PC->GetDisplayConfirmSeconds())),X+32,Y+217,19,DemoUI::Gold);
            Button(TEXT("DisplayYes"),TEXT("保留"),X+100,Y+330,240,52);
            Button(TEXT("DisplayNo"),TEXT("恢复"),X+380,Y+330,240,52,true,true);
        }
        else
        {
            const TCHAR* Names[] = {TEXT("游戏分辨率"),TEXT("画质"),TEXT("鼠标移动速度"),TEXT("显示模式"),TEXT("垂直同步")}; // 与Controller五个设置编号一致；新增项针对跳跃时快速垂直运动的撕裂。
            for (int32 Index=0; Index<5; ++Index) // 每项用明确的前/后按钮，不依赖键盘快捷键。
            {
                const float Row = Y+118+Index*55.f; // 五行仍位于应用按钮上方；设计像素随UIScale缩放。
                Label(Names[Index],X+32,Row+13,17,DemoUI::Ink);
                Button(FName(*FString::Printf(TEXT("Setting%dMinus"),Index)),TEXT("－"),X+265,Row,48,44,true,true);
                Label(PC->GetSettingText(Index),X+470,Row+13,18,DemoUI::Accent,true);
                Button(FName(*FString::Printf(TEXT("Setting%dPlus"),Index)),TEXT("＋"),X+630,Row,48,44,true,true);
            }
            Button(TEXT("SettingsApply"),TEXT("应用设置"),X+267,Y+423,200,48);
            Button(TEXT("OverlayBack"),TEXT("返回"),X+489,Y+423,200,48,true,true);
            Label(TEXT("显示改变需确认；返回放弃尚未应用的改动"),X+32,Y+393,13,DemoUI::Muted);
        }
    }
    // 状态反馈与错误常驻页底；只展示摘要，完整错误同时写日志。
    Label(Page==EDemoMenuPage::Multiplayer?GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()->GetStatus():(PC->GetMenuMessage().IsEmpty()?Saves->GetStatus():PC->GetMenuMessage()),X+32,Y+491,12,DemoUI::Gold); // 房间状态由本机服务独立提供。
}
bool ADemoHUD::HandleSessionClick(FName Name)
{
    DEMO_LOG_CALL();
    ADemoPlayerController* PC=Cast<ADemoPlayerController>(PlayerOwner); // 当前帧输入拥有者。
    if(!PC||!PC->HasBlockingOverlay())return false;
    const EDemoUIAction Action = DemoUIActions::Decode(Name); // 只允许白名单行号，不能把任意HitBox解释为服务器地址。
    if (Action>=EDemoUIAction::Room0 && Action<=EDemoUIAction::Room4)
        PC->GetMenuFlow()->JoinRoom(DrawnRoomOffset+static_cast<int32>(Action)-static_cast<int32>(EDemoUIAction::Room0),DrawnRoomRevision);
    else PC->GetMenuFlow()->DispatchUIAction(Action);
    return true; // 未知或过期热区同样消费，绝不穿透到下层商店。
}
