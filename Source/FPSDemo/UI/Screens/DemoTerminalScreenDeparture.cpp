// 职责：R04行动准备页的双列行动摘要、难度与公开队伍门禁。
// 使用：只读GS准备/资源投影及本人PC快照；点击保留原Flow动作。资源就绪不等于保存/开战成功，仍停留时明确原重试/取消路径。
// 依赖：原Preparation和CommonUI意图、房主GI已有RunSaves缓存；不修改公共Flow、准备握手、存档、钱包或网络协议。
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/DemoUIStyle.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Characters/DemoCharacter.h"
#include "Game/DemoGameState.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Save/DemoRunSave.h" // 只给本机权威房主展示已有检查点反馈；客户端不猜测服务器磁盘结果。
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h" // 借用本机GI拥有的只读缓存，绘制不发起IO。
#include "Debug/DemoLog.h"

void UDemoTerminalScreen::BuildDeparture(const ADemoGameState* State)
{
    DEMO_LOG_CALL(); ConfigureLayout(1600.f,835.f); // 与终端同宽；正文可滚动，固定操作区保持可达。
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 本人权限、装备和出发回执，不读队友经济。
    const bool TeamRequired=State->TeamReady.bRequired; // 服务端决定是否需要全队明确准备。
    const bool Hub=State->Phase==EDemoPhase::Hub; // 物理备战房间不等于逻辑Hub，关间恢复不得解锁难度。
    const bool RequiresCore=!State->bEndless&&State->CampaignPlan.CampaignId==TEXT("steam_demo")&&State->CampaignPlan.ContentRevision>0; // 公开冻结战役身份对应现有核心门禁；物理房间和玩家是否已选择不能代替模式判断。
    const FDemoEncounterPreparationView& Preparation=State->Preparation; // 资源就绪与人为Ready独立。
    const bool Loading=Preparation.bActive&&!Preparation.bReadyToCommit; // 人数满额仍可能等引擎ACK；可提交而保存失败时允许重试。
    const bool AwaitingCommit=Preparation.bActive&&Preparation.bReadyToCommit; // 当前页仍显示说明入场尚未完成；不把此标记单独判定为保存失败，也不建立另一份准备状态机。
    const int32 NextStage=State->LevelNumber+1; // 仅显示关号，不推测或修改计划。
    const TCHAR* Difficulty=State->Difficulty==EDemoDifficulty::Easy?TEXT("简单"):State->Difficulty==EDemoDifficulty::Normal?TEXT("普通"):State->Difficulty==EDemoDifficulty::Hard?TEXT("困难"):TEXT("地狱"); // 文案不参与规则。
    SetHeading(TEXT("下一次突破"),TeamRequired?TEXT("核对装备与核心 · 全员准备后由房主出发"):Hub?TEXT("核对配置后选择难度 · 单人点击难度即请求出发"):TEXT("整备完成后继续行动 · 本轮难度已锁定"));
    UHorizontalBox* Content=AddRow(PageBody); // 左行动简报，右队伍或本人检查，不新增准备状态机。
    USizeBox* BriefBounds=WidgetTree->ConstructWidget<USizeBox>(); BriefBounds->SetWidthOverride(540.f); // 左栏固定宽度，右侧目录不会被长文挤动。
    UHorizontalBoxSlot* BriefSlot=Content->AddChildToHorizontalBox(BriefBounds); // 当前树拥有的自动宽度槽与40单位列间距。
    BriefSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); BriefSlot->SetPadding(FMargin(0,0,40,0));
    UVerticalBox* Brief=WidgetTree->ConstructWidget<UVerticalBox>(); BriefBounds->SetContent(Brief); // 本次实例树拥有。
    UVerticalBox* Details=AddColumn(Content); // 右侧按剩余空间Fill，名字/拒绝原因完整换行。
    UTextBlock* Eyebrow=AddText(Brief,TEXT("NEXT SECTOR / DEPLOYMENT"),16,DemoUI::Gold); // 英文眉题使用批准的窄体。
    FSlateFontInfo EyebrowFont=DemoUI::DisplayFont(16,TEXT("Medium")); EyebrowFont.LetterSpacing=120; Eyebrow->SetFont(EyebrowFont); // 字距单位1/1000em。
    UHorizontalBox* StageLine=AddRow(Brief); // 大关号与模式共享行，中文不使用固定数字字格。
    UTextBlock* Stage=AddText(StageLine,FString::Printf(TEXT("%02d"),NextStage),80,DemoUI::Ink); Stage->SetFont(DemoUI::NumberFont(80)); // 大数字取GS。
    if(UHorizontalBoxSlot* StageSlot=Cast<UHorizontalBoxSlot>(Stage->Slot)) { StageSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); StageSlot->SetPadding(FMargin(4,0,22,0)); }
    AddText(StageLine,FString::Printf(TEXT("%s\n当前档位  /  %s"),State->bEndless?TEXT("无尽行动"):TEXT("十关战役"),Difficulty),20,DemoUI::Muted);
    if(State->CampaignPlan.Stage==NextStage&&!State->CampaignPlan.RoomId.IsNone())
    {
        const TCHAR* Objective=State->CampaignPlan.Objective==EDemoObjectiveKind::Sabotage?TEXT("破坏设施"):State->CampaignPlan.Objective==EDemoObjectiveKind::RotatingControl?TEXT("轮转控制"):TEXT("歼灭威胁"); // 只展示已公开的下一关计划。
        AddText(Brief,FString::Printf(TEXT("任务  /  %s  ·  %s"),*State->CampaignPlan.RoomId.ToString(),Objective),17,DemoUI::Accent);
    }
    else AddText(Brief,TEXT("任务与房间以服务器出发规划为准。"),16,DemoUI::Muted);
    AddProgressionRule(Brief);
    const bool CorePicker=Hub&&RequiresCore; // 2026-11-20出发终端内直接选核心；仍是原CoreN/服务器SelectRunCore，锁定/阶段由Offer.BlockReason表达。
    if(CorePicker) { BuildDepartureCorePicker(Brief,510.f); AddProgressionRule(Brief); }
    if(Hub)
    {
        const FString Step=CorePicker?TEXT("② "):TEXT(""); // 只为阅读顺序编号，不改变门禁。
        AddText(Brief,Step+(TeamRequired?TEXT("由房主选择挑战档位"):AwaitingCommit?TEXT("点击原档位重试出发"):TEXT("选择档位并出发")),22,DemoUI::Ink); // 单人Hub重试继续原难度按钮；不能新增ConfirmNextLevel入口绕过原显式档位意图。
        const bool CanChoose=(!TeamRequired||View.bCanLeadSession)&&!Loading; // 资源等待中不叠加出发意图，服务端仍重验。
        UHorizontalBox* Basic=AddRow(Brief); // 单人原DifficultyPressed直接请求，团队仅选择；不新增假草稿。
        AddAction(Basic,TEXT("Easy"),TEXT("简单"),CanChoose,TeamRequired&&State->Difficulty==EDemoDifficulty::Easy&&!State->bEndless)->SetVisualRole(EDemoButtonVisualRole::Tab);
        AddAction(Basic,TEXT("Normal"),TEXT("普通"),CanChoose,TeamRequired&&State->Difficulty==EDemoDifficulty::Normal&&!State->bEndless)->SetVisualRole(EDemoButtonVisualRole::Tab);
        AddAction(Basic,TEXT("Hard"),TEXT("困难"),CanChoose,TeamRequired&&State->Difficulty==EDemoDifficulty::Hard&&!State->bEndless)->SetVisualRole(EDemoButtonVisualRole::Tab);
        UHorizontalBox* Special=AddRow(Brief); // 高阶档位保持原解锁门禁及可读条件。
        AddAction(Special,TEXT("Hell"),View.Progress.bHellUnlocked?TEXT("地狱"):TEXT("地狱 · 未解锁"),CanChoose&&View.Progress.bHellUnlocked,TeamRequired&&State->Difficulty==EDemoDifficulty::Hell&&!State->bEndless);
        AddAction(Special,TEXT("Endless"),View.Progress.bEndlessUnlocked?TEXT("无尽模式"):TEXT("无尽 · 未解锁"),CanChoose&&View.Progress.bEndlessUnlocked,TeamRequired&&State->bEndless);
        UTextBlock* Rules=AddText(Brief,TEXT("地狱：只用手枪开火通关困难；允许冲刺与治疗。\n无尽：通关地狱解锁，每10关出现Boss。"),16,DemoUI::Muted); // 不把待评估核心方案写进玩法条件。
        Rules->SetAutoWrapText(false); Rules->SetWrapTextAt(510.f);
        if(TeamRequired&&!View.bCanLeadSession) AddText(Brief,TEXT("模式与难度由房主选择；你可以核对配置并准备。"),16,DemoUI::Gold);
    }
    else
    {
        AddText(Brief,TEXT("本轮难度已锁定"),23,DemoUI::Ink);
        AddText(Brief,TEXT("留在当前区域可以继续购物、调整装备与准备。\n关闭终端不会推进关卡。"),18,DemoUI::Muted);
    }
    if(TeamRequired)
    {
        const FDemoTeamReadySnapshot& Team=State->TeamReady; // 只显示身份/核心/状态，不伪造空位、头像或队友余额。
        const ADemoPlayerState* Self=Controller()->GetPlayerState<ADemoPlayerState>(); // 用当前World身份标注本人。
        bool SelfReady=false,CanReady=false; // 本人投影缺失则禁用准备，不能按名册人数推断。
        int32 ReadyCount=0; // 仅作标题统计，不是出发授权。
        for(const FDemoTeamReadyMember& Member:Team.Members) if(Member.bReady) ++ReadyCount;
        AddText(Details,FString::Printf(TEXT("行动小队  /  %d 人已准备 · 共 %d 人"),ReadyCount,Team.Members.Num()),22,DemoUI::Ink);
        for(int32 Index=0;Index<FMath::Min(4,Team.Members.Num());++Index)
        {
            const FDemoTeamReadyMember& Member=Team.Members[Index]; // 本次构建借用，旅行后不持有Player指针。
            UVerticalBox* MemberCard=AddCard(Details); // 名册条目，长名字及拒绝原因保留正文滚动出口。
            if(UBorder* MemberSurface=Cast<UBorder>(MemberCard->GetParent())) { MemberSurface->SetPadding(FMargin(14,6)); MemberSurface->SetBrushColor(DemoUI::Raised.CopyWithNewOpacity(.14f)); }
            UHorizontalBox* Row=AddRow(MemberCard); // 序号/姓名核心/状态，不在UI写Member.bReady。
            UTextBlock* IndexLabel=AddText(Row,FString::Printf(TEXT("%02d"),Index+1),28,Member.bReady?DemoUI::Accent:DemoUI::Muted); IndexLabel->SetFont(DemoUI::NumberFont(28)); // 序号不是网络身份。
            if(UHorizontalBoxSlot* IndexSlot=Cast<UHorizontalBoxSlot>(IndexLabel->Slot)) { IndexSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); IndexSlot->SetPadding(FMargin(0,0,18,0)); }
            UVerticalBox* Identity=AddColumn(Row); // 姓名允许换行，不截断真实公开成员信息。
            AddText(Identity,Member.Name+(Member.Player==Self?TEXT("（你）"):TEXT("")),20,DemoUI::Ink);
            AddText(Identity,Member.CoreId.IsNone()?(RequiresCore?TEXT("未选核心 · 出发前必选"):TEXT("当前模式无需核心")):DemoRunBuild::Title(Member.CoreId).ToString(),15,DemoUI::Muted);
            UTextBlock* Ready=AddText(Row,Member.bReady?TEXT("✓ 已准备"):TEXT("未准备"),18,Member.bReady?DemoUI::Accent:DemoUI::Gold); // 文字与✓共同表达，不只靠颜色。
            if(UHorizontalBoxSlot* ReadySlot=Cast<UHorizontalBoxSlot>(Ready->Slot)) { ReadySlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); ReadySlot->SetPadding(FMargin(12,0,0,0)); }
            Ready->SetAutoWrapText(false);
            if(!Member.BlockReason.IsEmpty()) AddText(MemberCard,Member.BlockReason,15,DemoUI::Gold);
            if(Member.Player==Self) { SelfReady=Member.bReady; CanReady=Member.BlockReason.IsEmpty(); }
        }
        if(Team.Members.IsEmpty()) AddText(Details,TEXT("等待服务器同步小队名册"),17,DemoUI::Muted);
        AddText(Details,Team.bCanDepart?TEXT("✓ 全员准备完成，等待房主出发。"):Team.BlockReason,17,Team.bCanDepart?DemoUI::Accent:DemoUI::Gold);
        if(!Team.LastChangeReason.IsEmpty()) AddText(Details,Team.LastChangeReason,15,DemoUI::Muted);
        AddText(Details,TEXT("购物、更换装备或成员变化后，需要重新准备。"),15,DemoUI::Muted);
        AddFooterAction(TEXT("NextLevelNo"),TEXT("关闭并取消准备"))->SetVisualRole(EDemoButtonVisualRole::Standard); // 原Flow撤销交互/准备，不模拟取消已完成交易。
        AddFooterAction(SelfReady?TEXT("TeamNotReady"):TEXT("TeamReady"),SelfReady?TEXT("取消准备"):TEXT("我已准备"),SelfReady||CanReady)->SetVisualRole(SelfReady?EDemoButtonVisualRole::Standard:EDemoButtonVisualRole::Primary);
        AddFooterAction(TEXT("NextLevelYes"),Loading?TEXT("等待资源就绪…"):View.bCanLeadSession?(AwaitingCommit?TEXT("重试出发  →"):TEXT("全队出发  →")):(AwaitingCommit?TEXT("等待房主重试"):TEXT("等待房主出发")),!Loading&&View.bCanLeadSession&&Team.bCanDepart,220.f); // 只改原按钮文案，Ready/房主/资源门禁及ActionID完全沿用。
    }
    else
    {
        AddText(Details,TEXT("出发前核对"),25,DemoUI::Ink);
        AddText(Details,TEXT("YOUR LOADOUT / OWNER VIEW"),16,DemoUI::Gold)->SetFont(DemoUI::DisplayFont(16,TEXT("Medium")));
        AddProgressionRule(Details);
        AddText(Details,TEXT("本轮核心"),16,DemoUI::Muted);
        AddText(Details,View.Build.CoreId.IsNone()?(RequiresCore?TEXT("未选择 · 出发前必选"):TEXT("当前模式无需核心")):DemoRunBuild::Title(View.Build.CoreId).ToString(),24,DemoUI::Ink);
        const ADemoCharacter* Player=Cast<ADemoCharacter>(Controller()->GetPawn()); // 只借用已初始化的本人目录，不Spawn或加载枪械/弹药。
        const UDemoAmmoComponent* Ammo=Player?Player->FindComponentByClass<UDemoAmmoComponent>():nullptr; // 当前Pawn持有的只读目录，不以组件Selected覆盖OwnerSnapshot的装配回执。
        const UDemoAmmoCatalog* AmmoCatalog=Ammo?Ammo->GetCatalog():nullptr; // 现有已加载配置包含本地化名称；AmmoOffers契约未发送Title或Id。
        FString AmmoName=TEXT("等待装配同步"); // 缺失装配/目录/有效名称时保留明确回退，不用空Offer.Title覆盖它。
        for(int32 AmmoIndex=0;AmmoIndex<View.AmmoOffers.Num();++AmmoIndex) // 沿用固定四弹药目录顺序，状态以权威bSelected为准。
            if(View.AmmoOffers[AmmoIndex].bSelected)
            {
                if(AmmoCatalog&&AmmoCatalog->Entries.IsValidIndex(AmmoIndex)&&!AmmoCatalog->Entries[AmmoIndex].Name.IsEmpty()) AmmoName=AmmoCatalog->Entries[AmmoIndex].Name.ToString();
                else UE_LOG(LogFPSDemo,Log,TEXT("UIR_DEPART selected ammo name unavailable index=%d; waiting for catalog"),AmmoIndex);
                break;
            }
        AddText(Details,TEXT("已装配弹药"),16,DemoUI::Muted); AddText(Details,AmmoName,24,DemoUI::Ink);
        const UDemoWeaponComponent* Equipment=Player?Player->GetWeaponComponent():nullptr; // Pawn重建允许空，不由UI补发默认主枪。
        const ADemoWeaponBase* Primary=Equipment&&!View.PrimaryWeaponId.IsNone()?Equipment->GetCatalogWeapon(DemoWeaponCatalog::IndexOf(View.PrimaryWeaponId)):nullptr; // 按稳定Id映射现有CDO。
        AddText(Details,TEXT("主武器 / 固定副武器"),16,DemoUI::Muted);
        AddText(Details,Primary?Primary->Config.DisplayName.ToString()+TEXT(" / 手枪"):View.PrimaryWeaponId.IsNone()?TEXT("未装配主武器 / 手枪"):TEXT("主武器信息同步中 / 手枪"),24,DemoUI::Ink);
        AddProgressionRule(Details);
        AddText(Details,Hub?TEXT("只有安全区允许加入或重连。核心在出发后锁定，装备调整完成后再选择档位。"):TEXT("保持当前配置进入下一关；关间物理备战地点不会解除本轮核心与难度限制。"),17,DemoUI::Muted);
        if(Hub) AddBack(TEXT("NextLevelNo"),TEXT("← 返回安全区")); // 主操作仍为左侧难度按钮，不新增绕过明确选择的出发动作。
        else
        {
            AddFooterAction(TEXT("NextLevelNo"),TEXT("留在这里"))->SetVisualRole(EDemoButtonVisualRole::Standard);
            AddFooterAction(TEXT("NextLevelYes"),Loading?TEXT("正在准备资源…"):AwaitingCommit?TEXT("重试进入下一关  →"):TEXT("进入下一关  →"),!Loading,220.f); // ExpectedLevel/终端代数仍由原ConfirmNextLevel校验，不改原可用条件。
        }
    }
    if(Preparation.bActive)
    {
        if(AwaitingCommit)
        {
            // 资源ACK完成后原Flow只自动提交一次；保存或预检失败不会自动反复写盘，因此不承诺“自动入场”。
            const TCHAR* RetryHint=TeamRequired?(View.bCanLeadSession?TEXT("若仍停留此页，可点击“重试出发”；关闭终端可取消。"):TEXT("等待房主处理并重试出发；你仍可关闭终端取消准备。")):
                Hub?TEXT("若仍停留此页，请点击左侧原档位重试出发；返回安全区可取消。"):TEXT("若仍停留此页，可点击“重试进入下一关”；选择“留在这里”可取消。"); // 静态显示字串，不决定重试权限或提交请求。
            AddText(PageBody,FString::Printf(TEXT("资源已就绪  ·  入场尚未完成  ·  资源与落脚点 %d / %d 人确认\n%s"),Preparation.ReadyPlayers,Preparation.TotalPlayers,RetryHint),17,DemoUI::Gold);
            // 原公共说明只标为资源回执，不能用“正在提交战斗”替代最终保存/开战结果；后续若扩展公开错误也仍可见。
            if(!Preparation.Status.IsEmpty()) AddText(PageBody,TEXT("资源回执  /  ")+Preparation.Status,16,DemoUI::Muted);
            if(View.bCanLeadSession&&Controller()->HasAuthority()&&GetGameInstance())
                if(const UDemoRunSaves* Saves=GetGameInstance()->GetSubsystem<UDemoRunSaves>())
                    if(!Saves->GetStatus().IsEmpty()) AddText(PageBody,TEXT("本机最近检查点反馈  /  ")+Saves->GetStatus(),16,DemoUI::Gold); // 仅显示服务现有缓存，未绑定本次准备代数，不把上一条成功当作本次结果；不解析文案，也不借客户端私档判断房主。
            UE_LOG(LogFPSDemo,Log,TEXT("UI_DEPARTURE awaiting commit/retry generation=%d stage=%d leader=%d authority=%d"),Preparation.Generation,Preparation.Stage,View.bCanLeadSession,Controller()->HasAuthority());
        }
        else AddText(PageBody,FString::Printf(TEXT("正在准备入场  ·  资源与落脚点 %d / %d 人确认  ·  %s\n正在等待资源与落脚点确认；关闭终端可取消。"),Preparation.ReadyPlayers,Preparation.TotalPlayers,*Preparation.Status),17,DemoUI::Gold); // 仅等待资源时显示等待，不以人数满额假定已具备提交条件。
    }
    else if(!Preparation.Status.IsEmpty()&&Preparation.Stage==NextStage) AddText(PageBody,Preparation.Status,16,DemoUI::Gold);
    if(!Controller()->GetMenuMessage().IsEmpty()) AddText(PageBody,Controller()->GetMenuMessage(),16,DemoUI::Gold); // 核心/保存/版本拒绝不能只写日志。
}
