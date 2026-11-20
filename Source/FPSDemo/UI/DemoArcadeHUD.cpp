// 职责：反应堆、战术骰、斗兽场与靶场的独立Canvas视图。使用：Casino主页面调用；只读公开快照，不生成经济结果。
#include "UI/DemoHUD.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIStyle.h"
#include "Economy/Gambling/DemoCasinoRules.h"
#include "Debug/DemoLog.h"
void ADemoHUD::DrawArcadePanel(ADemoPlayerController* PC,float X,float Y,bool bResult)
{
    DEMO_LOG_TICK();
    const auto* Flow=PC->GetMenuFlow(); // 当前帧本地页面。
    const auto& View=PC->GetMenuSnapshot(); const auto& R=View.CasinoRules; // 权威公开规则，只读。
    const auto& D=Flow->GetCasinoDraft(); const auto& Round=View.Progress.Gambling.Arcade; // 草稿与已保存局面分开。
    const auto& Result=Flow->GetCasinoResult(); // 只允许动画展示服务器已提交结果。
    const bool Pending=Round.Mode==D.Mode,Enabled=!Flow->IsGambleWaiting(); // 服务器仍复核阶段、序号及余额。
    const double Time=Flow->GetCasinoRevealTime(); // 真实秒；跳过动画直接返回10。
    if(bResult)
    {
        if(Result.Mode==3)
        {
            if(Result.bBomb) DrawCasinoExplosion(X+480,Y+275,Time);
            Label(Result.bBomb?TEXT("过载 · 能量已消散"):TEXT("充能收益已领取"),X+480,Y+323,24,DemoUI::Gold,true);
        }
        else if(Result.Mode==4)
        {
            for(int32 Index=0;Index<Result.Symbols.Num();++Index)
            {
                Panel(X+230+Index*103,Y+220,90,85,DemoUI::Raised,8);
                Label(FString::FromInt(Time<2.?1+(int32(Time*20)+Index)%6:Result.Symbols[Index]),X+275+Index*103,Y+235,38,DemoUI::Gold,true);
            }
            if(Time>=3.) Label(DemoCasino::DiceTierName(Result.Winner),X+480,Y+330,22,DemoUI::Gold,true);
        }
        else if(Result.Mode==5&&R.ArenaHealth.Num()==2)
        {
            int32 HP[2]={R.ArenaHealth[0],R.ArenaHealth[1]}; // 从实际命中序列还原双方血量，不在UI重新战斗。
            const int32 Count=FMath::Min(Result.Battle.Num(),FMath::FloorToInt(FMath::Clamp(Time/2.8,0.,1.)*Result.Battle.Num())); // 全部交战依序回放。
            for(int32 Index=0;Index<Count;++Index) { const int32 Event=Result.Battle[Index]; /* 符号表示攻击方。 */ HP[Event>0?1:0]=FMath::Max(0,HP[Event>0?1:0]-(FMath::Abs(Event)-1)); }
            for(int32 Side=0;Side<2;++Side)
            {
                const float Left=X+190+Side*360; // 对称机械兽卡片，冲击方向相向。
                const bool Attacking=Count>0&&((Result.Battle[Count-1]>0?0:1)==Side)&&Time<2.8;
                const float Lunge=Attacking?FMath::Abs(FMath::Sin(float(Time)*35))*20*(Side==0?1:-1):0; // 只影响造型位移。
                Panel(Left+30+Lunge,Y+242,130,65,Side==0?DemoUI::Accent:DemoUI::Gold,8);
                Panel(Left+50+Lunge,Y+222,82,35,DemoUI::Raised,5);
                Panel(Left+18+Lunge,Y+277,24,48,DemoUI::Muted,4); Panel(Left+150+Lunge,Y+277,24,48,DemoUI::Muted,4);
                Label(Side==0?TEXT("磁轨兽"):TEXT("装甲蛛"),Left+95,Y+190,22,DemoUI::Ink,true);
                Panel(Left,Y+345,190,12,DemoUI::Raised,3); Panel(Left,Y+345,190.f*HP[Side]/R.ArenaHealth[Side],12,DemoUI::Gold,3);
                Label(FString::Printf(TEXT("%d / %d"),HP[Side],R.ArenaHealth[Side]),Left+95,Y+366,14,DemoUI::Muted,true);
            }
            if(Count>0&&Time<3.) Label(FMath::Abs(Result.Battle[Count-1])==1?TEXT("闪避"):FString::Printf(TEXT("命中 −%d"),FMath::Abs(Result.Battle[Count-1])-1),X+480,Y+270,24,DemoUI::Gold,true);
        }
        else Label(TEXT("射击挑战结算完成"),X+480,Y+267,30,DemoUI::Gold,true);
        return;
    }
    if(D.Mode==3)
    {
        Label(TEXT("反应堆过载 / 逐级充能"),X+30,Y+114,22,DemoUI::Ink);
        const auto& Chances=Pending?Round.Chances:R.ReactorChances; // 旧局始终展示旧合同。
        double Survival=1; // 显示总倍率，不作为权威结果。
        for(int32 Level=0;Level<Chances.Num();++Level)
        {
            Survival*=Chances[Level];
            const int32 Paid=Pending?Round.Payouts[Level]:DemoCasino::Pay(D.Stake,R.ReactorReturn/Survival); // 非法极大报价显示不可用。
            Label(FString::Printf(TEXT("%s %02d级   成功 %.1f%%   可领 %s"),Pending&&Round.Step>Level?TEXT("●"):TEXT("○"),Level+1,Chances[Level]*100,Paid<0?TEXT("金额溢出"):*FString::FromInt(Paid)),X+30,Y+165+Level*26,16,DemoUI::Gold);
        }
        DrawGambleText(TEXT("每次充能独立判定。成功后可领取或继续；过载损失本次投入。最后一级自动领取。"),X+30,Y+437,3);
        if(Pending) { Button(TEXT("Casino.Advance"),TEXT("继续充能"),X+30,Y+510,235,40,Enabled); Button(TEXT("Casino.Collect"),TEXT("立即领取"),X+280,Y+510,235,40,Enabled&&Round.Step>0,true); }
    }
    else if(D.Mode==4)
    {
        Label(TEXT("保留骰面 / 一次重掷"),X+30,Y+114,22,DemoUI::Ink);
        for(int32 Index=0;Index<5;++Index)
        {
            const bool Kept=(D.KeepMask&(1<<Index))!=0; // 本机保留标志，服务器只接收五位掩码。
            Button(FName(*FString::Printf(TEXT("Casino.Keep.%d"),Index)),Pending?FString::FromInt(Round.Values[Index]):TEXT("?"),X+30+Index*99,Y+165,88,75,Enabled&&Pending,!Kept);
            Label(Kept?TEXT("已保留"):TEXT("可重掷"),X+74+Index*99,Y+251,14,DemoUI::Muted,true);
        }
        for(int32 Tier=0;Tier<8;++Tier)
            Label(Pending?FString::Printf(TEXT("%s：%d金币"),*DemoCasino::DiceTierName(Tier),Round.Payouts[Tier]):FString::Printf(TEXT("%s：%.3g倍"),*DemoCasino::DiceTierName(Tier),R.DiceMultipliers.IsValidIndex(Tier)?R.DiceMultipliers[Tier]:0),X+30+(Tier%2)*245,Y+300+(Tier/2)*32,17,DemoUI::Gold);
        Label(TEXT("只结算最高组合；点击骰子切换保留"),X+30,Y+460,16,DemoUI::Muted);
        if(Pending) { Button(TEXT("Casino.Advance"),TEXT("重掷未保留骰并结算"),X+30,Y+510,265,40,Enabled); Button(TEXT("Casino.Collect"),TEXT("直接结算"),X+310,Y+510,205,40,Enabled,true); }
    }
    else if(D.Mode==5&&R.ArenaHealth.Num()==2&&R.ArenaDamage.Num()==2&&R.ArenaHit.Num()==2)
    {
        Label(TEXT("公开战斗属性 / 选择支持方"),X+30,Y+114,22,DemoUI::Ink);
        for(int32 Side=0;Side<2;++Side)
        {
            FDemoCasinoIntent Intent=D; Intent.Selection=Side; // 各方独立赔率，使用同一精确概率模型。
            const auto Quote=DemoCasino::QuoteArcade(R,Intent);
            Panel(X+30+Side*250,Y+170,240,248,DemoUI::Raised,8);
            Label(Side==0?TEXT("磁轨兽"):TEXT("装甲蛛"),X+150+Side*250,Y+190,25,DemoUI::Gold,true);
            DrawGambleText(FString::Printf(TEXT("生命 %d / 伤害 %d\n命中率 %.1f%%\n胜率 %.2f%%\n总到账倍率 %.4g倍"),R.ArenaHealth[Side],R.ArenaDamage[Side],R.ArenaHit[Side]*100,Quote.Chance*100,Quote.Multiplier),X+47+Side*250,Y+242,6);
            Button(FName(*FString::Printf(TEXT("Casino.Side.%d"),Side)),D.Selection==Side?TEXT("已选择"):TEXT("支持此方"),X+47+Side*250,Y+365,206,38,Enabled,D.Selection!=Side);
        }
        DrawGambleText(TEXT("双方随机先手、交替攻击。赔率来自实际战斗模型，历史输赢不改变概率。"),X+30,Y+455,4);
    }
    else if(D.Mode==6)
    {
        Label(TEXT("赏金靶场 / 十靶限时挑战"),X+30,Y+114,22,DemoUI::Ink);
        DrawGambleText(FString::Printf(TEXT("开局后立即计时 %d 秒\n训练子弹 %d 发\n身体命中1分，头部命中2分\n鼠标瞄准，左键单发\n关闭菜单或读档不重置时限"),R.RangeSeconds,R.RangeShots),X+30,Y+166,7);
        for(int32 Tier=0;Tier<5&&R.RangeScores.IsValidIndex(Tier);++Tier)
            Label(Pending?FString::Printf(TEXT("至少%d分 → 到账%d金币"),Round.Values[Tier+1],Round.Payouts[Tier]):FString::Printf(TEXT("至少%d分 → %.3g倍"),R.RangeScores[Tier],R.RangeMultipliers[Tier]),X+30,Y+315+Tier*34,18,DemoUI::Gold);
        if(Pending) Button(TEXT("Casino.Collect"),TEXT("提前结束并领取当前成绩"),X+30,Y+510,485,40,Enabled,true);
    }
}
void ADemoHUD::DrawCasinoExplosion(float X,float Y,double Time)
{
    DEMO_LOG_TICK();
    const float T=FMath::Clamp(float(Time),0.f,3.f),Fade=FMath::Clamp(1.f-T/2.6f,0.f,1.f); // UI真实秒，结束后自然消隐。
    const float Shake=FMath::Sin(T*53.f)*8.f*Fade; // 局部中心震动，不影响相机和点击热区。
    for(int32 Cloud=0;Cloud<7;++Cloud)
    {
        const float Drift=(Cloud-3)*18.f*T,Size=20+T*19+(Cloud%3)*8; // 圆形烟团缓慢外扩上升，真实时间驱动。
        Panel(X+Drift-Size/2,Y-15-T*(20+Cloud*3),Size,Size,FLinearColor(.16f,.13f,.1f,Fade*.38f),Size/2);
    }
    for(int32 Layer=5;Layer>=0;--Layer)
    {
        const float Radius=18+Layer*12+T*28,Alpha=FMath::Clamp(1-T/1.1f,0.f,1.f); // 六层暖光形成短促爆炸核心，外层透明。
        Panel(X+Shake-Radius,Y-Radius*.65f,Radius*2,Radius*1.3f,FLinearColor(1.f,.28f+.1f*(5-Layer),.03f,Alpha*(.12f+.05f*(5-Layer))),Radius*.65f);
    }
    for(int32 Ring=0;Ring<3;++Ring)
    {
        const float Radius=15+FMath::Max(0.f,T-Ring*.16f)*95; // 分层冲击波，固定48线段无新资产。
        const FLinearColor Color(1.f,.22f+.15f*Ring,.03f,Fade*.8f); // 暖色爆炸，alpha衰减。
        for(int32 Segment=0;Segment<48;++Segment)
        {
            const float A=Segment*2*PI/48,B=(Segment+1)*2*PI/48; // 环段角度弧度。
            DrawLine((X+Shake+FMath::Cos(A)*Radius)*UIScale,(Y+FMath::Sin(A)*Radius*.55f)*UIScale,(X+Shake+FMath::Cos(B)*Radius)*UIScale,(Y+FMath::Sin(B)*Radius*.55f)*UIScale,Color,3*UIScale);
        }
    }
    for(int32 Index=0;Index<18;++Index)
    {
        const float A=Index*2.39996f,Speed=55+(Index%5)*28,DX=FMath::Cos(A)*Speed*T,DY=FMath::Sin(A)*Speed*T*.5f+T*T*18; // 固定视觉碎片，不消费游戏随机流。
        Panel(X+Shake+DX,Y+DY,3+(Index%3)*2,3+(Index%3)*2,FLinearColor(1.f,.4f,.08f,Fade),1);
    }
    if(T<.18f) Panel(X-75,Y-38,150,76,FLinearColor(1.f,.8f,.2f,1-T/.18f),20); // 仅面板中心短闪，不做全屏强闪。
}
