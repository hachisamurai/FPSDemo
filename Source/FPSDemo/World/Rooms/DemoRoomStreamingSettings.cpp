// 职责：校验安装房间目录并生成网络一致的流送请求；视觉入口另在GT按可信配置加载Profile，不改网络请求或存档。
#include "World/Rooms/DemoRoomStreamingSettings.h"
#include "World/Rooms/DemoRoomAtmosphereProfile.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h" // 无UObject的标记校验保留高频入口埋点，默认不输出逐帧日志。

bool UDemoRoomStreamingSettings::UsesPhaseAAtmosphere(FName RoomId) const
{
    DEMO_LOG_TICK();
    // Presence而非路径有效性决定是否接管：显式坏路径必须REJECT，不能悄悄当作未启用并通过验收。
    return RoomId==FName(TEXT("D03_L01")) && Rooms.Contains(RoomId) && RoomAtmospheres.Contains(RoomId);
}

const UDemoRoomAtmosphereProfile* UDemoRoomStreamingSettings::ResolveAtmosphere(FName RoomId, FString& Error) const
{
    DEMO_LOG_CALL(); Error.Reset();
    if (!UsesPhaseAAtmosphere(RoomId))
    { Error=TEXT("Room outside explicit Phase A scope"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT room=%s reason=%s"),*RoomId.ToString(),*Error); return nullptr; }
    const TSoftObjectPtr<UDemoRoomAtmosphereProfile>* Selected=RoomAtmospheres.Find(RoomId); // 同步借用CDO；不接受网络传入路径。
    if (!Selected)
    { Error=TEXT("Explicit Phase A binding disappeared"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=%s"),*Error); return nullptr; }
    const FSoftObjectPath Path=Selected->ToSoftObjectPath(); // 必须是安装的项目资源，不支持子对象或任意外部路径。
    if (!Path.IsValid() || !Path.GetLongPackageName().StartsWith(TEXT("/Game/")) || !Path.GetSubPathString().IsEmpty())
    { Error=TEXT("Invalid configured atmosphere asset path"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT reason=%s"),*Error); return nullptr; }
    const UDemoRoomAtmosphereProfile* Profile=Selected->LoadSynchronous(); // 每锚点一次GT加载；Anchor用UPROPERTY持有后续生命周期。
    if (!Profile) { Error=TEXT("Configured atmosphere asset missing or wrong class"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ATMO_REJECT asset=%s reason=%s"),*Path.ToString(),*Error); }
    return Profile;
}

namespace
{
/** 配置标识仅允许短ASCII字母数字下划线，禁止把RoomId变成任意包路径。 */
bool IsRoomToken(FName Name)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    const FString Value = Name.ToString(); // 本地有界标识，不进入路径解析前缀。
    if (Name.IsNone() || Value.Len() > 48) return false;
    for (TCHAR Letter : Value)
        if (!((Letter >= 'A' && Letter <= 'Z') || (Letter >= 'a' && Letter <= 'z') || (Letter >= '0' && Letter <= '9') || Letter == '_')) return false;
    return true;
}

/** 可信作者标记允许点分层和短ASCII，不接受资源路径；仅验证配置，不授予客户端选择Actor权限。 */
bool IsRoomMarkerTag(FName Name)
{
    DEMO_LOG_STATIC_TICK(nullptr);
    const FString Value=Name.ToString(); // 同步有界值，不跨配置刷新缓存。
    if (Name.IsNone() || Value.Len()>160) return false;
    for (TCHAR Letter:Value) if (!((Letter>='A' && Letter<='Z') || (Letter>='a' && Letter<='z') || (Letter>='0' && Letter<='9') || Letter=='_' || Letter=='-' || Letter=='.')) return false;
    return true;
}
}

bool UDemoRoomStreamingSettings::BuildRequest(FName RoomId, int32 Generation, int32 ArenaIndex, FDemoRoomStreamRequest& Out, FString& Error) const
{
    DEMO_LOG_CALL();
    Out = FDemoRoomStreamRequest(); Error.Reset();
    const FDemoRoomStreamDefinition* Definition = Rooms.Find(RoomId); // 同步借用作者白名单，缺失不能降级旧场景。
    if (!bEnabled || !Definition) { Error = TEXT("房间流送未启用或缺少房间目录配置"); UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_CONFIG %s"), *Error); return false; }
    Out.RoomId = RoomId; Out.Generation = Generation; Out.ArenaIndex = ArenaIndex; Out.Map = Definition->Map.ToSoftObjectPath();
    Out.bRequireNavigation = Definition->bRequireNavigation; Out.RequiredActorTags = Definition->RequiredActorTags;
    Out.InstanceName = FName(*FString::Printf(TEXT("DemoRoom_%d_%s"), Generation, *RoomId.ToString()));
    if (SlotLocations.Num()!=2 || SlotLocations[0].ContainsNaN() || SlotLocations[1].ContainsNaN()) { Error=TEXT("房间安装配置必须恰有两个有限空间槽"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_CONFIG %s"),*Error); return false; }
    Out.Transform = FTransform(SlotLocations[(Generation&1)?0:1]); // 保原奇数首槽规则；Preparation有Active时统一UseOtherSlot，重试不占脚下槽。
    return ValidateRequest(Out, Error);
}

bool UDemoRoomStreamingSettings::ValidateRequest(const FDemoRoomStreamRequest& Request, FString& Error) const
{
    DEMO_LOG_CALL();
    Error.Reset();
    const FDemoRoomStreamDefinition* Definition = Rooms.Find(Request.RoomId); // 客户端必须安装相同目录；不因bEnabled本机值不同而相信任意路径。
    const FVector Location = Request.Transform.GetLocation(); // 仅两个预留空间，cm；作者锚点仍必须在自身子关卡范围。
    if (SlotLocations.Num()!=2 || SlotLocations[0].ContainsNaN() || SlotLocations[1].ContainsNaN()
        || SlotLocations[0].GetAbsMax()>1000000. || SlotLocations[1].GetAbsMax()>1000000.
        || FMath::Abs(SlotLocations[0].Z-10000.)>1. || FMath::Abs(SlotLocations[1].Z-10000.)>1.
        || FVector::Dist2D(SlotLocations[0],SlotLocations[1])<10000.f)
    { Error=TEXT("房间空间槽必须为两个有限、分离且Z=10000cm的安装位置"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_CONFIG %s"),*Error); return false; }
    if (Rooms.Num() > 32 || !Definition || !IsRoomToken(Request.RoomId) || Request.Generation <= 0 || Request.ArenaIndex < 0 || Request.ArenaIndex > 2
        || !Request.Map.IsValid() || Request.Map.ToString().Len() > 256 || !Request.Map.GetLongPackageName().StartsWith(TEXT("/Game/"))
        || !Request.Map.GetSubPathString().IsEmpty() || Request.Map != Definition->Map.ToSoftObjectPath()
        || Request.bRequireNavigation != Definition->bRequireNavigation || Request.RequiredActorTags != Definition->RequiredActorTags
        || Request.RequiredActorTags.Num() > 16 || Request.Transform.ContainsNaN()
        || !Request.Transform.GetScale3D().Equals(FVector::OneVector) || !Request.Transform.GetRotation().Equals(FQuat::Identity)
        || (!Location.Equals(SlotLocations[0]) && !Location.Equals(SlotLocations[1]))
        || Request.InstanceName != FName(*FString::Printf(TEXT("DemoRoom_%d_%s"), Request.Generation, *Request.RoomId.ToString())))
    { Error = TEXT("房间身份、地图白名单、空间槽或准备参数不合法"); UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_REQUEST %s"), *Error); return false; }
    TSet<FName> Unique; // 至多16标签，同一个标签不能重复充当多个关键对象。
    for (FName Tag : Request.RequiredActorTags)
    {
        if (!IsRoomToken(Tag) || Unique.Contains(Tag)) { Error = TEXT("关键房间对象标签非法或重复"); UE_LOG(LogFPSDemo, Warning, TEXT("ROOM_REQUEST %s"), *Error); return false; }
        Unique.Add(Tag);
    }
    FBox Bounds; // 本地目录独立校验；请求端没有新增可放宽几何的字段。
    return GetLocalBounds(Request.RoomId,Bounds,Error);
}

bool UDemoRoomStreamingSettings::UseOtherSlot(const FTransform& ActiveTransform,FDemoRoomStreamRequest& Request,FString& Error) const
{
    DEMO_LOG_CALL();
    if (SlotLocations.Num()!=2 || SlotLocations[0].ContainsNaN() || SlotLocations[1].ContainsNaN()) { Error=TEXT("房间空间槽配置未就绪"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_SLOT %s"),*Error); return false; }
    const int32 Current=ActiveTransform.GetLocation().Equals(SlotLocations[0])?0:ActiveTransform.GetLocation().Equals(SlotLocations[1])?1:INDEX_NONE; // 只认现有安装槽，不猜测距离或旧常数中点。
    if (Current==INDEX_NONE) { Error=TEXT("当前房间不属于已安装空间槽，不能安全准备下一图"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_SLOT %s"),*Error); return false; }
    Request.Transform=FTransform(SlotLocations[1-Current]);
    return ValidateRequest(Request,Error);
}

bool UDemoRoomStreamingSettings::GetLocalBounds(FName RoomId,FBox& Out,FString& Error) const
{
    DEMO_LOG_TICK(); Out=FBox(ForceInit);
    const FDemoRoomStreamDefinition* Definition=Rooms.Find(RoomId); // 配置拥有，当前调用只读借用。
    if (!Definition || Definition->LocalBoundsMin.ContainsNaN() || Definition->LocalBoundsMax.ContainsNaN()
        || Definition->LocalBoundsMin.GetAbsMax()>100000. || Definition->LocalBoundsMax.GetAbsMax()>100000.
        || Definition->LocalBoundsMax.X<=Definition->LocalBoundsMin.X || Definition->LocalBoundsMax.Y<=Definition->LocalBoundsMin.Y || Definition->LocalBoundsMax.Z<=Definition->LocalBoundsMin.Z)
    { Error=TEXT("房间真实局部占地配置非法或超过1000m有界预算"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_BOUNDS %s id=%s"),*Error,*RoomId.ToString()); return false; }
    if (!FMath::IsFinite(Definition->PlayerEntryExclusionRadiusCm) || Definition->PlayerEntryExclusionRadiusCm < 0.f || Definition->PlayerEntryExclusionRadiusCm > 5000.f)
    { Error=TEXT("玩家战斗入场禁刷半径须为0至5000厘米的有限数"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_ENTRY_RADIUS room=%s %s"),*RoomId.ToString(),*Error); return false; }
    if (Definition->bUseAuthoredBounds)
    {
        TSet<FName> Seen; // 八个唯一生成标记加一个Boss标记，Actor本身稍后在所属Level精确核对。
        if (Definition->SpawnPointTags.Num()!=8 || !IsRoomMarkerTag(Definition->BossSpawnTag))
        { Error=TEXT("作者房间需要八个出生标记和唯一Boss标记"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_MARKERS room=%s %s"),*RoomId.ToString(),*Error); return false; }
        Seen.Add(Definition->BossSpawnTag);
        for (FName Tag:Definition->SpawnPointTags)
        {
            if (!IsRoomMarkerTag(Tag) || Seen.Contains(Tag)) { Error=TEXT("作者出生标记非法或重复"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_MARKERS room=%s %s"),*RoomId.ToString(),*Error); return false; }
            Seen.Add(Tag);
        }
    }
    else if (!Definition->SpawnPointTags.IsEmpty() || !Definition->BossSpawnTag.IsNone())
    { Error=TEXT("旧房间不能静默启用作者出生点"); UE_LOG(LogFPSDemo,Warning,TEXT("ROOM_MARKERS room=%s %s"),*RoomId.ToString(),*Error); return false; }
    Out=FBox(Definition->LocalBoundsMin,Definition->LocalBoundsMax); return true;
}
