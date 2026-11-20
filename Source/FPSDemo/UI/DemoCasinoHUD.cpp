// 职责：安全区金币娱乐的Canvas视图。使用：读取权威公开规则与本机Flow；所有点击经Flow提交，动画仅呈现已保存结果。
#include "UI/DemoHUD.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIStyle.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Debug/DemoLog.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Engine/Texture2D.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"

void ADemoHUD::DrawCasinoSymbol(int32 Index,float X,float Y)
{
    DEMO_LOG_TICK();
    const auto* Pawn=PlayerOwner?Cast<ADemoCharacter>(PlayerOwner->GetPawn()):nullptr; // 当前帧拥有者，旅行不缓存旧Pawn。
    const auto* Ammo=Pawn?Pawn->FindComponentByClass<UDemoAmmoComponent>():nullptr; // 已加载目录属于组件。
    const auto* Catalog=Ammo?Ammo->GetCatalog():nullptr; // 不在绘制内LoadObject。
    if(Index>=0&&Index<4&&Catalog&&Catalog->Entries.IsValidIndex(Index)&&Catalog->Entries[Index].Icon&&Catalog->Entries[Index].Icon->GetResource())
    {
        FCanvasTileItem Icon(FVector2D(X,Y)*UIScale,Catalog->Entries[Index].Icon->GetResource(),FVector2D(56,56)*UIScale,FLinearColor::White); // 使用现有透明弹药Icon。
        Icon.BlendMode=SE_BLEND_Translucent; Canvas->DrawItem(Icon);
    }
    else Label(Index==4?TEXT("G"):TEXT("◆"),X+28,Y+5,42,DemoUI::Gold,true); // 金币无新二进制资产，缺失图标也能明确识别符号。
    Label(DemoCasino::SymbolName(Index),X+28,Y+60,16,DemoUI::Gold,true);
}

void ADemoHUD::DrawCasinoMenu(ADemoPlayerController* PC)
{
    DEMO_LOG_TICK();
    const auto* Flow=PC->GetMenuFlow(); // 本帧借用，不持有业务对象。
    const auto& View=PC->GetMenuSnapshot(); // 已脱敏菜单快照，没有任何未揭雷位。
    const auto& Draft=Flow->GetCasinoDraft(); // 本机下注选择，服务器会重新验证。
    const auto& Rules=View.CasinoRules; // 由服务器投影的公开概率/倍率。
    const auto& Mine=View.Progress.Gambling.Mine; // 只有已开格、冻结金额和雷数量。
    const auto& Result=Flow->GetCasinoResult(); // 已成功保存的最终公开结果。
    const auto Quote=Flow->GetCasinoQuote(); // 不发奖的预览。
    const float X=ViewWidth/2-480,Y=ViewHeight/2-310; // 960x620设计空间，统一HUD缩放。
    const bool Waiting=Flow->IsGambleWaiting(),Confirm=Flow->IsGambleConfirmOpen(); // 旧帧输入仍由Flow二次拦截。
    const TCHAR* Titles[]={TEXT("数字竞猜"),TEXT("军械老虎机"),TEXT("拆弹货箱")}; // 模式顺序固定。
    const auto& Arcade=View.Progress.Gambling.Arcade; // 已保存持续局面，关闭页面仍保留。
    const bool HasResult=Result.Mode>=0&&Mine.Stake==0&&Arcade.Mode<0; // 安全翻格保留网格，终局展示结果。
    const double Time=Flow->GetCasinoRevealTime(); // 真实时间，世界暂停不影响三轴动画。
    const bool Rolling=Flow->IsCasinoRolling(); // 仅数字和老虎机延时揭晓。
    Panel(X,Y,960,620,GetPanelSurface(),12);
    Label(TEXT("GOLD ARCADE / 金币娱乐"),X+30,Y+24,13,DemoUI::Accent);
    Label(DemoCasino::ModeName(Draft.Mode),X+30,Y+50,28,DemoUI::Ink);
    Label(FString::Printf(TEXT("金币 %d"),View.Progress.Coins),X+300,Y+61,18,DemoUI::Gold);
    if(!Confirm)
    {
        Button(TEXT("GambleSkipAnimation"),Flow->IsGambleAnimationSkipped()?TEXT("[√] 跳过动画"):TEXT("[ ] 跳过动画"),X+520,Y+26,185,34,!Waiting,true);
        Button(TEXT("Casino.Back"),PC->GetMenuFlow()->IsCasinoDevicePage()?TEXT("离开设备"):TEXT("返回列表"),X+715,Y+26,110,34,!Waiting&&!Rolling,true);
        Button(TEXT("Close"),TEXT("关闭"),X+835,Y+26,95,34,!Waiting,true);
    }
    if(Confirm)
    {
        Label(TEXT("确认下注"),X+480,Y+145,28,DemoUI::Ink,true);
        Label(FString::Printf(TEXT("投入 %d 金币"),Draft.Stake),X+480,Y+200,24,DemoUI::Gold,true);
        const FString ChoiceText=Draft.Mode==0?(Draft.Selection==0?FString::Printf(TEXT("选择 %02d～%02d"),Draft.Low,Draft.High):Draft.Selection==1?FString(TEXT("选择奇数")):FString(TEXT("选择偶数"))):Draft.Mode==1?FString(TEXT("三个转轴独立开奖")):FString::Printf(TEXT("25格 / %d枚雷，关闭界面可继续"),Draft.Bombs); // 明确实际操作，不能仅泛化成确认购买。
        Label(Draft.Mode>=3?DemoCasino::ModeName(Draft.Mode):ChoiceText,X+480,Y+247,20,DemoUI::Ink,true);
        if(Draft.Mode==0) Label(FString::Printf(TEXT("概率 %.2f%% · 总到账 %d · 净收益 %+d"),Quote.Chance*100,Quote.Payout,Quote.Payout-Draft.Stake),X+480,Y+291,18,DemoUI::Gold,true);
        else if(Draft.Mode==1) Label(FString::Printf(TEXT("中奖 %.2f%% · 一对 %.2g倍 · 最高到账 %d"),Quote.Chance*100,Rules.PairMultiplier,Quote.Payout),X+480,Y+291,18,DemoUI::Gold,true);
        else if(Draft.Mode>=3) Label(FString::Printf(TEXT("最高总到账 %d 金币 · 不设奖金封顶"),Quote.MaximumPayout),X+480,Y+291,18,DemoUI::Gold,true);
        else Label(FString::Printf(TEXT("第一格安全率 %.2f%% · 安全后可领 %d"),Quote.Chance*100,Quote.Payout),X+480,Y+291,18,DemoUI::Gold,true);
        Label(TEXT("失败损失本次投入；倍率包含本金。保存失败会撤销本次操作。"),X+480,Y+342,16,DemoUI::Muted,true);
        Button(TEXT("GambleYes"),TEXT("确认下注"),X+260,Y+422,210,46);
        Button(TEXT("GambleNo"),TEXT("取消"),X+490,Y+422,210,46,true,true); return;
    }
    if(HasResult)
    {
        Label(Rolling?TEXT("正在抽取"):TEXT("抽取完成"),X+480,Y+132,28,DemoUI::Ink,true);
        if(Result.Mode==1) Label(TEXT("转轴为结果动画 · 符号经过次数不代表中奖概率"),X+480,Y+178,14,DemoUI::Muted,true);
        if(Result.Mode==1&&Result.Symbols.Num()==3)
        {
            for(int32 Reel=0;Reel<3;++Reel) // 三轴独立减速/停轴，最终值只能来自已保存回执。
            {
                const double Stop=1.7+Reel*.55; // 逐轴停止时间秒；不参与随机概率。
                const bool Stopped=Time>=Stop; // 跳过设置将Time直接推进完成。
                const double Progress=FMath::Clamp(Time/Stop,0.,1.); // 三次缓动滚动到服务器符号。
                const double Target=30.+Result.Symbols[Reel]; // 整数循环后准确停靠目标。
                const double Position=Target*(1.-FMath::Pow(1.-Progress,3.)); // 纯UI位置，不重抽结果。
                const float Left=X+240+Reel*165; // 三个滚轴的独立视口。
                Panel(Left,Y+218,150,150,Stopped?DemoUI::Selected:DemoUI::Raised,10);
                Canvas->Canvas->Flush_GameThread(); // 每轴裁切只影响本轴，避免符号穿过按钮和其他HUD。
                Canvas->Canvas->SetRenderTargetScissorRect(FIntRect(FMath::RoundToInt(Left*UIScale),FMath::RoundToInt((Y+223)*UIScale),FMath::RoundToInt((Left+150)*UIScale),FMath::RoundToInt((Y+362)*UIScale)));
                const int32 Middle=FMath::FloorToInt(Position); // 循环符号的附近三行，不创建Widget/Actor。
                for(int32 Slot=Middle-1;Slot<=Middle+2;++Slot)
                    DrawCasinoSymbol((Slot%5+5)%5,Left+47,Y+250+float(Slot-Position)*100.f); // 连续垂直位移，停靠时中心恰好是服务器目标。
                Canvas->Canvas->Flush_GameThread(); Canvas->Canvas->SetRenderTargetScissorRect(FIntRect());
                Panel(Left,Y+288,6,2,DemoUI::Gold,0); Panel(Left+144,Y+288,6,2,DemoUI::Gold,0); // 指示中央中奖行。
                Label(Stopped?TEXT("已停靠"):TEXT("滚动中"),Left+75,Y+377,13,DemoUI::Muted,true);
            }
        }
        else if(Result.Mode==0)
        {
            const int32 Number=Rolling?int32(Time*53)%100:Result.Number; // 纯计时占位，结果由回执确定。
            Label(FString::Printf(TEXT("%02d"),Number),X+480,Y+237,78,DemoUI::Gold,true);
        }
        else if(Result.Mode>=3) DrawArcadePanel(PC,X,Y,true);
        else if(Result.bBomb) { DrawCasinoExplosion(X+480,Y+292,Time); Label(TEXT("货箱爆炸"),X+480,Y+345,25,DemoUI::Gold,true); }
        else Label(Result.bBomb?TEXT("爆炸格 · 本箱结束"):TEXT("货箱金币已领取"),X+480,Y+255,30,Result.bBomb?DemoUI::Muted:DemoUI::Gold,true);
        if(!Rolling)
        {
            Label(PC->GetMenuMessage().Left(60),X+480,Y+405,18,DemoUI::Gold,true);
            Button(TEXT("Casino.Again"),FString::Printf(TEXT("继续抽取 · %d金币"),Draft.Stake),X+210,Y+478,280,44,!Waiting);
            Button(TEXT("Casino.ClearResult"),TEXT("调整选项"),X+510,Y+478,230,44,!Waiting,true);
        }
        return;
    }
    // 左侧玩法，右侧下注与报价。统一说明余额变化和整数取整，避免把总到账当成净赚。
    Panel(X+550,Y+103,380,458,DemoUI::Raised,8);
    const bool Pending=Mine.Stake>0||Arcade.Mode>=0; // 有未结箱时所有新下注停用，仍可从列表进入货箱。
    Label(Pending?TEXT("已保存的局面"):TEXT("设置下注"),X+574,Y+121,22,DemoUI::Ink);
    if(!Pending)
    {
        for(int32 Index=0;Index<Rules.Presets.Num()&&Index<4;++Index)
            Button(FName(*FString::Printf(TEXT("Casino.Preset.%d"),Index)),FString::FromInt(Rules.Presets[Index]),X+574+Index*83,Y+164,76,32,!Waiting,true);
        Button(TEXT("Casino.BetInput"),FString::Printf(TEXT("%d 金币 · 点击输入"),Draft.Stake),X+574,Y+207,332,40,!Waiting,true);
        Button(TEXT("Casino.BetDown10"),TEXT("−10步"),X+574,Y+256,76,32,!Waiting,true);
        Button(TEXT("Casino.BetDown"),TEXT("−"),X+657,Y+256,76,32,!Waiting,true);
        Button(TEXT("Casino.BetUp"),TEXT("+"),X+740,Y+256,76,32,!Waiting,true);
        Button(TEXT("Casino.BetUp10"),TEXT("+10步"),X+823,Y+256,76,32,!Waiting,true);
        Label(FString::Printf(TEXT("最低%d · 无下注上限 · 支持键入/粘贴"),Rules.MinBet),X+574,Y+300,13,DemoUI::Muted);
        FString Detail; // 按模式展示实际报价；老虎机全部赔率在左侧。
        if(Draft.Mode==0) Detail=FString::Printf(TEXT("中奖概率 %.2f%%\n规则倍率 %.4g倍（含本金）\n命中到账 %d 金币\n净收益 %+d / 未中 -%d"),Quote.Chance*100,Quote.Multiplier,Quote.Payout,Quote.Payout-Draft.Stake,Draft.Stake);
        else if(Draft.Mode==1) Detail=FString::Printf(TEXT("中奖概率 %.2f%%\n最高到账 %d 金币\n符号只用于结算金币\n不会解锁对应弹药"),Quote.Chance*100,Quote.Payout);
        else if(Draft.Mode>=3) Detail=Draft.Mode==5?FString::Printf(TEXT("所选胜率 %.2f%%\n总到账倍率 %.4g倍\n命中到账 %d 金币"),Quote.Chance*100,Quote.Multiplier,Quote.Payout):FString::Printf(TEXT("最高总到账 %d 金币\n按实际组合或成绩结算\n倍率包含本金\n未结局面关闭后可继续"),Quote.MaximumPayout);
        else Detail=FString::Printf(TEXT("首格安全率 %.2f%%\n首格安全可领 %d 金币\n返还系数 %.2f%%\n按倍率完整到账，无奖金封顶"),Quote.Chance*100,Quote.Payout,Rules.MineReturn*100);
        DrawGambleText(Detail,X+574,Y+335,5);
        DrawGambleText(Quote.Error,X+574,Y+440,2);
        Button(TEXT("Casino.Place"),Waiting?TEXT("正在抽取"):Draft.Mode==2?TEXT("投入金币 · 开启货箱"):TEXT("下注并确认"),X+574,Y+500,332,42,!Waiting&&Quote.Error.IsEmpty());
    }
    else if(Arcade.Mode>=0)
    {
        Label(DemoCasino::ModeName(Arcade.Mode),X+574,Y+175,23,DemoUI::Gold);
        Label(FString::Printf(TEXT("实际投入 %d 金币"),Arcade.Debited),X+574,Y+220,18,DemoUI::Ink);
        DrawGambleText(TEXT("局面与金额已保存，不能同时开启其他项目。关闭页面后可从列表回到本局。"),X+574,Y+275,6);
        if(Arcade.Mode!=Draft.Mode) Button(FName(*FString::Printf(TEXT("Casino.Open.%d"),Arcade.Mode)),TEXT("继续未结项目"),X+574,Y+490,332,42,!Waiting);
        else if(Arcade.Mode==6) Button(TEXT("Casino.Range"),TEXT("进入靶场 · 左键射击"),X+574,Y+490,332,42,!Waiting);
    }
    else
    {
        const int32 Count=Mine.OpenCount(); // 只由公开安全格计算，不检查隐藏雷。
        Label(FString::Printf(TEXT("实际投入 %d · 已开 %d 格"),Mine.Debited,Count),X+574,Y+173,18,DemoUI::Gold);
        Label(FString::Printf(TEXT("当前可领 %d 金币"),Mine.Available()),X+574,Y+222,22,DemoUI::Gold);
        if(Mine.Payouts.IsValidIndex(Count)) Label(FString::Printf(TEXT("再开安全格可领 %d"),Mine.Payouts[Count]),X+574,Y+268,17,DemoUI::Ink);
        if(Count>0) Label(FString::Printf(TEXT("当前领取净收益 %+d"),Mine.Available()-Mine.Debited),X+574,Y+293,14,DemoUI::Gold);
        Label(FString::Printf(TEXT("下一格安全率 %.2f%%"),100.*(25-Mine.Bombs-Count)/(25-Count)),X+574,Y+322,17,DemoUI::Muted);
        DrawGambleText(TEXT("棋盘与金额已经保存。踩雷失去本箱未领金币；关闭、重开界面或读档仍是同一箱。"),X+574,Y+352,5);
        Button(TEXT("Casino.Cash"),TEXT("领取金币并结束"),X+574,Y+500,332,42,!Waiting&&Count>0&&Draft.Mode==2);
    }
    if(Draft.Mode>=3) DrawArcadePanel(PC,X,Y,false);
    else if(Draft.Mode==0)
    {
        const TCHAR* Keys[]={TEXT("Casino.Small"),TEXT("Casino.Big"),TEXT("Casino.Odd"),TEXT("Casino.Even")}; // 明确固定快捷操作。
        const TCHAR* Names[]={TEXT("小 00～49"),TEXT("大 50～99"),TEXT("奇数"),TEXT("偶数")}; // 实际匹配由规则共用。
        for(int32 Index=0;Index<4;++Index) Button(Keys[Index],Names[Index],X+30+Index*126,Y+104,118,32,!Waiting&&!Pending,true);
        Button(TEXT("Casino.LowDown"),TEXT("−"),X+30,Y+146,34,30,!Waiting&&!Pending,true);
        Label(FString::Printf(TEXT("起点 %02d"),Draft.Low),X+77,Y+151,16,DemoUI::Ink);
        Button(TEXT("Casino.LowUp"),TEXT("+"),X+163,Y+146,34,30,!Waiting&&!Pending,true);
        Button(TEXT("Casino.HighDown"),TEXT("−"),X+237,Y+146,34,30,!Waiting&&!Pending,true);
        Label(FString::Printf(TEXT("终点 %02d"),Draft.High),X+284,Y+151,16,DemoUI::Ink);
        Button(TEXT("Casino.HighUp"),TEXT("+"),X+370,Y+146,34,30,!Waiting&&!Pending,true);
        Label(TEXT("点格选单号"),X+417,Y+153,13,DemoUI::Muted);
        for(int32 Number=0;Number<100;++Number) // 固定100格，横向十列；不加载任何资产或随机数。
            Button(FName(*FString::Printf(TEXT("Casino.Number.%d"),Number)),FString::Printf(TEXT("%02d"),Number),X+30+(Number%10)*50,Y+192+(Number/10)*36,44,31,!Waiting&&!Pending,!DemoCasino::Matches(Draft,Number));
    }
    else if(Draft.Mode==1)
    {
        Label(TEXT("三个转轴独立抽取 · 三连优先于一对"),X+30,Y+115,18,DemoUI::Ink);
        double Total=0; // 展示实际权重的归一化概率。
        for(double Weight:Rules.SymbolWeights) Total+=Weight;
        for(int32 Index=0;Index<5&&Rules.SymbolWeights.IsValidIndex(Index)&&Rules.TripleMultipliers.IsValidIndex(Index);++Index)
        {
            Panel(X+30,Y+160+Index*56,493,46,DemoUI::Raised,5);
            Label(DemoCasino::SymbolName(Index),X+46,Y+172+Index*56,18,DemoUI::Gold);
            Label(FString::Printf(TEXT("每轴 %.2f%%   三连 %.3g倍"),Total>0?100*Rules.SymbolWeights[Index]/Total:0,Rules.TripleMultipliers[Index]),X+200,Y+175+Index*56,16,DemoUI::Ink);
        }
        Label(FString::Printf(TEXT("恰好一对 %.3g倍 · 三个不同 0倍"),Rules.PairMultiplier),X+30,Y+463,18,DemoUI::Gold);
        Label(TEXT("倍率包含本金，向下取整，不设奖金封顶"),X+30,Y+511,15,DemoUI::Muted);
    }
    else
    {
        if(Pending) Label(FString::Printf(TEXT("本箱 %d 枚雷 · 金额与棋盘已冻结"),Mine.Bombs),X+30,Y+113,18,DemoUI::Gold);
        else for(int32 Index=0;Index<Rules.BombChoices.Num()&&Index<3;++Index)
            Button(FName(*FString::Printf(TEXT("Casino.Bombs.%d"),Index)),FString::Printf(TEXT("%d 枚雷"),Rules.BombChoices[Index]),X+30+Index*165,Y+104,155,34,!Waiting&&!Pending,Draft.Bombs!=Rules.BombChoices[Index]);
        for(int32 Tile=0;Tile<25;++Tile)
        {
            const bool Open=(Mine.OpenMask&(1<<Tile))!=0; // 只展示已开安全格，不推测隐藏棋盘。
            Button(FName(*FString::Printf(TEXT("Casino.Tile.%d"),Tile)),Open?TEXT("安全"):TEXT("?"),X+30+(Tile%5)*100,Y+155+(Tile/5)*79,92,70,!Waiting&&Pending&&!Open,!Open);
        }
    }
    Label(PC->GetMenuMessage().Left(62),X+30,Y+581,14,DemoUI::Gold);
}
