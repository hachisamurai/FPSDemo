// 职责：菜单关闭后的真实回执/连接/检查点；S1游戏HUD无底板，旧页型保R04。使用：DrawHUD局内/初始化调用，不改服务文案或有效期。
// 依赖：本PC UIManager的短通知、PC连接状态与GI RunSaves；无输入/存档/网络权限，不改业务结果。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/Common/DemoUIManager.h"
#include "Player/DemoPlayerController.h"
#include "Save/DemoRunSave.h"
#include "Engine/GameInstance.h"
#include "Debug/DemoLog.h"
#include "HAL/PlatformTime.h" // 本机短通知使用单调钟，暂停不延长保存成功提示。

void ADemoHUD::DrawSystemStatus(ADemoPlayerController* PC)
{
    DEMO_LOG_TICK(); if(!PC) return;
    const bool bR02=IsR02GameplayPresentation(); // 本帧样式开关，不依据通知文字推断业务/准备结果。
    const auto* Manager=PC->FindComponentByClass<UDemoUIManager>(); // 本地拥有者组件，不读取其它玩家回执。
    FString Message=Manager?Manager->GetFeedbackText():FString(); // 仅当前单调时间内的有限通知。
    bool Warning=Manager&&Manager->IsFeedbackWarning();
    if(Message.IsEmpty()&&!PC->GetConnectionStatus().IsEmpty()) { Message=PC->GetConnectionStatus(); Warning=true; }
    if(!Message.IsEmpty())
    {
        TArray<FString> Lines; FString Current; float Width=0.f; // Canvas不自动换行，按保守中文等效列宽保留完整有限错误。
        for(const TCHAR Character:Message.Left(160))
        {
            const float Units=Character<128?.55f:1.f; // 显示文案近似列宽，不用于伤害/数值判断。
            if(Character==TEXT('\n')||Width+Units>26.f) { Lines.Add(Current); Current.Reset(); Width=0; if(Character==TEXT('\n')) continue; }
            Current.AppendChar(Character); Width+=Units;
        }
        if(!Current.IsEmpty()) Lines.Add(Current);
        const float X=ViewWidth*.5f-230.f,Y=145.f; // 处于左右目标/钱包之间、Boss条下方，保持战斗中心可见。
        if(!bR02)
        {
            Panel(X,Y,460,18.f+Lines.Num()*22.f,DemoUI::Surface.CopyWithNewOpacity(.90f),2);
            Panel(X,Y,2,18.f+Lines.Num()*22.f,Warning?DemoUI::Gold:DemoUI::Accent,0);
        }
        for(int32 Index=0;Index<Lines.Num();++Index) Label(Lines[Index],X+16,Y+9+Index*22,16,Warning?DemoUI::Gold:DemoUI::Ink);
    }
    const auto* Saves=GetGameInstance()?GetGameInstance()->GetSubsystem<UDemoRunSaves>():nullptr; // GI保活，仅借用缓存状态，不在绘制中IO。
    if(Saves&&Saves->GetActiveSlot()!=INDEX_NONE&&!Saves->GetStatus().IsEmpty())
    {
        if(bR02)
        {
            const uint64 Serial=Saves->GetChangeSerial(); // 真实本地保存序号，不能用于授权或推算钱包。
            if(R02ObservedSaveSerial!=Serial||R02ObservedSaveStatus!=Saves->GetStatus())
            { R02ObservedSaveSerial=Serial; R02ObservedSaveStatus=Saves->GetStatus(); R02SaveNoticeUntil=FPlatformTime::Seconds()+3.; UE_LOG(LogFPSDemo,Log,TEXT("R02 save notice updated serial=%llu"),Serial); }
            if(FPlatformTime::Seconds()>=R02SaveNoticeUntil) return; // 完整保存状态仍在原菜单；只结束本HUD短通知。
        }
        if(!bR02) Panel(28,ViewHeight-27,440,22,DemoUI::Surface.CopyWithNewOpacity(.78f),1);
        Label(Saves->GetStatus().Left(36),40,ViewHeight-23,13,bR02?DemoUI::Ink:DemoUI::Muted); // 真实状态沿原源；亮场景无底板时使用高对比Ink+Label阴影。
    }
}
