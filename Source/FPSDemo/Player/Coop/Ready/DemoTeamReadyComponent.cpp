// 职责：事件驱动的合作出发门禁；依赖GM目录、终端和Flow，GS只复制脱敏展示值。
#include "Player/Coop/Ready/DemoTeamReadyComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/DemoGameState.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Player/Coop/DemoCoopRosterComponent.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Interaction/DemoTerminalComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Debug/DemoLog.h"

UDemoTeamReadyComponent::UDemoTeamReadyComponent()
{ DEMO_LOG_CALL(); PrimaryComponentTick.bCanEverTick=false; }

TArray<ADemoPlayerController*> UDemoTeamReadyComponent::GetMembers() const
{
    DEMO_LOG_TICK();
    const AFPSDemoGameMode* Mode=Cast<AFPSDemoGameMode>(GetOwner()); // 同World同步借用；GM组件不缓存旅行前控制器。
    return Mode&&Mode->GetCoopRoster()?Mode->GetCoopRoster()->GetControllers():TArray<ADemoPlayerController*>();
}

FString UDemoTeamReadyComponent::GetBlockReason(ADemoPlayerController* Player,bool bRequireTerminal) const
{
    DEMO_LOG_TICK();
    const AFPSDemoGameMode* Mode=Cast<AFPSDemoGameMode>(GetOwner()); // 所有检查都在服务器执行，不信任HUD按钮。
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 启动/卸载允许为空。
    if(!Mode||!Mode->HasAuthority()||!State) return TEXT("战局尚未就绪");
    if(State->Phase!=EDemoPhase::Hub&&State->Phase!=EDemoPhase::Intermission) return TEXT("仅安全区或关间备战可以准备");
    if(GetWorld()->IsPaused()) return TEXT("游戏暂停中");
    if(!Player||!Player->IsCoopRegistered()) return TEXT("等待队伍握手完成");
    const ADemoCharacter* Pawn=IsValid(Player)?Cast<ADemoCharacter>(Player->GetPawn()):nullptr; // 缺Pawn也属于目录阻塞项。
    const ADemoPlayerState* PS=Player?Player->GetPlayerState<ADemoPlayerState>():nullptr; // 身份来自拥有者PC。
    if(!Pawn||!PS||Pawn->GetController()!=Player||!Pawn->GetDemoASC()||Pawn->GetDemoASC()->GetAvatarActor()!=Pawn)
        return TEXT("角色或能力系统初始化中");
    if(!Pawn->GetWeaponComponent()||!Pawn->GetWeaponComponent()->GetActiveWeapon()) return TEXT("武器初始化中");
    if(Pawn->IsCoopEliminated()||!Pawn->GetDemoAttributes()||Pawn->GetDemoAttributes()->GetHealth()<=0.f) return TEXT("等待复活");
    if(Mode->GetRunFlow()->UsesDemoCampaign()&&!PS->GetRunBuildComponent()->HasSelectedCore()) return TEXT("尚未选择流派核心");
    if(State->LevelNumber>0&&(PS->GetProgressComponent()->GetSnapshot().LastRewardLevel<State->LevelNumber||PS->GetRunBuildComponent()->GetSnapshot().PendingBranchStage!=0))
        return TEXT("尚未领取本关奖励");
    if(bRequireTerminal)
    {
        const FString TerminalError=Mode->GetTerminalSystem()->ValidateSession(Pawn,false); // 校验当前终端代数、距离、生命和阶段。
        if(!TerminalError.IsEmpty()) return TEXT("请打开下一关终端并留在附近");
    }
    return FString();
}

FString UDemoTeamReadyComponent::GetPreparationFingerprint(ADemoPlayerController* Player) const
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Pawn=Player?Cast<ADemoCharacter>(Player->GetPawn()):nullptr; // 只读当前Pawn的实际装配。
    const ADemoPlayerState* PS=Player?Player->GetPlayerState<ADemoPlayerState>():nullptr; // Revision来自本人的权威组件。
    const UDemoWeaponComponent* Weapons=Pawn?Pawn->GetWeaponComponent():nullptr; // 不序列化全库存或弹药数量，运行时补给不影响准备。
    const UDemoAmmoComponent* Ammo=Pawn?Pawn->FindComponentByClass<UDemoAmmoComponent>():nullptr; // 弹药配置选择可经旧接口变更。
    if(!PS||!Weapons||!Ammo) return FString();
    return FString::Printf(TEXT("%u:%d:%d:%d:%d:%d"),PS->GetUniqueID(),PS->GetProgressComponent()->GetSnapshot().Revision,PS->GetRunBuildComponent()->GetSnapshot().Revision,Weapons->GetPrimaryIndex(),Weapons->GetActiveSlot(),Ammo->GetSelected()); // PS身份替换即使Revision碰巧相同，也不能继承旧确认。
}

void UDemoTeamReadyComponent::InvalidateAll(const FString& Reason)
{
    DEMO_LOG_CALL();
    if(!GetOwner()->HasAuthority()) return;
    ReadyPlayers.Reset(); ReadyFingerprints.Reset(); Revision=Revision==MAX_int32?1:Revision+1; LastReason=Reason;
    UE_LOG(LogFPSDemo,Log,TEXT("TEAM_READY_INVALIDATE_ALL revision=%d reason=%s"),Revision,*Reason);
    Publish(false);
}

void UDemoTeamReadyComponent::InvalidatePlayer(ADemoPlayerController* Player,const FString& Reason)
{
    DEMO_LOG_CALL();
    if(!GetOwner()->HasAuthority()) return;
    ReadyPlayers.Remove(Player); ReadyFingerprints.Remove(Player); Revision=Revision==MAX_int32?1:Revision+1; LastReason=Reason;
    UE_LOG(LogFPSDemo,Log,TEXT("TEAM_READY_INVALIDATE player=%s revision=%d reason=%s"),*GetNameSafe(Player),Revision,*Reason);
    Publish(false);
}

void UDemoTeamReadyComponent::Refresh()
{ DEMO_LOG_CALL(); Publish(true); }

bool UDemoTeamReadyComponent::SetReady(ADemoPlayerController* Player,bool bReady,int32 ExpectedRevision,int32 ExpectedLevel,FString& Error)
{
    DEMO_LOG_CALL(); Error.Empty();
    Publish(true); // 先发现Avatar/上下文失效，再比对旧客户端版本，不能接受先设置后撤销的假成功。
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 版本与关号同时绑定，不能旧页给下一关准备。
    if(!GetOwner()->HasAuthority()||!GetMembers().Contains(Player)||!State) Error=TEXT("玩家尚未完成队伍登记");
    else if(ExpectedRevision!=Revision||ExpectedLevel!=State->LevelNumber) Error=TEXT("队伍状态已变化，请按最新状态重新确认");
    else if(State->Phase!=EDemoPhase::Hub&&State->Phase!=EDemoPhase::Intermission) Error=TEXT("当前不在备战阶段");
    else if(bReady) Error=GetBlockReason(Player,true);
    if(!Error.IsEmpty()) { UE_LOG(LogFPSDemo,Log,TEXT("TEAM_READY_REJECT %s"),*Error); Publish(true); return false; }
    if(bReady) { ReadyPlayers.Add(Player,Cast<ADemoCharacter>(Player->GetPawn())); ReadyFingerprints.Add(Player,GetPreparationFingerprint(Player)); }
    else { ReadyPlayers.Remove(Player); ReadyFingerprints.Remove(Player); }
    Revision=Revision==MAX_int32?1:Revision+1; LastReason=bReady?TEXT("队员已准备"):TEXT("队员取消准备");
    UE_LOG(LogFPSDemo,Log,TEXT("TEAM_READY_SET player=%s ready=%d revision=%d"),*GetNameSafe(Player),bReady,Revision);
    Publish(true); return true;
}

bool UDemoTeamReadyComponent::ValidateDeparture(FString& Error)
{
    DEMO_LOG_CALL(); Publish(true);
    const ADemoGameState* State=GetWorld()->GetGameState<ADemoGameState>(); // 刷新后读取唯一权威投影。
    Error=State?State->TeamReady.BlockReason:TEXT("战局尚未就绪");
    if(!State||!State->TeamReady.bCanDepart) { if(Error.IsEmpty()) Error=TEXT("队伍尚未准备完成"); UE_LOG(LogFPSDemo,Log,TEXT("TEAM_DEPARTURE_REJECT %s"),*Error); return false; }
    return true;
}

void UDemoTeamReadyComponent::Publish(bool bRecheck)
{
    DEMO_LOG_TICK();
    UWorld* World=GetWorld(); // World结束期间不复活Timer或访问网络。
    ADemoGameState* State=World?World->GetGameState<ADemoGameState>():nullptr; // 组件构造期间GS可能尚不存在。
    if(!State||!GetOwner()->HasAuthority()||World->bIsTearingDown) return;
    const TArray<ADemoPlayerController*> Members=GetMembers(); // 最多4个在线身份，包含缺Pawn成员。
    const bool bRequired=Members.Num()>1; // 一个玩家继续沿用原交互，不强迫额外按准备。
    const AFPSDemoGameMode* Mode=Cast<AFPSDemoGameMode>(GetOwner()); // 同World流程身份是准备上下文的一部分。
    FString NewContext=FString::Printf(TEXT("%s:%d:%d:%d:%d"),Mode?*Mode->GetRunFlow()->GetRunId():TEXT(""),static_cast<int32>(State->Phase),State->LevelNumber,static_cast<int32>(State->Difficulty),State->bEndless); // 不复制RunId。
    for(const ADemoPlayerController* Member:Members) NewContext+=FString::Printf(TEXT(":%u"),Member->GetUniqueID()); // GetMembers排序稳定，离开重连生成新对象必须重确认。
    if(NewContext!=ContextKey)
    {
        ContextKey=MoveTemp(NewContext); ReadyPlayers.Reset(); ReadyFingerprints.Reset(); Revision=Revision==MAX_int32?1:Revision+1;
        LastReason=TEXT("战局或队伍变化，请重新准备"); UE_LOG(LogFPSDemo,Log,TEXT("TEAM_READY_CONTEXT revision=%d"),Revision);
    }
    if(bRecheck)
    {
        bool bRemoved=false; // 一次刷新只递增一次版本，不因每人失效多次发布。
        for(auto It=ReadyPlayers.CreateIterator();It;++It)
        {
            ADemoPlayerController* PC=It.Key().Get(); // 弱引用不延长控制器生命周期。
            if(!Members.Contains(PC)||!PC||It.Value().Get()!=PC->GetPawn()||!GetBlockReason(PC,bRequired).IsEmpty()
                ||ReadyFingerprints.FindRef(PC)!=GetPreparationFingerprint(PC)) { ReadyFingerprints.Remove(PC); It.RemoveCurrent(); bRemoved=true; }
        }
        if(bRemoved) { Revision=Revision==MAX_int32?1:Revision+1; LastReason=TEXT("玩家、角色或终端条件变化，已撤销准备"); UE_LOG(LogFPSDemo,Log,TEXT("TEAM_READY_EXPIRED revision=%d"),Revision); }
    }
    FDemoTeamReadySnapshot Next; // 只包含公开队伍字段，不复制成长或固定身份键。
    Next.Revision=Revision; Next.bRequired=bRequired; Next.bCanDepart=!Members.IsEmpty(); Next.LastChangeReason=LastReason;
    for(ADemoPlayerController* PC:Members)
    {
        FDemoTeamReadyMember Item; // 当前身份的可读阻塞状态。
        Item.Player=PC?PC->GetPlayerState<ADemoPlayerState>():nullptr; Item.Name=Item.Player?Item.Player->GetPlayerName().Left(32):TEXT("玩家初始化中");
        Item.CoreId=Item.Player?Item.Player->GetRunBuildComponent()->GetSnapshot().CoreId:NAME_None;
        Item.BlockReason=GetBlockReason(PC,bRequired); Item.bReady=ReadyPlayers.Contains(PC)&&Item.BlockReason.IsEmpty();
        if(!Item.BlockReason.IsEmpty()||(bRequired&&!Item.bReady))
        { Next.bCanDepart=false; if(Next.BlockReason.IsEmpty()) Next.BlockReason=Item.Name+TEXT("：")+(Item.BlockReason.IsEmpty()?TEXT("尚未准备"):Item.BlockReason); }
        Next.Members.Add(Item);
    }
    if(Members.IsEmpty()) Next.BlockReason=TEXT("等待玩家登记");
    bool bChanged=State->TeamReady.Revision!=Next.Revision||State->TeamReady.bRequired!=Next.bRequired||State->TeamReady.bCanDepart!=Next.bCanDepart
        ||State->TeamReady.BlockReason!=Next.BlockReason||State->TeamReady.LastChangeReason!=Next.LastChangeReason||State->TeamReady.Members.Num()!=Next.Members.Num(); // 没有变化的2Hz检查不产生网络广播。
    for(int32 Index=0;!bChanged&&Index<Next.Members.Num();++Index)
    {
        const FDemoTeamReadyMember& Old=State->TeamReady.Members[Index]; const FDemoTeamReadyMember& New=Next.Members[Index]; // 同序公开项逐字段比较，无反射对象序列化开销。
        bChanged=Old.Player!=New.Player||Old.Name!=New.Name||Old.CoreId!=New.CoreId||Old.bReady!=New.bReady||Old.BlockReason!=New.BlockReason;
    }
    if(bChanged) { State->TeamReady=MoveTemp(Next); State->ForceNetUpdate(); }
    if(ReadyPlayers.IsEmpty()) World->GetTimerManager().ClearTimer(CheckTimer);
    else if(!World->GetTimerManager().IsTimerActive(CheckTimer)) World->GetTimerManager().SetTimer(CheckTimer,this,&UDemoTeamReadyComponent::RecheckReadyMembers,.5f,true);
}

void UDemoTeamReadyComponent::RecheckReadyMembers()
{ DEMO_LOG_TICK(); Publish(true); }

void UDemoTeamReadyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL(); if(GetWorld()) GetWorld()->GetTimerManager().ClearTimer(CheckTimer); ReadyPlayers.Reset(); ReadyFingerprints.Reset(); Super::EndPlay(EndPlayReason);
}
