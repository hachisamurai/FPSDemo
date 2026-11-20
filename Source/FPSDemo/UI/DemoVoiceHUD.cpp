// 职责：S1 R02无底板队伍存活/发声简表与本机语音状态。DrawHUD局内调用，依赖公开Pawn状态、PS.Voice与Canvas；不录音、不发RPC、不持有玩家引用。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/DemoHUDReadout.h" // Boss队友生命仅使用原有限/合法比例格式，不以缺失或非法MaxHealth假报满血。
#include "UI/R02/DemoUITheme.h" // Boss队伍区借用同一只读前景主题，实况语音/公开HP仍来自既有组件。
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/Voice/DemoVoiceComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Game/DemoGameState.h" // Boss非歼灭目标仍占用132/160任务区，队友列表按真实Objective种类错开。
#include "Characters/DemoCharacter.h" // 只读公开阵亡标记，不读取队友私有钱包/成长。
#include "GAS/DemoAttributeSet.h" // 姓名旁细线只读取原公开HP，不推算或读取队友私有成长。
#include "Engine/World.h"
#include "Debug/DemoLog.h"

void ADemoHUD::DrawVoiceStatus(ADemoPlayerController* PC)
{
    DEMO_LOG_TICK();
    const ADemoPlayerState* Self = PC ? PC->GetPlayerState<ADemoPlayerState>() : nullptr; // 本人身份只读，PS尚未到达则不显示。
    if (!Self) return;
    const bool bR02=IsR02GameplayPresentation(); // 用户精简密度仅作用于当前R02游戏页型，保留旧变体入口。
    const bool bBoss=bR02&&GetR02LiveBoss()!=nullptr; // 只在真实活Boss存在时采用C07左上名单，常态队伍布局不改。
    const UDemoVoiceComponent* LocalVoice=Self->GetVoiceComponent(); // 语音暂未就绪不应吞掉Boss时的真实公开队伍信息。
    if(!LocalVoice&&!bBoss) return;
    const FLinearColor Ink(FColor(233,227,215)),Muted(FColor(110,115,119)),Ember(FColor(180,71,47)); // R02只读前景；阵亡只用小刻度/明文，不以色相猜测语音权限。
    const FString LocalStatus=LocalVoice?LocalVoice->GetLocalStatus().Left(24):FString(); // 原本机采集/禁用/错误文案，只读；本次不启动麦克风或推断语音权限。
    const bool bMenuOpen=PC->HasBlockingOverlay()||PC->IsUpgradeMenuOpen()||PC->IsNextLevelConfirmationOpen(); // CommonUI页已在DrawHUD返回；旧Canvas菜单也不叠本机提示。
    if(!bMenuOpen&&!LocalStatus.IsEmpty()&&(!bR02||(LocalVoice&&LocalVoice->HasActiveLocalStatus())))
    {
        Label(LocalStatus,32.f,ViewHeight-162.f,14.f,Ink); // 原本机采集/禁用/错误文案完整保留；靠统一Canvas文字描边在明亮场景读出，不铺底板。
    }
    const AGameStateBase* State = GetWorld()->GetGameState(); // PlayerArray来自服务器，最多四名参战者，不扫描世界全部Actor。
    if (!State) return;
    int32 Count = 0; // 最多三个队友；紧凑名单与生命板左缘对齐，不虚构队友生命数值。
    for (const APlayerState* Player : State->PlayerArray) // 仅本帧同步借用，不跨地图存储。
    {
        const ADemoPlayerState* Speaker = Cast<ADemoPlayerState>(Player); // 非项目PS不参与语音。
        if(Speaker&&Speaker!=Self)
        {
            const auto* Pawn=Cast<ADemoCharacter>(Speaker->GetPawn()); // 重生/复制到达前显示同步中，不能误报存活。
            const bool bTalking=Speaker->GetVoiceComponent()&&Speaker->GetVoiceComponent()->GetMouthOpenAmount()>0.f; // 实际语音幅度，非准备/禁言推断。
            const bool bEliminated=Pawn&&Pawn->IsCoopEliminated(); // 服务器公开标记，缺Pawn不猜测为存活。
            if(bBoss)
            {
                const ADemoGameState* Run=Cast<ADemoGameState>(State); // 真实非KillAll任务不被队友名单盖住，不隐藏或改写目标事实。
                const bool bHasObjective=Run&&Run->Objective.Kind!=EDemoObjectiveKind::KillAll;
                const float Y=(bHasObjective?184.f:120.f)+Count*46.f; // C07左上三人；非歼灭时避开132/160的真实任务文本/进度。
                const UDemoAttributeSet* Attributes=Pawn?Pawn->GetDemoAttributes():nullptr; // 原公开Health/MaxHealth，不向HUD增加队友经济/成长权限。
                Label(FString::Printf(TEXT("%02d %s"),Count+1,*Speaker->GetPlayerName().Left(9)),44.f,Y,18.f,R02HUDTheme->Ink);
                if(!Pawn||bEliminated||bTalking) Label(!Pawn?TEXT("同步"):bEliminated?TEXT("观战"):TEXT("语音"),262.f,Y+2.f,12.f,bEliminated?R02HUDTheme->Ember:R02HUDTheme->Muted);
                R02HUDLine(44.f,Y+27.f,202.f,Attributes&&!bEliminated?DemoHUDReadout::Fraction(Attributes->GetHealth(),Attributes->GetMaxHealth()):0.f,
                    bEliminated?R02HUDTheme->Ember:R02HUDTheme->Ink); // 真实生命细线，阵亡/缺值不冒充满血；单人没有任何虚构条目。
                if(++Count>=3) break;
                continue;
            }
            if(bR02)
            {
                const float Y=ViewHeight*.43f+Count*42.f; // 类似参考的左侧短名单，三人也不侵入左下大生命读数。
                const auto* Attributes=Pawn?Pawn->GetDemoAttributes():nullptr; // 只读公开Health/MaxHealth；缺失不冒充满血。
                Label(Speaker->GetPlayerName().Left(10),44,Y,13,Ink);
                if(!Pawn||bEliminated||bTalking) Label(!Pawn?TEXT("同步"):bEliminated?TEXT("观战"):TEXT("语音"),174,Y,12,Ink); // 常态不重复“存活”，特殊状态仍有明文。
                R02HUDLine(44,Y+22,100,Attributes&&Attributes->GetMaxHealth()>0.f&&!bEliminated?Attributes->GetHealth()/Attributes->GetMaxHealth():0.f,bEliminated?Ember:Ink);
                if(++Count>=3) break;
                continue;
            }
            const FString Status=!Pawn?TEXT("同步中"):bEliminated?TEXT("观战"):bTalking?TEXT("语音"):TEXT("存活"); // 使用短状态独立列，长名不把清关说明挤到中部技能区。
            const float Y=ViewHeight-250.f+Count*28.f; // 三人列表位于生命板上方；4:3视口只增加垂直留白，不叠到底部状态。
            if(Count==0) Label(TEXT("SQUAD / 小队"),32,ViewHeight-276.f,12,Muted);
            R02HUDLine(32,Y+14,4.f/UIScale,1.f,bEliminated?Ember:bTalking?Ink:Muted); // 原发声/生死事实的小刻度，姓名和明文状态仍可独立读出。
            Label(Speaker->GetPlayerName().Left(10),43,Y+5,16,Ink); // 保守最多10个中文宽度，完整名字仍在小队/房间/Tab目录查看。
            Label(Status,245,Y+5,14,Ink); // 固定状态列；不把明亮场景中的存活/阵亡理由降到低对比灰字。
            if(++Count>=3) break;
        }
    }
}
