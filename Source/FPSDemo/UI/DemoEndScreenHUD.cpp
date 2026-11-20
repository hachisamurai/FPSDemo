// 职责：R04实际终局Canvas战报；以冻结的个人摘要展示到账、构筑与保留/清除边界。
// 使用：DrawHUD仅在Victory/Defeat且没有更高覆盖页时调用；依赖原Restart意图，不创建钱包或重新发放奖励。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/R02/DemoUITheme.h"
#include "Game/DemoGameState.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerCommands.h"
#include "Player/DemoPlayerProfile.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Debug/DemoLog.h"

void ADemoHUD::DrawEndScreen(const ADemoGameState* State)
{
    DEMO_LOG_TICK();
    if(!State) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("R04 after-action report waiting for game state")); return; }
    const bool Victory=State->Phase==EDemoPhase::Victory; // 公开阶段仅控制标题，不从已清理的当前账本推测上一轮收益。
    const FDemoPlayerMenuSnapshot& View=GetPlayerView(); // 当前帧借用本人可靠RPC投影，不读取队友经济。
    const FDemoRunSummarySnapshot& Summary=View.Summary; // 结束前冻结，World生命周期内只读。
    const bool Ready=!Summary.RunId.IsEmpty()&&Summary.EndPhaseRevision==State->PhaseRevision
        &&(Summary.Outcome==EDemoRunOutcome::Victory)==Victory; // 不同Actor复制无先后保证，拒绝呈现上一轮战报。
    const UDemoUITheme* Theme=R02HUDTheme?R02HUDTheme.Get():GetDefault<UDemoUITheme>(); // 终局战报按R02暗黑电影感无框账本绘制，不新建材质或缓存。
    const FLinearColor Base=Theme->Base,Ink=Theme->Ink,Muted=Theme->Muted,Ember=Theme->Ember;
    const FLinearColor Hairline=Ink.CopyWithNewOpacity(Theme->HairlineOpacity); // 1px发丝分隔线，替代R04大矩形框。
    const float Width=1088.f,Height=620.f; // 720p设计坐标内保留安全边距；超宽仍居中，不拉长信息阅读距离。
    const float X=(ViewWidth-Width)*.5f,Y=(ViewHeight-Height)*.5f; // 本帧布局值，无跨帧或网络状态。
    const float Left=X+32.f,Right=X+500.f; // 左栏收益/战果、右栏本轮构筑，保持两栏清晰对齐。
    Panel(0.f,0.f,ViewWidth,ViewHeight,Base.CopyWithNewOpacity(.90f),0.f); // 全屏暖炭黑暗化底，替代旧圆角卡片。
    if(ViewWidth/FMath::Max(1.f,ViewHeight)>=1.55f) // 宽屏下加2.39:1电影上下遮幅，4:3下不挤压纵向空间。
    {
        const float LetterboxH=FMath::Max(0.f,(ViewHeight-ViewWidth/2.39f)*.5f);
        if(LetterboxH>2.f)
        {
            Panel(0.f,0.f,ViewWidth,FMath::Min(LetterboxH,52.f),FLinearColor(0.f,0.f,0.f,1.f),0.f);
            Panel(0.f,ViewHeight-FMath::Min(LetterboxH,52.f),ViewWidth,FMath::Min(LetterboxH,52.f),FLinearColor(0.f,0.f,0.f,1.f),0.f);
        }
    }
    const bool bHellTrueEnding=Ready&&Victory&&Summary.Difficulty==EDemoDifficulty::Hell&&!Summary.bEndless; // D03剧本：仅地狱难度整轮通关触发十号冷库救出亲人真结局。
    const FString Headline=bHellTrueEnding?TEXT("冷库门开了。她走出了封锁线。"):
        Victory&&Summary.bEndless?TEXT("回响仍在继续。"):
        Victory?TEXT("外围防线已突破。"):
        State->bReturnToHubOnRestart?TEXT("意识被弹回安全区。"):TEXT("行动中断，进度受保护。"); // 按D03《静默封锁区》区分真结局、普通突破与回响重开。
    const FString SubHeadline=bHellTrueEnding?TEXT("SECTOR 10 BREACHED"):
        Victory?TEXT("PERIMETER CLEARED"):
        State->bReturnToHubOnRestart?TEXT("ECHO RESET"):TEXT("OPERATION INTERRUPTED"); // 英文副标仅作R02编辑排版定位。
    Label(TEXT("PERSONAL AFTER-ACTION LEDGER"),Left,Y+16.f,14.f,Muted);
    Label(Headline,Left,Y+40.f,38.f,Ink);
    Label(SubHeadline,Right,Y+50.f,16.f,(bHellTrueEnding||!Victory)?Ember:Muted);
    Panel(Left,Y+116.f,Width-64.f,1.f,(bHellTrueEnding||!Victory)?Ember.CopyWithNewOpacity(.75f):Hairline,0.f); // R02主标下1px发丝线。
    if(!Ready)
    {
        Label(TEXT("正在等待服务器确认本轮摘要…"),Left,Y+157.f,22.f,Muted);
        Label(TEXT("确认前不显示奖励、余额或清理结果。"),Left,Y+197.f,16.f,Muted);
        if(!State->FailureReason.IsEmpty())
        {
            // 公开失败文案仅此帧借用；按保守中文列宽折行，保留旧系统的完整有限原因，不以Left截断。
            FString Line; int32 LineIndex=0; float Units=0.f; // 当前行、行号与中文宽度单位，ASCII按半宽估算。
            for(TCHAR Character:State->FailureReason)
            {
                const float CharacterUnits=Character<128?.55f:1.f; // 这里只约束错误辅助文案。
                if(Character==TEXT('\n')||Units+CharacterUnits>58.f)
                {
                    Label(Line,Left,Y+245.f+LineIndex*24.f,16.f,Ember); Line.Reset(); Units=0.f; ++LineIndex;
                    if(Character==TEXT('\n')) continue;
                }
                Line.AppendChar(Character); Units+=CharacterUnits;
            }
            if(!Line.IsEmpty()) Label(Line,Left,Y+245.f+LineIndex*24.f,16.f,Ember);
        }
    }
    else
    {
        Panel(X+466.f,Y+142.f,1.f,254.f,Hairline,0.f); // R02两栏账本中间竖向1px发丝线。
        const TCHAR* Difficulty=Summary.Difficulty==EDemoDifficulty::Easy?TEXT("简单"):Summary.Difficulty==EDemoDifficulty::Hard?TEXT("困难"):Summary.Difficulty==EDemoDifficulty::Hell?TEXT("地狱"):TEXT("普通"); // 集中配置DifficultyName为表键；这里只翻译已确认枚举，不改难度规则。
        Label(FString::Printf(TEXT("%s%s  /  已完成 %d%s  /  到达第 %d 关"),Difficulty,Summary.bEndless?TEXT(" · 无尽"):TEXT("难度"),Summary.CompletedStages,Summary.bEndless?TEXT(" 关"):TEXT(" / 10 关"),Summary.ReachedStage),Left,Y+88.f,15.f,Muted);
        Label(TEXT("本轮实际金币奖励"),Left,Y+144.f,16.f,Muted);
        const FString Credit=FString::Printf(TEXT("+%d"),Summary.AwardedGold); // 实际已提交增量，含余额上限；不能显示配置额冒充到账。
        const float CreditSize=FMath::Min(76.f,380.f/FMath::Max(1.f,Credit.Len()*.64f)); // 余额上限可达十位数，收益读数不跨入构筑列。
        Label(Credit,Left,Y+176.f,CreditSize,Ink);
        Label(TEXT("金币  /  永久保留"),Left,Y+262.f,16.f,Muted);
        Panel(Left,Y+298.f,392.f,1.f,Hairline,0.f);
        Label(TEXT("团队累计击杀"),Left,Y+318.f,15.f,Muted);
        Label(FString::FromInt(Summary.TeamKills),Left,Y+345.f,36.f,Ink); // 原账本是团队击杀，禁止重命名为个人击杀。
        Label(TEXT("保留金币余额"),Left+220.f,Y+318.f,15.f,Muted);
        const FString Balance=FString::FromInt(Summary.GoldBalance); // 摘要中冻结的个人余额，不与奖励再次相加。
        const float BalanceSize=FMath::Min(36.f,178.f/FMath::Max(1.f,Balance.Len()*.64f)); // 保留十位金额可读，禁止跨列。
        Label(Balance,Left+220.f,Y+345.f,BalanceSize,Ink);
        Label(TEXT("RUN BUILD  /  本轮构筑"),Right,Y+144.f,15.f,Muted);
        const FString Core=Summary.Build.CoreId.IsNone()?TEXT("旧战役 / 未选择核心"):DemoRunBuild::Title(Summary.Build.CoreId).ToString(); // 清理前记录，团灭不显示已重置的空核心。
        const FString First=Summary.Build.Branch3.IsNone()?TEXT("未获得"):DemoRunBuild::Title(Summary.Build.Branch3).ToString(); // 第一互斥分支组。
        const FString Second=Summary.Build.Branch7.IsNone()?TEXT("未获得"):DemoRunBuild::Title(Summary.Build.Branch7).ToString(); // 第二互斥分支组。
        Label(Core,Right,Y+176.f,28.f,Ink);
        Label(FString::Printf(TEXT("03  /  %s"),*First),Right,Y+226.f,16.f,Muted);
        Label(FString::Printf(TEXT("07  /  %s"),*Second),Right,Y+257.f,16.f,Muted);
        const TCHAR* Objective=Summary.LastObjective==EDemoObjectiveKind::Sabotage?TEXT("设施破坏"):Summary.LastObjective==EDemoObjectiveKind::RotatingControl?TEXT("转移控制"):TEXT("歼灭目标"); // 仅翻译服务器确认的任务枚举。
        Label(Summary.LastObjective==EDemoObjectiveKind::KillAll?FString::Printf(TEXT("最后任务  /  %s"),Objective):FString::Printf(TEXT("最后任务  /  %s  %d / %d"),Objective,Summary.ObjectiveCompleted,Summary.ObjectiveTotal),Right,Y+306.f,16.f,Muted);
        Label(FString::Printf(TEXT("永久成长  /  生命 +%g  ·  伤害 +%g  ·  容量 +%g"),Summary.PermanentHealth,Summary.PermanentDamage,Summary.PermanentMagazine),Right,Y+340.f,15.f,Ink);
        Label(FString::Printf(TEXT("保留武器权限 %d 项  /  解锁后仍需回终端装备"),Summary.UnlockedWeapons.Num()),Right,Y+373.f,15.f,Muted); // 权限总数不能冒充此次新增解锁数。
        Panel(Left,Y+413.f,Width-64.f,1.f,Hairline,0.f);
        Label(TEXT("PROGRESS RETENTION"),Left,Y+438.f,14.f,Muted);
        Label(Summary.bTemporaryReset?FString::Printf(TEXT("已清除银币 %d；银币成长、免费强化与核心分支结束。"),Summary.SilverRemoved):TEXT("保留最近检查点；未完成战斗不会记录为通关。"),Right,Y+434.f,15.f,Muted);
        FString ResultNote=Victory&&Summary.Difficulty==EDemoDifficulty::Hell&&!Summary.bEndless?TEXT("她已经安全了。体内的药剂正在彻底失控——在理智熄灭前，你停在了阴影里。〔无尽模式已解锁〕"):
            Victory&&Summary.Difficulty==EDemoDifficulty::Hell?TEXT("已通关最高难度，无尽回响持续记录中。"):
            Victory&&Summary.Difficulty==EDemoDifficulty::Hard&&Summary.PistolChallenge==1?TEXT("你压住了嗜杀的冲动，仅凭手枪突破重围——通往十号冷库的地狱封锁线已打开。"):
            Victory?TEXT("已突破外围防线并带回物资，但核心冷库仍在更深处。可在安全区提高难度继续推进。"):
            Summary.bTemporaryReset?TEXT("回响场将你弹回了安全区。金币、永久强化与已解锁武器保留。"):
            Summary.Reason; // 使用冻结资格和真实系统原因，按D03剧情映射四难度与真结局说明，不虚构未实现数值。
        // 有限服务器原因按约60个中文宽度换行；无捕获/异步回调，所有临时内容本帧消费。
        FString Line; int32 LineIndex=0; float Units=0.f; // 结果说明的行缓存，不进入存档或复制。
        for(TCHAR Character:ResultNote)
        {
            const float CharacterUnits=Character<128?.55f:1.f; // 英文型号按近似半宽，为完整原因预留第二行。
            if(Character==TEXT('\n')||Units+CharacterUnits>62.f)
            {
                Label(Line,Left,Y+478.f+LineIndex*23.f,16.f,(bHellTrueEnding||Victory)?Ink:Muted); Line.Reset(); Units=0.f; ++LineIndex;
                if(Character==TEXT('\n')) continue;
            }
            Line.AppendChar(Character); Units+=CharacterUnits;
        }
        if(!Line.IsEmpty()) Label(Line,Left,Y+478.f+LineIndex*23.f,16.f,(bHellTrueEnding||Victory)?Ink:Muted);
    }
    if(View.bCanLeadSession)
    {
        const float ButtonX=X+Width-420.f,ButtonY=Y+548.f,ButtonWidth=388.f,ButtonHeight=44.f; // 固定操作区，热区与实际矩形同源。
        const bool Hover=IsHovered(ButtonX,ButtonY,ButtonWidth,ButtonHeight); // R02无底框下划线操作，悬停只点亮底线，不自动执行Restart。
        Label(Victory?TEXT("[ENTER] 返回安全区 · 继续挑战"):State->bReturnToHubOnRestart?TEXT("[ENTER] 返回安全区 · 重新挑战"):TEXT("[ENTER] 返回大厅"),ButtonX+ButtonWidth*.5f,ButtonY+10.f,17.f,Hover?Ink:Ink.CopyWithNewOpacity(.82f),true);
        Panel(ButtonX+36.f,ButtonY+ButtonHeight-4.f,ButtonWidth-72.f,1.f,Hover?Ember:Ink.CopyWithNewOpacity(.45f),0.f);
        AddHitBox(FVector2D(ButtonX,ButtonY)*UIScale,FVector2D(ButtonWidth,ButtonHeight)*UIScale,TEXT("Restart"),true); // 入口名称不变，原控制器重新核验房主与阶段权限。
    }
    else Label(TEXT("等待房主继续行动；可按 Esc 离开房间。"),Left,Y+559.f,17.f,Muted);
    if(View.bCanLeadSession) Label(TEXT("金币与永久成长独立保留"),Left,Y+561.f,15.f,Muted); // 客户端等待文案占同区时不叠加辅助标签。
    if(const UDemoPlayerProfile* Profile=GetGameInstance()?GetGameInstance()->GetSubsystem<UDemoPlayerProfile>():nullptr)
        Label(Profile->GetStatus(),Left,Y+600.f,12.f,Muted); // 保存状态来自原系统；战报已收到并不代表本地/云已保存。
}
