// 职责：验证并生成可重复的十关内容计划。使用：服务器准备阶段调用；不创建Actor、不发奖，随机不依赖全局FRand状态。
#include "Game/Content/DemoCampaignDefinition.h"
#include "Debug/DemoLog.h"

namespace
{
/** 稳定ID仅允许1..64位ASCII字母、数字、下划线和连字符；不接受路径或None，规则与V9 API一致。 */
bool IsCampaignIdToken(FName Id)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    const FString Text = Id.ToString(); // 有界作者标识，不作为资源路径使用。
    if(Id.IsNone() || Text.IsEmpty() || Text.Len()>64) return false;
    for(TCHAR Character:Text)
        if(!((Character>='A' && Character<='Z') || (Character>='a' && Character<='z') || (Character>='0' && Character<='9') || Character=='_' || Character=='-')) return false;
    return true;
}
}

UDemoCampaignDefinition::UDemoCampaignDefinition()
{
    DEMO_LOG_CALL();
    LocalDeviceAnchors={FVector(300,-650,0),FVector(300,650,0)};
    LocalControlAnchors={FVector(-400,-600,0),FVector(500,0,0),FVector(-400,600,0)};
    RoomAreas={{TEXT("R01"),0},{TEXT("R02"),1},{TEXT("R03"),2},{TEXT("R04"),0},{TEXT("R05"),1},{TEXT("R06"),2},{TEXT("B01"),0},{TEXT("B02"),2}};
    RoomBossRows={{TEXT("B01"),TEXT("Warden")},{TEXT("B02"),TEXT("Weaver")}};
    Stages.SetNum(10);
    Stages[0].RoomPool={TEXT("R01")};
    Stages[1].RoomPool={TEXT("R02")}; Stages[1].Objective=EDemoObjectiveKind::Sabotage;
    Stages[2].RoomPool={TEXT("R03"),TEXT("R05")};
    Stages[3].RoomPool={TEXT("R01"),TEXT("R05")}; Stages[3].Objective=EDemoObjectiveKind::Sabotage;
    Stages[4].RoomPool={TEXT("R04")}; Stages[4].Objective=EDemoObjectiveKind::RotatingControl;
    Stages[5].RoomPool={TEXT("R02"),TEXT("R05")}; Stages[5].Objective=EDemoObjectiveKind::Sabotage;
    Stages[6].RoomPool={TEXT("R03"),TEXT("R06")};
    Stages[7].RoomPool={TEXT("R04"),TEXT("R06")}; Stages[7].Objective=EDemoObjectiveKind::RotatingControl;
    Stages[8].RoomPool={TEXT("R01"),TEXT("R03"),TEXT("R05")};
    Stages[9].RoomPool={TEXT("B01"),TEXT("B02")};
    Party.SetNum(4);
    for(int32 Index=0;Index<Party.Num();++Index) // 固定四档初调，后续DataAsset独立调参。
    {
        Party[Index].MinionHealth=1.f+.35f*Index; Party[Index].MinionCount=1.f+.6f*Index;
        Party[Index].BossHealth=1.f+.7f*Index; Party[Index].DeviceHealth=1.f+.5f*Index; Party[Index].MaxAlive=8+2*Index;
    }
}
bool UDemoCampaignDefinition::Validate(FString& Error) const
{
    DEMO_LOG_CALL();
    Error.Reset();
    if(CampaignId!=TEXT("steam_demo") || ContentRevision<1 || Stages.Num()!=10 || Party.Num()!=4
        || !FMath::IsFinite(DeviceBaseHealth) || DeviceBaseHealth<1.f || DeviceBaseHealth>100000.f
        || !FMath::IsFinite(ObjectiveCountMultiplier) || ObjectiveCountMultiplier<1.f || ObjectiveCountMultiplier>3.f)
    { Error=TEXT("Demo内容身份、版本、十关配置或目标倍率无效"); UE_LOG(LogFPSDemo,Warning,TEXT("CONTENT_INVALID %s"),*Error); return false; }
    if(RoomAreas.IsEmpty() || RoomAreas.Num()>64 || RoomBossRows.IsEmpty() || RoomBossRows.Num()>64)
    { Error=TEXT("Demo区域与Boss映射必须为1..64项"); UE_LOG(LogFPSDemo,Warning,TEXT("CONTENT_INVALID %s"),*Error); return false; }
    for(const TPair<FName,int32>& Pair:RoomAreas) // 所有作者字段都校验，不能只检查当前十关使用到的子集。
        if(!IsCampaignIdToken(Pair.Key) || Pair.Value<0 || Pair.Value>2)
        { Error=TEXT("Demo区域映射ID或区域越界"); UE_LOG(LogFPSDemo,Warning,TEXT("CONTENT_INVALID %s"),*Error); return false; }
    for(const TPair<FName,FName>& Pair:RoomBossRows)
        if(!RoomAreas.Contains(Pair.Key) || !IsCampaignIdToken(Pair.Value))
        { Error=TEXT("Demo Boss行缺少有效区域或行ID"); UE_LOG(LogFPSDemo,Warning,TEXT("CONTENT_INVALID %s"),*Error); return false; }
    if(LocalDeviceAnchors.Num()!=2 || LocalControlAnchors.Num()!=3
        || !FMath::IsFinite(ControlSeconds) || ControlSeconds<1.f || ControlSeconds>600.f
        || !FMath::IsFinite(TransitionSeconds) || TransitionSeconds<1.f || TransitionSeconds>30.f
        || !FMath::IsFinite(ControlRadius) || ControlRadius<100.f || ControlRadius>2000.f)
    { Error=TEXT("Demo目标锚点数量、占领秒数、迁移秒数或半径无效"); UE_LOG(LogFPSDemo,Warning,TEXT("CONTENT_INVALID %s"),*Error); return false; }
    for(const TArray<FVector>* Anchors:{&LocalDeviceAnchors,&LocalControlAnchors}) // 作者输入只允许当前有限白模区域附近的位置。
        for(const FVector& Anchor:*Anchors)
            if(Anchor.ContainsNaN() || Anchor.GetAbsMax()>10000.)
            { Error=TEXT("Demo目标局部锚点包含非有限值或超出100米范围"); UE_LOG(LogFPSDemo,Warning,TEXT("CONTENT_INVALID %s"),*Error); return false; }
    for(int32 Index=0;Index<Stages.Num();++Index) // 内容验证是准备时操作，不在逐帧执行。
    {
        const FDemoCampaignStage& Row=Stages[Index]; // 仅借用到函数返回，不保存可变表指针。
        if(Row.RoomPool.IsEmpty() || Row.RoomPool.Num()>8 || static_cast<uint8>(Row.Objective)>static_cast<uint8>(EDemoObjectiveKind::RotatingControl)
            || (Index==9 && Row.Objective!=EDemoObjectiveKind::KillAll))
        { Error=FString::Printf(TEXT("Demo第%d关目标或房间池无效"),Index+1); UE_LOG(LogFPSDemo,Warning,TEXT("%s"),*Error); return false; }
        TSet<FName> Seen; // 有界去重，避免重复ID无意改变抽样权重。
        for(FName Room:Row.RoomPool)
        {
            const int32* Area=RoomAreas.Find(Room); // 区域身份来自作者配置，客户端不可指定路径。
            if(Room.IsNone() || Seen.Contains(Room) || !Area || *Area<0 || *Area>2)
            { Error=TEXT("Demo房间ID重复、缺少区域适配或区域越界"); UE_LOG(LogFPSDemo,Warning,TEXT("%s"),*Error); return false; }
            Seen.Add(Room);
            if(Index==9 && (!RoomBossRows.Contains(Room)||RoomBossRows.FindChecked(Room).IsNone()))
            { Error=TEXT("Demo终局房间缺少Boss行映射"); UE_LOG(LogFPSDemo,Warning,TEXT("CONTENT_INVALID %s"),*Error); return false; }
        }
    }
    for(const FDemoPartyScaling& Scale:Party)
    {
        for(float Value:{Scale.MinionHealth,Scale.MinionCount,Scale.BossHealth,Scale.DeviceHealth})
            if(!FMath::IsFinite(Value)||Value<1.f||Value>10.f)
            { Error=TEXT("Demo人数倍率须为1到10的有限值"); UE_LOG(LogFPSDemo,Warning,TEXT("%s"),*Error); return false; }
        if(Scale.MaxAlive<4||Scale.MaxAlive>16)
        { Error=TEXT("Demo并发数量须为4到16"); UE_LOG(LogFPSDemo,Warning,TEXT("%s"),*Error); return false; }
    }
    return true;
}
bool UDemoCampaignDefinition::BuildPlan(int32 Seed,int32 Stage,int32 Players,FDemoCampaignPlan& Out,FString& Error) const
{
    DEMO_LOG_CALL();
    if(!Validate(Error)) return false;
    if(Stage<1||Stage>10||Players<1||Players>4)
    { Error=TEXT("Demo关号或参战人数越界"); UE_LOG(LogFPSDemo,Warning,TEXT("%s"),*Error); return false; }
    const FDemoCampaignStage& Row=Stages[Stage-1]; // 当前关只读定义。
    FRandomStream Random(static_cast<int32>(static_cast<uint32>(Seed) ^ (static_cast<uint32>(Stage)*2654435761u))); // 稳定无符号混合，避免有符号溢出。
    FDemoCampaignPlan Candidate; // 全部验证通过后一次提交输出。
    Candidate.CampaignId=CampaignId; Candidate.ContentRevision=ContentRevision; Candidate.RunSeed=Seed; Candidate.Stage=Stage;
    Candidate.RoomId=Row.RoomPool[Random.RandRange(0,Row.RoomPool.Num()-1)]; Candidate.Variant=Random.RandRange(0,1);
    Candidate.Objective=Row.Objective; Candidate.PartySize=Players; Candidate.ArenaIndex=RoomAreas.FindChecked(Candidate.RoomId);
    Out=Candidate;
    return true;
}
