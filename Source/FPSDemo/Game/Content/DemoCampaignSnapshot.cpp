// 职责：V9战役纯值快照的规范序列化、完整校验及V8迁移。使用：Flow/存档编解码调用；依赖Core SHA1和Json，绝不从快照加载资源。
#include "Game/Content/DemoCampaignDefinition.h"
#include "Debug/DemoLog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Misc/SecureHash.h"
#include "Containers/StringConv.h"

namespace DemoCampaignSnapshot
{
constexpr int32 MaxBytes = 64 * 1024; // UTF8硬上限；JSON字符串进入解析器前检查，防止无界分配。
constexpr int32 SchemaVersion = 1; // 唯一代码解释边界；作者ContentRevision可独立增加。
using FWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

/** 仅比较ASCII稳定ID，大小写敏感序对应后端Ordinal；排序只在同步导出调用，不依赖本机文化设置。 */
bool LessId(const FString& A, const FString& B)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    return A.Compare(B, ESearchCase::CaseSensitive)<0;
}

/** 序列化器前限制嵌套深度；忽略字符串内括号，完整语法仍由Json库判断，最多八层足够schema1。 */
bool HasBoundedNesting(const FString& Json)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    int32 Depth=0; // JSON容器当前深度，防止小体积但极深的无效输入。
    bool bString=false, bEscaped=false; // 只用于跳过带转义的字符串，不自行实现JSON语义。
    for(TCHAR Character:Json)
    {
        if(bString)
        {
            if(bEscaped) bEscaped=false;
            else if(Character=='\\') bEscaped=true;
            else if(Character=='"') bString=false;
        }
        else if(Character=='"') bString=true;
        else if(Character=='{' || Character=='[') { if(++Depth>8) return false; }
        else if(Character=='}' || Character==']') { if(--Depth<0) return false; }
    }
    return Depth==0 && !bString;
}

/** 所有解析拒绝均使用同一诊断，不输出存档正文，避免日志放大。 */
bool Reject(FString& Error, const TCHAR* Reason)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    Error = Reason;
    UE_LOG(LogFPSDemo, Warning, TEXT("CAMPAIGN_SNAPSHOT_REJECTED %s"), *Error);
    return false;
}

/** 指纹覆盖实际UTF8字节；SHA1仅检测内容一致性，账号/云请求认证由既有服务完成。 */
FString FingerprintOf(const FString& Json)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    const FTCHARToUTF8 Utf8(*Json); // 转码缓冲只在同步哈希期间使用，不传递临时指针。
    return TEXT("sha1:") + FSHA1::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length())).ToString().ToLower();
}

/** 精确字段集合；规范化再比对原文还会发现JSON库覆盖掉的重复键。 */
bool ExactFields(const TSharedPtr<FJsonObject>& Object, std::initializer_list<const TCHAR*> Keys)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    if (!Object || Object->Values.Num() != static_cast<int32>(Keys.size())) return false;
    for (const TCHAR* Key : Keys) if (!Object->HasField(Key)) return false;
    return true;
}

/** 仅读取JSON number，有限范围校验在转换前完成，不允许字符串伪装数字。 */
bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Minimum, double Maximum, double& Out)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    return Object && Object->HasTypedField<EJson::Number>(Key) && Object->TryGetNumberField(Key, Out)
        && FMath::IsFinite(Out) && Out >= Minimum && Out <= Maximum;
}

/** 整数字段拒绝小数、NaN及超过int32范围的值。 */
bool Integer(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32 Minimum, int32 Maximum, int32& Out)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    double Value = 0.; // 解析宽类型，在确认范围和整数性后才转换。
    if (!Number(Object, Key, Minimum, Maximum, Value) || FMath::FloorToDouble(Value) != Value) return false;
    Out = static_cast<int32>(Value);
    return true;
}

/** 受限浮点字段转回原配置float；再次规范化确保跨存档不会改变实际位值。 */
bool Scalar(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double Minimum, double Maximum, float& Out)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    double Value = 0.; // JSON宽数值，转换前已验证范围。
    if (!Number(Object, Key, Minimum, Maximum, Value)) return false;
    Out = static_cast<float>(Value);
    return FMath::IsFinite(Out);
}

/** ASCII稳定ID允许字母/数字/_/-，1..64位；None为UE空身份而被拒绝。 */
bool Token(const TSharedPtr<FJsonValue>& Value, FName& Out)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    FString Text; // 不接受路径和任意Unicode标识，服务器与C#白名单一致。
    if (!Value || Value->Type != EJson::String || !Value->TryGetString(Text) || Text.IsEmpty() || Text.Len() > 64) return false;
    for (TCHAR Character : Text)
        if (!((Character >= 'A' && Character <= 'Z') || (Character >= 'a' && Character <= 'z')
            || (Character >= '0' && Character <= '9') || Character == '_' || Character == '-')) return false;
    Out = FName(*Text);
    return !Out.IsNone();
}

/** 将JSON值安全视为对象；返回的共享引用只在本次同步解析中使用。 */
TSharedPtr<FJsonObject> ObjectOf(const TSharedPtr<FJsonValue>& Value)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    return Value && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
}

/** 定长向量列表只收cm纯值，各轴有限且在±10000cm内。 */
bool Anchors(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, int32 Count, TArray<FVector>& Out)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr; // Json树拥有，只借用到解析函数返回。
    if (!Object || !Object->TryGetArrayField(Key, Rows) || Rows->Num() != Count) return false;
    Out.Reset();
    for (const TSharedPtr<FJsonValue>& Row : *Rows)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr; // 每行固定XYZ，不接受额外数据。
        if (!Row || !Row->TryGetArray(Values) || Values->Num() != 3) return false;
        FVector Position = FVector::ZeroVector; // 所有轴通过校验后提交。
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            double Value = 0.; // FVector在UE5使用double；不经float截断。
            if (!(*Values)[Axis] || (*Values)[Axis]->Type != EJson::Number || !(*Values)[Axis]->TryGetNumber(Value)
                || !FMath::IsFinite(Value) || FMath::Abs(Value) > 10000.) return false;
            Position[Axis] = Value;
        }
        Out.Add(Position);
    }
    return true;
}

/** 写出定长锚点；固定数组顺序保留控制点的实际激活顺序。 */
void WriteAnchors(const TSharedRef<FWriter>& Writer, const TCHAR* Key, const TArray<FVector>& Values)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    Writer->WriteArrayStart(Key);
    for (const FVector& Position : Values)
    {
        Writer->WriteArrayStart();
        Writer->WriteValue(Position.X); Writer->WriteValue(Position.Y); Writer->WriteValue(Position.Z);
        Writer->WriteArrayEnd();
    }
    Writer->WriteArrayEnd();
}

/** 不可变V8发布数据；必须保留每一项显式赋值，未来作者默认值变更不得修改此迁移函数。 */
void ApplyPublishedV1(UDemoCampaignDefinition& Definition)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    Definition.CampaignId = TEXT("steam_demo");
    Definition.ContentRevision = 1;
    Definition.RoomAreas = {{TEXT("R01"),0},{TEXT("R02"),1},{TEXT("R03"),2},{TEXT("R04"),0},{TEXT("R05"),1},{TEXT("R06"),2},{TEXT("B01"),0},{TEXT("B02"),2}};
    Definition.RoomBossRows = {{TEXT("B01"),TEXT("Warden")},{TEXT("B02"),TEXT("Weaver")}};
    Definition.Stages.Reset(); Definition.Stages.SetNum(10);
    for (FDemoCampaignStage& Stage : Definition.Stages) Stage.Objective = EDemoObjectiveKind::KillAll;
    Definition.Stages[0].RoomPool={TEXT("R01")};
    Definition.Stages[1].RoomPool={TEXT("R02")}; Definition.Stages[1].Objective=EDemoObjectiveKind::Sabotage;
    Definition.Stages[2].RoomPool={TEXT("R03"),TEXT("R05")};
    Definition.Stages[3].RoomPool={TEXT("R01"),TEXT("R05")}; Definition.Stages[3].Objective=EDemoObjectiveKind::Sabotage;
    Definition.Stages[4].RoomPool={TEXT("R04")}; Definition.Stages[4].Objective=EDemoObjectiveKind::RotatingControl;
    Definition.Stages[5].RoomPool={TEXT("R02"),TEXT("R05")}; Definition.Stages[5].Objective=EDemoObjectiveKind::Sabotage;
    Definition.Stages[6].RoomPool={TEXT("R03"),TEXT("R06")};
    Definition.Stages[7].RoomPool={TEXT("R04"),TEXT("R06")}; Definition.Stages[7].Objective=EDemoObjectiveKind::RotatingControl;
    Definition.Stages[8].RoomPool={TEXT("R01"),TEXT("R03"),TEXT("R05")};
    Definition.Stages[9].RoomPool={TEXT("B01"),TEXT("B02")};
    Definition.Party.Reset(); Definition.Party.SetNum(4);
    for(int32 Index=0;Index<4;++Index)
    {
        FDemoPartyScaling& Scale=Definition.Party[Index]; // 复现已发布float算式，不受结构体未来初始化值影响。
        Scale.MinionHealth=1.f+.35f*Index; Scale.MinionCount=1.f+.6f*Index;
        Scale.BossHealth=1.f+.7f*Index; Scale.DeviceHealth=1.f+.5f*Index; Scale.MaxAlive=8+2*Index;
    }
    Definition.ObjectiveCountMultiplier=1.2f; Definition.DeviceBaseHealth=600.f;
    Definition.LocalDeviceAnchors={FVector(300,-650,0),FVector(300,650,0)};
    Definition.LocalControlAnchors={FVector(-400,-600,0),FVector(500,0,0),FVector(-400,600,0)};
    Definition.ControlSeconds=20.f; Definition.TransitionSeconds=5.f; Definition.ControlRadius=450.f;
}
}

bool UDemoCampaignDefinition::ExportSnapshot(FString& OutJson, FString& OutFingerprint, FString& Error) const
{
    DEMO_LOG_CALL();
    using namespace DemoCampaignSnapshot;
    OutJson.Reset(); OutFingerprint.Reset(); Error.Reset();
    if (!Validate(Error)) return false;
    FString Json; // 固定字段顺序加有序映射，不依赖TMap迭代或系统区域设置。
    const TSharedRef<FWriter> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("schemaVersion"), SchemaVersion);
    Writer->WriteValue(TEXT("campaignId"), CampaignId.ToString().ToLower());
    Writer->WriteValue(TEXT("contentRevision"), ContentRevision);
    Writer->WriteArrayStart(TEXT("stages"));
    for (const FDemoCampaignStage& Stage : Stages)
    {
        Writer->WriteObjectStart(); Writer->WriteValue(TEXT("objective"), static_cast<int32>(Stage.Objective));
        Writer->WriteArrayStart(TEXT("rooms"));
        for (FName Room : Stage.RoomPool) Writer->WriteValue(Room.ToString().ToLower()); // FName大小写不敏感，规范化避免不同构建的显示大小写影响指纹。
        Writer->WriteArrayEnd(); Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    TArray<FString> Keys; // ASCII标识统一小写后排序，避免FName内部索引/首次登记大小写影响跨进程指纹。
    for (const TPair<FName,int32>& Pair : RoomAreas) Keys.Add(Pair.Key.ToString().ToLower());
    Keys.Sort(LessId);
    Writer->WriteArrayStart(TEXT("roomAreas"));
    for (const FString& Key : Keys)
    {
        Writer->WriteObjectStart(); Writer->WriteValue(TEXT("id"), Key);
        Writer->WriteValue(TEXT("area"), RoomAreas.FindChecked(FName(*Key))); Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd(); Keys.Reset();
    for (const TPair<FName,FName>& Pair : RoomBossRows) Keys.Add(Pair.Key.ToString().ToLower());
    Keys.Sort(LessId);
    Writer->WriteArrayStart(TEXT("roomBossRows"));
    for (const FString& Key : Keys)
    {
        Writer->WriteObjectStart(); Writer->WriteValue(TEXT("id"), Key);
        Writer->WriteValue(TEXT("row"), RoomBossRows.FindChecked(FName(*Key)).ToString().ToLower()); Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteArrayStart(TEXT("party"));
    for (const FDemoPartyScaling& Scale : Party)
    {
        Writer->WriteObjectStart();
        // UE5.4 float写入默认仅%g六位；提升double走17有效位，冻结时保留原float精度而不是悄悄改变作者调参。
        Writer->WriteValue(TEXT("minionHealth"), static_cast<double>(Scale.MinionHealth)); Writer->WriteValue(TEXT("minionCount"), static_cast<double>(Scale.MinionCount));
        Writer->WriteValue(TEXT("bossHealth"), static_cast<double>(Scale.BossHealth)); Writer->WriteValue(TEXT("deviceHealth"), static_cast<double>(Scale.DeviceHealth));
        Writer->WriteValue(TEXT("maxAlive"), Scale.MaxAlive); Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd();
    Writer->WriteValue(TEXT("objectiveCountMultiplier"), static_cast<double>(ObjectiveCountMultiplier)); Writer->WriteValue(TEXT("deviceBaseHealth"), static_cast<double>(DeviceBaseHealth));
    WriteAnchors(Writer, TEXT("deviceAnchors"), LocalDeviceAnchors); WriteAnchors(Writer, TEXT("controlAnchors"), LocalControlAnchors);
    Writer->WriteValue(TEXT("controlSeconds"), static_cast<double>(ControlSeconds)); Writer->WriteValue(TEXT("transitionSeconds"), static_cast<double>(TransitionSeconds));
    Writer->WriteValue(TEXT("controlRadius"), static_cast<double>(ControlRadius)); Writer->WriteObjectEnd();
    if (!Writer->Close()) return Reject(Error, TEXT("战役快照JSON写入失败"));
    const FTCHARToUTF8 Utf8(*Json); // 哈希和外层协议统一按UTF8字节限制，不使用字符数近似。
    if (Utf8.Length() > MaxBytes) return Reject(Error, TEXT("战役快照超出64KiB限制"));
    OutJson = MoveTemp(Json); OutFingerprint = FingerprintOf(OutJson);
    return true;
}

UDemoCampaignDefinition* UDemoCampaignDefinition::FromSnapshot(UObject* Outer, const FString& Json, const FString& Fingerprint, FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    using namespace DemoCampaignSnapshot;
    Error.Reset();
    if (!IsValid(Outer) || Json.IsEmpty() || Json.Len() > MaxBytes)
    { Reject(Error, TEXT("战役快照所有者或大小无效")); return nullptr; }
    const FTCHARToUTF8 Utf8(*Json); // 字符上限先拦截，然后精确检查字节大小。
    if (Utf8.Length() > MaxBytes || !HasBoundedNesting(Json) || Fingerprint.Len() != 45 || Fingerprint != FingerprintOf(Json))
    { Reject(Error, TEXT("战役快照大小超限或指纹不符")); return nullptr; }
    TSharedPtr<FJsonObject> Root; // 本函数同步拥有JSON树，未校验数据不得流入World。
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !ExactFields(Root, {TEXT("schemaVersion"),TEXT("campaignId"),TEXT("contentRevision"),TEXT("stages"),TEXT("roomAreas"),TEXT("roomBossRows"),TEXT("party"),TEXT("objectiveCountMultiplier"),TEXT("deviceBaseHealth"),TEXT("deviceAnchors"),TEXT("controlAnchors"),TEXT("controlSeconds"),TEXT("transitionSeconds"),TEXT("controlRadius")}))
    { Reject(Error, TEXT("战役快照JSON或字段集合无效")); return nullptr; }
    int32 Schema = 0; // 与作者revision独立，未来格式不会按旧规则误读。
    if (!Integer(Root,TEXT("schemaVersion"),1,MAX_int32,Schema) || Schema != SchemaVersion)
    { Reject(Error, TEXT("战役快照格式版本不受支持")); return nullptr; }
    UDemoCampaignDefinition* Candidate = NewObject<UDemoCampaignDefinition>(Outer); // 尚未提交；失败后由GC清理，成功调用方须UPROPERTY持有。
    if (!Token(Root->TryGetField(TEXT("campaignId")),Candidate->CampaignId) || !Integer(Root,TEXT("contentRevision"),1,MAX_int32,Candidate->ContentRevision))
    { Reject(Error, TEXT("战役快照身份或修订无效")); return nullptr; }
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr; // 逐段借用JSON数组，每段立即消费不跨异步存储。
    if (!Root->TryGetArrayField(TEXT("stages"),Rows) || Rows->Num()!=10)
    { Reject(Error, TEXT("战役快照必须包含十关")); return nullptr; }
    Candidate->Stages.Reset();
    for (const TSharedPtr<FJsonValue>& Value : *Rows)
    {
        const TSharedPtr<FJsonObject> Row = ObjectOf(Value); // 每关只允许目标与稳定房间池。
        int32 Kind=0; // 整数转枚举之前限制为当前三个目标。
        const TArray<TSharedPtr<FJsonValue>>* Rooms=nullptr; // 顺序决定种子抽样结果，不能重排。
        if (!ExactFields(Row,{TEXT("objective"),TEXT("rooms")}) || !Integer(Row,TEXT("objective"),0,2,Kind)
            || !Row->TryGetArrayField(TEXT("rooms"),Rooms) || Rooms->IsEmpty() || Rooms->Num()>8)
        { Reject(Error, TEXT("战役快照关卡字段或房间数量无效")); return nullptr; }
        FDemoCampaignStage Stage; Stage.Objective=static_cast<EDemoObjectiveKind>(Kind); // 候选行，完整校验再加入。
        TSet<FName> Seen; // FName大小写不敏感，重复大小写也必须拒绝。
        for (const TSharedPtr<FJsonValue>& RoomValue : *Rooms)
        {
            FName Room; // 稳定ID，不是包路径。
            if (!Token(RoomValue,Room) || Seen.Contains(Room))
            { Reject(Error, TEXT("战役快照房间ID无效或重复")); return nullptr; }
            Seen.Add(Room); Stage.RoomPool.Add(Room);
        }
        Candidate->Stages.Add(MoveTemp(Stage));
    }
    if (!Root->TryGetArrayField(TEXT("roomAreas"),Rows) || Rows->IsEmpty() || Rows->Num()>64)
    { Reject(Error, TEXT("战役快照区域映射数量无效")); return nullptr; }
    Candidate->RoomAreas.Reset();
    for (const TSharedPtr<FJsonValue>& Value : *Rows)
    {
        const TSharedPtr<FJsonObject> Row=ObjectOf(Value); // 映射数组便于显式发现重复ID。
        FName Id; int32 Area=0; // 只允许当前白模适配的0..2区域。
        if (!ExactFields(Row,{TEXT("id"),TEXT("area")}) || !Token(Row->TryGetField(TEXT("id")),Id)
            || !Integer(Row,TEXT("area"),0,2,Area) || Candidate->RoomAreas.Contains(Id))
        { Reject(Error, TEXT("战役快照区域映射无效或重复")); return nullptr; }
        Candidate->RoomAreas.Add(Id,Area);
    }
    if (!Root->TryGetArrayField(TEXT("roomBossRows"),Rows) || Rows->IsEmpty() || Rows->Num()>64)
    { Reject(Error, TEXT("战役快照Boss映射数量无效")); return nullptr; }
    Candidate->RoomBossRows.Reset();
    for (const TSharedPtr<FJsonValue>& Value : *Rows)
    {
        const TSharedPtr<FJsonObject> Row=ObjectOf(Value); // 仅稳定数据表行名，不反射加载类型。
        FName Id, Boss; // 所有Boss房间还必须通过区域存在性验证。
        if (!ExactFields(Row,{TEXT("id"),TEXT("row")}) || !Token(Row->TryGetField(TEXT("id")),Id)
            || !Token(Row->TryGetField(TEXT("row")),Boss) || Candidate->RoomBossRows.Contains(Id))
        { Reject(Error, TEXT("战役快照Boss映射无效或重复")); return nullptr; }
        Candidate->RoomBossRows.Add(Id,Boss);
    }
    if (!Root->TryGetArrayField(TEXT("party"),Rows) || Rows->Num()!=4)
    { Reject(Error, TEXT("战役快照必须包含四档人数倍率")); return nullptr; }
    Candidate->Party.Reset();
    for (const TSharedPtr<FJsonValue>& Value : *Rows)
    {
        const TSharedPtr<FJsonObject> Row=ObjectOf(Value); // 1..4人对应顺序，不保存动态在线人数。
        FDemoPartyScaling Scale; // 候选全部五字段均显式解析，拒绝缺失默认回填。
        if (!ExactFields(Row,{TEXT("minionHealth"),TEXT("minionCount"),TEXT("bossHealth"),TEXT("deviceHealth"),TEXT("maxAlive")})
            || !Scalar(Row,TEXT("minionHealth"),1.,10.,Scale.MinionHealth) || !Scalar(Row,TEXT("minionCount"),1.,10.,Scale.MinionCount)
            || !Scalar(Row,TEXT("bossHealth"),1.,10.,Scale.BossHealth) || !Scalar(Row,TEXT("deviceHealth"),1.,10.,Scale.DeviceHealth)
            || !Integer(Row,TEXT("maxAlive"),4,16,Scale.MaxAlive))
        { Reject(Error, TEXT("战役快照人数倍率或并发上限无效")); return nullptr; }
        Candidate->Party.Add(Scale);
    }
    if (!Scalar(Root,TEXT("objectiveCountMultiplier"),1.,3.,Candidate->ObjectiveCountMultiplier)
        || !Scalar(Root,TEXT("deviceBaseHealth"),1.,100000.,Candidate->DeviceBaseHealth)
        || !Anchors(Root,TEXT("deviceAnchors"),2,Candidate->LocalDeviceAnchors) || !Anchors(Root,TEXT("controlAnchors"),3,Candidate->LocalControlAnchors)
        || !Scalar(Root,TEXT("controlSeconds"),1.,600.,Candidate->ControlSeconds)
        || !Scalar(Root,TEXT("transitionSeconds"),1.,30.,Candidate->TransitionSeconds)
        || !Scalar(Root,TEXT("controlRadius"),100.,2000.,Candidate->ControlRadius) || !Candidate->Validate(Error))
    { Reject(Error, TEXT("战役快照目标参数、锚点或关联关系无效")); return nullptr; }
    FString Canonical, CanonicalFingerprint; // 重建结果必须逐字一致，捕获重复JSON键、乱序映射、未知或非规范数字。
    if (!Candidate->ExportSnapshot(Canonical,CanonicalFingerprint,Error) || Canonical!=Json || CanonicalFingerprint!=Fingerprint)
    { Reject(Error, TEXT("战役快照不是规范schema1编码，可能含重复字段或不稳定数值")); return nullptr; }
    return Candidate;
}

bool UDemoCampaignDefinition::ExportLegacyV1(FString& OutJson, FString& OutFingerprint, FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    UDemoCampaignDefinition* Published = NewObject<UDemoCampaignDefinition>(GetTransientPackage()); // 同步迁移临时对象，不引用作者资产或World。
    DemoCampaignSnapshot::ApplyPublishedV1(*Published);
    return Published->ExportSnapshot(OutJson,OutFingerprint,Error);
}

bool UDemoCampaignDefinition::HasUnsupportedSnapshotSchema(const FString& Json)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    if (Json.IsEmpty() || Json.Len()>DemoCampaignSnapshot::MaxBytes) return false;
    const FTCHARToUTF8 Utf8(*Json); // 刷新存档时的首道预检也必须按字节限长，不能等FromSnapshot才限制深度。
    if(Utf8.Length()>DemoCampaignSnapshot::MaxBytes || !DemoCampaignSnapshot::HasBoundedNesting(Json)) return false;
    TSharedPtr<FJsonObject> Root; // 仅格式保护预检；不能替代FromSnapshot完整校验。
    const TSharedRef<TJsonReader<>> Reader=TJsonReaderFactory<>::Create(Json);
    int32 Schema=0; // 可识别的未来正整数才保护，不把损坏内容错误宣称为新版本。
    return FJsonSerializer::Deserialize(Reader,Root) && DemoCampaignSnapshot::Integer(Root,TEXT("schemaVersion"),1,MAX_int32,Schema)
        && Schema>DemoCampaignSnapshot::SchemaVersion;
}
