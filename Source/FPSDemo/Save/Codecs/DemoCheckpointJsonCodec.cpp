// 职责：检查点JSON序列化与严格解码。使用：读写本人稳定ID和等级，目录白名单校验，不从存档加载类型路径。
#include "Save/Codecs/DemoCheckpointJsonCodec.h"
#include "Save/Codecs/DemoArcadeJson.h"
#include "Save/Codecs/DemoOwnershipJson.h" // V10根/成员共用显式来源白名单。
#include "Save/DemoRunSave.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Debug/DemoLog.h"
#include <initializer_list>

namespace
{
    /** Error/Message只在本次同步解析中使用；失败消息不包含账号凭据或完整远端正文。 */
    bool Reject(FString& Error,const FString& Message)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__); Error=Message;
        UE_LOG(LogFPSDemo,Warning,TEXT("CHECKPOINT_CODEC_REJECT %s"),*Error); return false;
    }
    /** Object借用当前JSON对象，Fields为明确协议键；未知字段失败，防本地新UPROPERTY静默进入云协议。 */
    bool AllowOnly(const FJsonObject& Object,std::initializer_list<const TCHAR*> Fields,FString& Error)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        for (const auto& Pair:Object.Values) // 只检查键，不把不可信正文打印到日志。
        {
            bool bKnown=false; // 单个JSON字段匹配结果，协议字段数固定且很小。
            for (const TCHAR* Field:Fields) if (Pair.Key==Field) { bKnown=true; break; } // 白名单大小与运行数据量无关。
            if (!bKnown) return Reject(Error,TEXT("云检查点包含当前协议不支持的字段"));
        }
        return true;
    }
    /** Key为稳定JSON字段；可选字段只允许缺失，不接受null/错误类型；Out成功时写入。 */
    bool ReadInt(const FJsonObject& Object,const TCHAR* Key,int32& Out,bool bRequired,FString& Error)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs field=%s"),__FUNCTION__,Key);
        if (!Object.HasField(Key)) return !bRequired||Reject(Error,FString::Printf(TEXT("云检查点缺少字段 %s"),Key));
        double Number=0; // JSON数值先用double检查有限/整数/范围，再转换int32，禁止小数被截断。
        if (!Object.HasTypedField<EJson::Number>(Key)||!Object.TryGetNumberField(Key,Number)||!FMath::IsFinite(Number)||Number<MIN_int32||Number>MAX_int32||static_cast<double>(static_cast<int32>(Number))!=Number)
            return Reject(Error,FString::Printf(TEXT("云检查点整数字段非法 %s"),Key));
        Out=static_cast<int32>(Number); return true;
    }
    /** 浮点字段必须真实存在且有限；语义范围由RunSave.Validate统一检查。 */
    bool ReadFloat(const FJsonObject& Object,const TCHAR* Key,float& Out,FString& Error)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs field=%s"),__FUNCTION__,Key);
        double Number=0; // 使用double接收JSON，转换前排除float溢出。
        if (!Object.HasTypedField<EJson::Number>(Key)||!Object.TryGetNumberField(Key,Number)||!FMath::IsFinite(Number)||FMath::Abs(Number)>MAX_flt) return Reject(Error,FString::Printf(TEXT("云检查点数值非法 %s"),Key));
        Out=static_cast<float>(Number); return true;
    }
    /** Key/Out为稳定文本字段；Limit控制输入大小，不允许JSON number被当作字符串。 */
    bool ReadString(const FJsonObject& Object,const TCHAR* Key,FString& Out,int32 Limit,FString& Error)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs field=%s"),__FUNCTION__,Key);
        if (!Object.HasTypedField<EJson::String>(Key)||!Object.TryGetStringField(Key,Out)||Out.Len()>Limit) return Reject(Error,FString::Printf(TEXT("云检查点文本字段非法 %s"),Key));
        return true;
    }
    /** 三个属性购买次数必须完整提供；数组由候选对象拥有，不与远端JSON共享可变元素。 */
    bool ReadLevels(const FJsonObject& Object,const TCHAR* Key,TArray<int32>& Out,FString& Error)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs field=%s"),__FUNCTION__,Key);
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr; // 只在解析栈借用远端数组。
        if (!Object.TryGetArrayField(Key,Values)||Values->Num()!=3) return Reject(Error,TEXT("云成长账本必须包含三个属性等级"));
        Out.Reset();
        for (const TSharedPtr<FJsonValue>& Value:*Values) // 每项必须0..100000整数，与本地账本限制一致。
        {
            double Number=0; // 转换前先检查，损坏小数不能改变购买价格。
            if (!Value||Value->Type!=EJson::Number||!Value->TryGetNumber(Number)||!FMath::IsFinite(Number)||Number<0||Number>100000||static_cast<double>(static_cast<int32>(Number))!=Number) return Reject(Error,TEXT("云成长等级非法"));
            Out.Add(static_cast<int32>(Number));
        }
        return true;
    }
    /** Levels为已验证的整数账本（三属性或天赋等级）；返回独立JSON数组，输出顺序稳定。 */
    TArray<TSharedPtr<FJsonValue>> WriteLevels(const TArray<int32>& Levels)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        TArray<TSharedPtr<FJsonValue>> Values; // 本次导出独占的值数组。
        for (const int32 Level:Levels) Values.Add(MakeShared<FJsonValueNumber>(Level)); // 只复制值，无对象路径。
        return Values;
    }
    /** 已验证领域账本显式写为五项协议字段，不反射本地结构。 */
    TSharedRef<FJsonObject> WriteProgress(const FDemoUpgradeProgress& Progress)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>(); // 独立成长值对象。
        Json->SetArrayField(TEXT("goldLevels"),WriteLevels(Progress.GoldLevels)); Json->SetArrayField(TEXT("silverLevels"),WriteLevels(Progress.SilverLevels));
        Json->SetNumberField(TEXT("permanentDamage"),Progress.PermanentDamage); Json->SetNumberField(TEXT("permanentHealth"),Progress.PermanentHealth); Json->SetNumberField(TEXT("permanentMagazine"),Progress.PermanentMagazine); return Json;
    }
    /** Object为账本对象，Out为候选领域值；所有字段均为必需。 */
    bool ReadProgress(const FJsonObject& Object,FDemoUpgradeProgress& Out,FString& Error)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        return AllowOnly(Object,{TEXT("goldLevels"),TEXT("silverLevels"),TEXT("permanentDamage"),TEXT("permanentHealth"),TEXT("permanentMagazine")},Error)
            &&ReadLevels(Object,TEXT("goldLevels"),Out.GoldLevels,Error)&&ReadLevels(Object,TEXT("silverLevels"),Out.SilverLevels,Error)
            &&ReadFloat(Object,TEXT("permanentDamage"),Out.PermanentDamage,Error)&&ReadFloat(Object,TEXT("permanentHealth"),Out.PermanentHealth,Error)&&ReadFloat(Object,TEXT("permanentMagazine"),Out.PermanentMagazine,Error);
    }
    /** IDs为稳定目录键；归一化后写独立字符串数组，不保存FName索引。 */
    TArray<TSharedPtr<FJsonValue>> WriteIds(const TArray<FName>& Ids)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        TArray<TSharedPtr<FJsonValue>> Values; // 本次JSON对象独占数组。
        for (const FName Id:Ids) Values.Add(MakeShared<FJsonValueString>(Id.ToString().ToLower())); return Values;
    }
    /** Object/Key指定目录数组，MaxCount限制小型协议大小；重复/目录权限由最终领域校验拒绝。 */
    bool ReadIds(const FJsonObject& Object,const TCHAR* Key,TArray<FName>& Out,int32 MaxCount,FString& Error,bool bAllowEmpty=false)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs field=%s"),__FUNCTION__,Key);
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr; // 本解析栈只读JSON数组。
        if (!Object.TryGetArrayField(Key,Values)||(!bAllowEmpty&&Values->Num()<1)||Values->Num()>MaxCount) return Reject(Error,TEXT("云目录ID数组非法"));
        Out.Reset();
        for (const TSharedPtr<FJsonValue>& Value:*Values)
        {
            FString Id; // 小写稳定目录名，无路径和任意对象名语义。
            if (!Value||Value->Type!=EJson::String||!Value->TryGetString(Id)||Id.IsEmpty()||Id.Len()>32) return Reject(Error,TEXT("云目录ID非法"));
            Out.Add(FName(*Id));
        }
        return true;
    }
    /** Mine为权威完整值，仅云存档协议使用；菜单不得调用此编码器。 */
    TSharedRef<FJsonObject> WriteMine(const FDemoMineRound& Mine)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        auto Json=MakeShared<FJsonObject>(); // 独立私有存档对象，不发到客户端菜单。
        Json->SetNumberField(TEXT("stake"),Mine.Stake); Json->SetNumberField(TEXT("bombs"),Mine.Bombs);
        Json->SetNumberField(TEXT("debited"),Mine.Debited); // 实际扣款独立于报价，避免Editor免扣时反馈虚假亏损。
        Json->SetNumberField(TEXT("bombMask"),Mine.BombMask); Json->SetNumberField(TEXT("openMask"),Mine.OpenMask);
        Json->SetArrayField(TEXT("payouts"),WriteLevels(Mine.Payouts)); return Json;
    }
    /** Json仅接受完整V7货箱对象；Mine输出仍通过领域校验，不接受私有雷位被吞字段。 */
    bool ReadMine(const FJsonObject& Json,FDemoMineRound& Mine,FString& Error)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        if(!AllowOnly(Json,{TEXT("stake"),TEXT("debited"),TEXT("bombs"),TEXT("bombMask"),TEXT("openMask"),TEXT("payouts")},Error)
            ||!ReadInt(Json,TEXT("stake"),Mine.Stake,true,Error)||!ReadInt(Json,TEXT("bombs"),Mine.Bombs,true,Error)
            ||!ReadInt(Json,TEXT("debited"),Mine.Debited,true,Error)
            ||!ReadInt(Json,TEXT("bombMask"),Mine.BombMask,true,Error)||!ReadInt(Json,TEXT("openMask"),Mine.OpenMask,true,Error)) return false;
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr; // JSON值只借用当前解析栈。
        if(!Json.TryGetArrayField(TEXT("payouts"),Values)||Values->Num()>24) return Reject(Error,TEXT("货箱冻结金额缺失或过多"));
        Mine.Payouts.Reset();
        for(const auto& Value:*Values)
        {
            double Number=0; // 不允许小数、NaN或溢出进入整数账本。
            if(!Value||Value->Type!=EJson::Number||!Value->TryGetNumber(Number)||!FMath::IsFinite(Number)||Number<0||Number>MAX_int32||double(int32(Number))!=Number) return Reject(Error,TEXT("货箱金额非法"));
            Mine.Payouts.Add(int32(Number));
        }
        return Mine.Validate()||Reject(Error,TEXT("货箱棋盘/金额不一致"));
    }
    /** 最新已保存老虎机事实只编码四个专属字段；其他模式字段不进入此协议对象，空事实由父对象省略。 */
    TSharedRef<FJsonObject> WriteSlotsReceipt(const FDemoCasinoResult& Receipt)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>(); // 本次作者JSON独占，不携带未来结果或概率。
        Json->SetNumberField(TEXT("mode"),Receipt.Mode); Json->SetNumberField(TEXT("serial"),Receipt.Serial);
        Json->SetArrayField(TEXT("symbols"),WriteLevels(Receipt.Symbols)); Json->SetNumberField(TEXT("paid"),Receipt.Paid);
        return Json;
    }
    /** 可选对象一旦存在必须完整四字段；未知模式字段/null/非整数拒绝，缺失整个对象才兼容旧档空值。 */
    bool ReadSlotsReceipt(const FJsonObject& Json,FDemoCasinoResult& Out,FString& Error)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        Out=FDemoCasinoResult();
        if (!AllowOnly(Json,{TEXT("mode"),TEXT("serial"),TEXT("symbols"),TEXT("paid")},Error)
            || !ReadInt(Json,TEXT("mode"),Out.Mode,true,Error) || !ReadInt(Json,TEXT("serial"),Out.Serial,true,Error)
            || !ReadInt(Json,TEXT("paid"),Out.Paid,true,Error)) return false;
        const TArray<TSharedPtr<FJsonValue>>* Symbols=nullptr; // 明确有界：canonical-empty空数组或Mode1三项，由领域校验决定组合。
        if (!Json.TryGetArrayField(TEXT("symbols"),Symbols) || Symbols->Num()>3) return Reject(Error,TEXT("老虎机符号数组非法"));
        for (const TSharedPtr<FJsonValue>& Value:*Symbols)
        {
            double Symbol=0.; // 拒绝小数、字符串、null及超出五符号，不能被int截断。
            if (!Value || Value->Type!=EJson::Number || !Value->TryGetNumber(Symbol) || !FMath::IsFinite(Symbol) || Symbol<0 || Symbol>4 || Symbol!=static_cast<double>(static_cast<int32>(Symbol))) return Reject(Error,TEXT("老虎机符号非法"));
            Out.Symbols.Add(static_cast<int32>(Symbol));
        }
        return true; // 与账本Serial的关系由Out.Gambling.Validate统一核，不能只信JSON字段范围。
    }
    /** Data为已验证研究值；Version决定显式协议字段，旧档不新增键改变哈希。 */
    TSharedRef<FJsonObject> WriteGamble(const FDemoGambleProgress& Data,int32 Version)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>(); // 此次编码独占。
        if(Version>=10&&Data.LastSlotsReceipt.Mode==1) Json->SetObjectField(TEXT("lastSlotsReceipt"),WriteSlotsReceipt(Data.LastSlotsReceipt)); // 可选V10字段；旧档/空事实省略，保持旧请求哈希。
        if(Version>=7&&Data.Arcade.Mode>=0) Json->SetObjectField(TEXT("arcade"),DemoArcadeJson::Write(Data.Arcade)); // 可选扩展：无待结局时保留旧JSON哈希。
        if(Version>=7) Json->SetObjectField(TEXT("mine"),WriteMine(Data.Mine)); // V7必须包含完整空箱或待结箱。
        if(!Data.TalentRanks.IsEmpty()) Json->SetArrayField(TEXT("talentRanks"),WriteLevels(Data.TalentRanks)); // 缺省保留旧档字节语义，第一次升级才写入。
        Json->SetArrayField(TEXT("talents"),WriteIds(Data.Talents)); Json->SetArrayField(TEXT("relics"),WriteIds(Data.Relics));
        Json->SetStringField(TEXT("equippedRelic"),Data.EquippedRelic.IsNone()?TEXT(""):Data.EquippedRelic.ToString().ToLower());
        Json->SetArrayField(TEXT("failures"),WriteLevels(Data.Failures));
        Json->SetNumberField(TEXT("node"),Data.Node); Json->SetNumberField(TEXT("attempts"),Data.Attempts); Json->SetNumberField(TEXT("serial"),Data.Serial);
        Json->SetBoolField(TEXT("bRescueUsed"),Data.bRescueUsed); Json->SetStringField(TEXT("lastResult"),Data.LastResult);
        return Json;
    }
    /** Parent是槽/个人成长对象；V6必须提供完整gambling对象，旧档只允许缺省。 */
    bool ReadGamble(const FJsonObject& Parent,FDemoGambleProgress& Out,int32 Version,FString& Error)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        Out=FDemoGambleProgress();
        if(Version<6) return !Parent.HasField(TEXT("gambling"))||Reject(Error,TEXT("旧版本不能携带研究字段"));
        const TSharedPtr<FJsonObject>* Json=nullptr; // 解析期间借用，输出只存值。
        FString Equipped; // 空文本显式表示未装备，不使用字符串None。
        if(!Parent.TryGetObjectField(TEXT("gambling"),Json)
            ||!AllowOnly(**Json,{TEXT("lastSlotsReceipt"),TEXT("arcade"),TEXT("mine"),TEXT("talents"),TEXT("talentRanks"),TEXT("relics"),TEXT("equippedRelic"),TEXT("failures"),TEXT("node"),TEXT("attempts"),TEXT("serial"),TEXT("bRescueUsed"),TEXT("lastResult")},Error)
            ||!ReadIds(**Json,TEXT("talents"),Out.Talents,DemoGamble::TalentCount(),Error,true)||!ReadIds(**Json,TEXT("relics"),Out.Relics,4,Error,true)
            ||!ReadString(**Json,TEXT("equippedRelic"),Equipped,32,Error)||!ReadLevels(**Json,TEXT("failures"),Out.Failures,Error)
            ||!ReadInt(**Json,TEXT("node"),Out.Node,true,Error)||!ReadInt(**Json,TEXT("attempts"),Out.Attempts,true,Error)||!ReadInt(**Json,TEXT("serial"),Out.Serial,true,Error)
            ||!(*Json)->HasTypedField<EJson::Boolean>(TEXT("bRescueUsed"))||!(*Json)->TryGetBoolField(TEXT("bRescueUsed"),Out.bRescueUsed)
            ||!ReadString(**Json,TEXT("lastResult"),Out.LastResult,256,Error)) return Reject(Error,TEXT("云研究账本字段非法"));
        if((*Json)->HasField(TEXT("lastSlotsReceipt")))
        {
            const TSharedPtr<FJsonObject>* Receipt=nullptr; // 解析期间借用，typed输出由账本值持有。
            if(Version<10 || !(*Json)->TryGetObjectField(TEXT("lastSlotsReceipt"),Receipt) || !ReadSlotsReceipt(**Receipt,Out.LastSlotsReceipt,Error)) return Reject(Error,TEXT("最近老虎机耐久回执非法"));
        }
        const TSharedPtr<FJsonObject>* Mine=nullptr; // 仅保存/恢复通道可解析完整雷位。
        if(Version>=7) { if(!(*Json)->TryGetObjectField(TEXT("mine"),Mine)||!ReadMine(**Mine,Out.Mine,Error)) return Reject(Error,TEXT("V7货箱对象非法")); }
        else if((*Json)->HasField(TEXT("mine"))) return Reject(Error,TEXT("旧版本不可携带货箱"));
        if((*Json)->HasField(TEXT("arcade")))
        {
            const TSharedPtr<FJsonObject>* Arcade=nullptr; // 同栈候选，不能以错误结构冒充空局。
            if(Version<7||!(*Json)->TryGetObjectField(TEXT("arcade"),Arcade)||!DemoArcadeJson::Read(**Arcade,Out.Arcade)||Out.Arcade.Mode<0) return Reject(Error,TEXT("持续娱乐局面非法"));
        }
        if((*Json)->HasField(TEXT("talentRanks")))
        {
            const TArray<TSharedPtr<FJsonValue>>* Ranks=nullptr; // 借用解析栈数组；不允许null、部分数组或空数组冒充完整等级。
            if(!(*Json)->TryGetArrayField(TEXT("talentRanks"),Ranks)||Ranks->Num()!=Out.Talents.Num()) return Reject(Error,TEXT("天赋等级数量不匹配"));
            for(const TSharedPtr<FJsonValue>& Value:*Ranks) // 每项只接受正int32，禁止浮点截断或数值溢出。
            {
                double Number=0; // 转换前检查原始JSON类型、有限性和精度。
                if(!Value||Value->Type!=EJson::Number||!Value->TryGetNumber(Number)||!FMath::IsFinite(Number)||Number<1||Number>MAX_int32||double(int32(Number))!=Number) return Reject(Error,TEXT("天赋等级非法"));
                Out.TalentRanks.Add(int32(Number));
            }
        }
        Out.EquippedRelic=Equipped.IsEmpty()?NAME_None:FName(*Equipped);
        return Out.Validate()||Reject(Error,TEXT("云研究账本白名单/范围非法"));
    }
    /** Build为已验证V8个人值；只输出稳定选择，不输出GA句柄、触发计数或Actor引用。 */
    TSharedRef<FJsonObject> WriteBuild(const FDemoRunBuildSnapshot& Build)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
        TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>(); // 新建独立值对象，旧版本不调用以保持旧请求哈希。
        Json->SetStringField(TEXT("coreId"), Build.CoreId.IsNone() ? TEXT("") : Build.CoreId.ToString().ToUpper());
        Json->SetStringField(TEXT("branch3"), Build.Branch3.IsNone() ? TEXT("") : Build.Branch3.ToString().ToUpper());
        Json->SetStringField(TEXT("branch7"), Build.Branch7.IsNone() ? TEXT("") : Build.Branch7.ToString().ToUpper());
        Json->SetNumberField(TEXT("pendingBranchStage"), Build.PendingBranchStage);
        Json->SetNumberField(TEXT("revision"), Build.Revision); Json->SetBoolField(TEXT("bLocked"), Build.bLocked);
        return Json;
    }
    /** Parent为根/成员对象，Version控制字段是否必需；Out只写候选，非法目录/互斥分支拒绝。 */
    bool ReadBuild(const FJsonObject& Parent, FDemoRunBuildSnapshot& Out, int32 Version, FString& Error)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
        Out = FDemoRunBuildSnapshot(); // 防候选复用残留上次核心。
        if (Version < 8) return !Parent.HasField(TEXT("build")) || Reject(Error, TEXT("旧检查点不能携带核心字段"));
        const TSharedPtr<FJsonObject>* Json = nullptr; // 仅同步借用，未完成整个存档校验前不提交玩家状态。
        FString Core, First, Second; // JSON固定大写ID；空字符串映射NAME_None，不接受路径。
        if (!Parent.TryGetObjectField(TEXT("build"), Json)
            || !AllowOnly(**Json, {TEXT("coreId"), TEXT("branch3"), TEXT("branch7"), TEXT("pendingBranchStage"), TEXT("revision"), TEXT("bLocked")}, Error)
            || !ReadString(**Json, TEXT("coreId"), Core, 16, Error) || !ReadString(**Json, TEXT("branch3"), First, 16, Error)
            || !ReadString(**Json, TEXT("branch7"), Second, 16, Error)
            || !ReadInt(**Json, TEXT("pendingBranchStage"), Out.PendingBranchStage, true, Error)
            || !ReadInt(**Json, TEXT("revision"), Out.Revision, true, Error)
            || !(*Json)->HasTypedField<EJson::Boolean>(TEXT("bLocked")) || !(*Json)->TryGetBoolField(TEXT("bLocked"), Out.bLocked))
            return Reject(Error, TEXT("云核心字段缺少或类型非法"));
        if (Core != Core.ToUpper() || First != First.ToUpper() || Second != Second.ToUpper()
            || Core == TEXT("NONE") || First == TEXT("NONE") || Second == TEXT("NONE")) return Reject(Error, TEXT("云核心ID不是规范格式")); // FName的None哨兵只能由协议空文本表示。
        Out.CoreId = Core.IsEmpty() ? NAME_None : FName(*Core); Out.Branch3 = First.IsEmpty() ? NAME_None : FName(*First); Out.Branch7 = Second.IsEmpty() ? NAME_None : FName(*Second);
        return Out.Validate() || Reject(Error, TEXT("云核心或互斥分支非法"));
    }
    /** Item为合作成员领域值；省略对象身份/认证信息，仅保留房主已有检查点字段。 */
    TSharedRef<FJsonObject> WriteMember(const FDemoCoopPlayerCheckpoint& Item,int32 Version)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
        TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>(); // 独立成员对象，包含个人经济和装备。
        if (Version >= 8) Json->SetObjectField(TEXT("build"), WriteBuild(Item.Build)); // 每人独立核心，不能从房主复制。
        TSharedRef<FJsonObject> Progress=MakeShared<FJsonObject>(); // 个人成长不合并到账户永久解锁投影。
        if(Version>=6) Progress->SetObjectField(TEXT("gambling"),WriteGamble(Item.Progress.Gambling,Version)); // 旧V5请求序列化保持原样。
        Progress->SetNumberField(TEXT("coins"),Item.Progress.Coins); Progress->SetNumberField(TEXT("silverCoins"),Item.Progress.SilverCoins); Progress->SetNumberField(TEXT("kills"),Item.Progress.Kills);
        Progress->SetObjectField(TEXT("upgradeProgress"),WriteProgress(Item.Progress.UpgradeProgress)); Progress->SetNumberField(TEXT("pistolChallenge"),Item.Progress.PistolChallenge); Progress->SetNumberField(TEXT("bestEndlessLevel"),Item.Progress.BestEndlessLevel);
        if (Version>=10) Progress->SetObjectField(TEXT("weaponOwnership"),DemoOwnershipJson::Encode(Item.Progress.Ownership)); // 完整永久权与同人钱包原子保存。
        Progress->SetArrayField(TEXT("unlockedWeaponIds"),WriteIds(Item.Progress.UnlockedWeaponIds)); Progress->SetBoolField(TEXT("bHellUnlocked"),Item.Progress.bHellUnlocked); Progress->SetBoolField(TEXT("bEndlessUnlocked"),Item.Progress.bEndlessUnlocked);
        Progress->SetNumberField(TEXT("lastRewardLevel"),Item.Progress.LastRewardLevel); Progress->SetNumberField(TEXT("revision"),Item.Progress.Revision);
        TSharedRef<FJsonObject> Loadout=MakeShared<FJsonObject>(); // 只包含领域装备值，没有Actor/Class路径。
        TArray<TSharedPtr<FJsonValue>> Weapons; // 本成员实际持有武器快照。
        for (const FDemoWeaponSnapshot& Weapon:Item.Loadout.Weapons)
        {
            TSharedRef<FJsonObject> Value=MakeShared<FJsonObject>(); // 单把装备完整三项值。
            Value->SetStringField(TEXT("id"),Weapon.Id.ToString().ToLower()); Value->SetNumberField(TEXT("ammo"),Weapon.Ammo); Value->SetNumberField(TEXT("reserve"),Weapon.Reserve); Weapons.Add(MakeShared<FJsonValueObject>(Value));
        }
        Loadout->SetArrayField(TEXT("weapons"),Weapons); Loadout->SetStringField(TEXT("primaryId"),Item.Loadout.PrimaryId.IsNone()?TEXT(""):Item.Loadout.PrimaryId.ToString().ToLower()); Loadout->SetNumberField(TEXT("activeSlot"),Item.Loadout.ActiveSlot);
        Loadout->SetArrayField(TEXT("unlockedAmmoIds"),WriteIds(Item.Loadout.UnlockedAmmoIds)); Loadout->SetStringField(TEXT("selectedAmmoId"),Item.Loadout.SelectedAmmoId.ToString().ToLower()); Loadout->SetNumberField(TEXT("magazineBonus"),Item.Loadout.MagazineBonus);
        Json->SetStringField(TEXT("playerKey"),Item.PlayerKey); Json->SetObjectField(TEXT("progress"),Progress); Json->SetObjectField(TEXT("loadout"),Loadout);
        Json->SetNumberField(TEXT("maxHealth"),Item.MaxHealth); Json->SetNumberField(TEXT("health"),Item.Health); Json->SetNumberField(TEXT("damageBonus"),Item.DamageBonus); Json->SetNumberField(TEXT("magazineBonus"),Item.MagazineBonus);
        Json->SetNumberField(TEXT("healAmount"),Item.HealAmount); Json->SetNumberField(TEXT("dashSpeed"),Item.DashSpeed); return Json;
    }
    /** Json为团队成员白名单对象；Out完整解码后仍交RunSave/Roster校验经济、权限和装备一致性。 */
    bool ReadMember(const FJsonObject& Json,FDemoCoopPlayerCheckpoint& Out,FString& Error,int32 Version)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
        if (!AllowOnly(Json,{TEXT("playerKey"),TEXT("progress"),TEXT("loadout"),TEXT("maxHealth"),TEXT("health"),TEXT("damageBonus"),TEXT("magazineBonus"),TEXT("healAmount"),TEXT("dashSpeed"),TEXT("build")},Error)
            ||!ReadString(Json,TEXT("playerKey"),Out.PlayerKey,128,Error)||Out.PlayerKey.IsEmpty()) return Reject(Error,TEXT("云合作成员身份非法"));
        FGuid PlayerId; // 可信LAN稳定关联GUID，不等同平台认证凭据。
        if (!FGuid::Parse(Out.PlayerKey,PlayerId)||!PlayerId.IsValid()) return Reject(Error,TEXT("云合作成员GUID非法"));
        Out.PlayerKey=PlayerId.ToString(EGuidFormats::DigitsWithHyphens); // 归一化后去重，不能用同一GUID不同文本绕过。
        if (!ReadBuild(Json, Out.Build, Version, Error)) return false;
        const TSharedPtr<FJsonObject>* Progress=nullptr; const TSharedPtr<FJsonObject>* Loadout=nullptr; const TSharedPtr<FJsonObject>* Upgrades=nullptr; // 三个对象只在本次同步解码借用。
        if (!Json.TryGetObjectField(TEXT("progress"),Progress)||!Json.TryGetObjectField(TEXT("loadout"),Loadout)||!(*Progress)->TryGetObjectField(TEXT("upgradeProgress"),Upgrades)) return Reject(Error,TEXT("云合作成员缺少成长或装备"));
        if (!AllowOnly(**Progress,{TEXT("coins"),TEXT("silverCoins"),TEXT("kills"),TEXT("upgradeProgress"),TEXT("pistolChallenge"),TEXT("bestEndlessLevel"),TEXT("unlockedWeaponIds"),TEXT("bHellUnlocked"),TEXT("bEndlessUnlocked"),TEXT("lastRewardLevel"),TEXT("revision"),TEXT("gambling"),TEXT("weaponOwnership")},Error)
            ||!ReadInt(**Progress,TEXT("coins"),Out.Progress.Coins,true,Error)||!ReadInt(**Progress,TEXT("silverCoins"),Out.Progress.SilverCoins,true,Error)||!ReadInt(**Progress,TEXT("kills"),Out.Progress.Kills,true,Error)
            ||!ReadInt(**Progress,TEXT("pistolChallenge"),Out.Progress.PistolChallenge,true,Error)||!ReadInt(**Progress,TEXT("bestEndlessLevel"),Out.Progress.BestEndlessLevel,true,Error)
            ||!ReadInt(**Progress,TEXT("lastRewardLevel"),Out.Progress.LastRewardLevel,true,Error)||!ReadInt(**Progress,TEXT("revision"),Out.Progress.Revision,true,Error)
            ||!ReadProgress(**Upgrades,Out.Progress.UpgradeProgress,Error)||!ReadIds(**Progress,TEXT("unlockedWeaponIds"),Out.Progress.UnlockedWeaponIds,4,Error)) return false;
        if(!ReadGamble(**Progress,Out.Progress.Gambling,Version,Error)) return false;
        if (Version>=10) { const TSharedPtr<FJsonObject>* Ownership=nullptr; /* 当前成员唯一永久权。 */ if (!(*Progress)->TryGetObjectField(TEXT("weaponOwnership"),Ownership)||!DemoOwnershipJson::Decode(*Ownership,Out.Progress.Ownership)) return Reject(Error,TEXT("云成员拥有权非法")); }
        else if ((*Progress)->HasField(TEXT("weaponOwnership"))) return Reject(Error,TEXT("旧版本不能携带拥有权")); // 完整成员策略与钱包一并恢复。
        if (!(*Progress)->HasTypedField<EJson::Boolean>(TEXT("bHellUnlocked"))||!(*Progress)->TryGetBoolField(TEXT("bHellUnlocked"),Out.Progress.bHellUnlocked)
            ||!(*Progress)->HasTypedField<EJson::Boolean>(TEXT("bEndlessUnlocked"))||!(*Progress)->TryGetBoolField(TEXT("bEndlessUnlocked"),Out.Progress.bEndlessUnlocked)) return Reject(Error,TEXT("云合作解锁标记类型非法"));
        if (!AllowOnly(**Loadout,{TEXT("weapons"),TEXT("primaryId"),TEXT("activeSlot"),TEXT("unlockedAmmoIds"),TEXT("selectedAmmoId"),TEXT("magazineBonus")},Error)
            ||!ReadInt(**Loadout,TEXT("activeSlot"),Out.Loadout.ActiveSlot,true,Error)||!ReadFloat(**Loadout,TEXT("magazineBonus"),Out.Loadout.MagazineBonus,Error)||!ReadIds(**Loadout,TEXT("unlockedAmmoIds"),Out.Loadout.UnlockedAmmoIds,4,Error)) return false;
        FString Primary,Selected; // 武器空ID明确表示没有主枪，弹药必须具有稳定ID。
        if (!ReadString(**Loadout,TEXT("primaryId"),Primary,32,Error)||!ReadString(**Loadout,TEXT("selectedAmmoId"),Selected,32,Error)) return false;
        Out.Loadout.PrimaryId=Primary.IsEmpty()?NAME_None:FName(*Primary); Out.Loadout.SelectedAmmoId=FName(*Selected);
        const TArray<TSharedPtr<FJsonValue>>* Weapons=nullptr; // 数量上限与基础检查点相同。
        if (!(*Loadout)->TryGetArrayField(TEXT("weapons"),Weapons)||Weapons->Num()>4) return Reject(Error,TEXT("云合作武器快照非法")); Out.Loadout.Weapons.Reset();
        for (const TSharedPtr<FJsonValue>& Value:*Weapons)
        {
            const TSharedPtr<FJsonObject>* WeaponJson=nullptr; FDemoWeaponSnapshot Weapon; FString Id; // 单项成功后再提交到候选数组。
            if (!Value||!Value->TryGetObject(WeaponJson)||!AllowOnly(**WeaponJson,{TEXT("id"),TEXT("ammo"),TEXT("reserve")},Error)||!ReadString(**WeaponJson,TEXT("id"),Id,32,Error)
                ||!ReadInt(**WeaponJson,TEXT("ammo"),Weapon.Ammo,true,Error)||!ReadInt(**WeaponJson,TEXT("reserve"),Weapon.Reserve,true,Error)) return Reject(Error,TEXT("云合作武器字段非法"));
            Weapon.Id=FName(*Id); Out.Loadout.Weapons.Add(Weapon);
        }
        return ReadFloat(Json,TEXT("maxHealth"),Out.MaxHealth,Error)&&ReadFloat(Json,TEXT("health"),Out.Health,Error)&&ReadFloat(Json,TEXT("damageBonus"),Out.DamageBonus,Error)
            &&ReadFloat(Json,TEXT("magazineBonus"),Out.MagazineBonus,Error)&&ReadFloat(Json,TEXT("healAmount"),Out.HealAmount,Error)&&ReadFloat(Json,TEXT("dashSpeed"),Out.DashSpeed,Error);
    }

}

TSharedPtr<FJsonObject> DemoCheckpointJsonCodec::Encode(const UDemoRunSave& Snapshot,FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__); Error.Reset();
    if (!Snapshot.Validate()) { Reject(Error,TEXT("本地检查点校验失败，未导出")); return nullptr; }
    TSharedRef<FJsonObject> Json=MakeShared<FJsonObject>(); // 显式协议白名单，不随SaveGame新增字段变化。
    Json->SetNumberField(TEXT("version"),Snapshot.Version);
    if (Snapshot.Version>=10) { Json->SetStringField(TEXT("slotStorageKey"),Snapshot.SlotStorageKey); Json->SetStringField(TEXT("ownerPlayerKey"),Snapshot.OwnerPlayerKey); Json->SetObjectField(TEXT("weaponOwnership"),DemoOwnershipJson::Encode(Snapshot.Ownership)); }
    if (Snapshot.Version >= 8)
    {
        Json->SetStringField(TEXT("campaignId"), Snapshot.CampaignId.IsNone() ? TEXT("") : Snapshot.CampaignId.ToString().ToLower());
        Json->SetNumberField(TEXT("contentRevision"), Snapshot.ContentRevision); Json->SetNumberField(TEXT("runSeed"), Snapshot.RunSeed);
        Json->SetObjectField(TEXT("build"), WriteBuild(Snapshot.Build)); // 即使未选核心也显式编码空对象，避免服务端吞字段。
    }
    if (Snapshot.Version >= 9)
    {
        Json->SetStringField(TEXT("contentSnapshot"), Snapshot.ContentSnapshot); // 保持冻结JSON原文，不能在云往返时重排其字段改变指纹。
        Json->SetStringField(TEXT("contentFingerprint"), Snapshot.ContentFingerprint); // 旧V1..V8不输出任何新键，历史Outbox正文不变。
    }
    if(Snapshot.Version>=6) Json->SetObjectField(TEXT("gambling"),WriteGamble(Snapshot.Gambling,Snapshot.Version)); // V6新增独立账本。
    if (!Snapshot.OnlineOwnerKey.IsEmpty()) Json->SetStringField(TEXT("onlineOwnerKey"),Snapshot.OnlineOwnerKey); // 可选V5身份绑定扩展；旧档省略以保持原哈希。
    Json->SetStringField(TEXT("createdLocal"),Snapshot.CreatedLocal.ToIso8601()); Json->SetStringField(TEXT("savedUtc"),Snapshot.SavedUtc.ToIso8601());
    Json->SetStringField(TEXT("runId"),Snapshot.RunId);
    Json->SetStringField(TEXT("phase"),Snapshot.Phase==EDemoPhase::Hub?TEXT("Hub"):Snapshot.Phase==EDemoPhase::Reward?TEXT("Reward"):Snapshot.Phase==EDemoPhase::Intermission?TEXT("Intermission"):TEXT("Victory"));
    Json->SetStringField(TEXT("difficulty"),Snapshot.Difficulty==EDemoDifficulty::Easy?TEXT("easy"):Snapshot.Difficulty==EDemoDifficulty::Normal?TEXT("normal"):Snapshot.Difficulty==EDemoDifficulty::Hard?TEXT("hard"):TEXT("hell"));
    Json->SetNumberField(TEXT("completedLevel"),Snapshot.CompletedLevel); Json->SetBoolField(TEXT("bEndless"),Snapshot.bEndless);
    Json->SetNumberField(TEXT("bestEndlessLevel"),Snapshot.BestEndlessLevel); Json->SetNumberField(TEXT("pistolChallenge"),Snapshot.PistolChallenge);
    Json->SetNumberField(TEXT("coins"),Snapshot.Coins); Json->SetNumberField(TEXT("silverCoins"),Snapshot.SilverCoins); Json->SetNumberField(TEXT("kills"),Snapshot.Kills);
    Json->SetNumberField(TEXT("purchases"),Snapshot.Purchases); Json->SetNumberField(TEXT("goldPurchases"),Snapshot.GoldPurchases); Json->SetNumberField(TEXT("silverPurchases"),Snapshot.SilverPurchases);
    TSharedRef<FJsonObject> Progress=MakeShared<FJsonObject>(); // 永久账本只包含当前服务端支持的五项字段。
    Progress->SetArrayField(TEXT("goldLevels"),WriteLevels(Snapshot.UpgradeProgress.GoldLevels)); Progress->SetArrayField(TEXT("silverLevels"),WriteLevels(Snapshot.UpgradeProgress.SilverLevels));
    Progress->SetNumberField(TEXT("permanentDamage"),Snapshot.UpgradeProgress.PermanentDamage); Progress->SetNumberField(TEXT("permanentHealth"),Snapshot.UpgradeProgress.PermanentHealth); Progress->SetNumberField(TEXT("permanentMagazine"),Snapshot.UpgradeProgress.PermanentMagazine);
    Json->SetObjectField(TEXT("upgradeProgress"),Progress);
    TArray<TSharedPtr<FJsonValue>> Ammo; // 协议ID一律小写，避免非Editor FName大小写合并造成差异。
    for (const FName Id:Snapshot.UnlockedAmmoIds) Ammo.Add(MakeShared<FJsonValueString>(Id.ToString().ToLower()));
    Json->SetArrayField(TEXT("unlockedAmmoIds"),Ammo); Json->SetStringField(TEXT("selectedAmmoId"),Snapshot.SelectedAmmoId.ToString().ToLower());
    Json->SetNumberField(TEXT("maxHealth"),Snapshot.MaxHealth); Json->SetNumberField(TEXT("health"),Snapshot.Health); Json->SetNumberField(TEXT("damageBonus"),Snapshot.DamageBonus);
    Json->SetNumberField(TEXT("magazineBonus"),Snapshot.MagazineBonus); Json->SetNumberField(TEXT("healAmount"),Snapshot.HealAmount); Json->SetNumberField(TEXT("dashSpeed"),Snapshot.DashSpeed);
    TArray<TSharedPtr<FJsonValue>> Weapons; // 只输出稳定ID及弹药数量，不输出Actor、资源路径或类名。
    for (const FDemoSavedWeapon& Weapon:Snapshot.Weapons)
    {
        TSharedRef<FJsonObject> Item=MakeShared<FJsonObject>(); // 本次导出的单把武器值对象。
        Item->SetStringField(TEXT("id"),Weapon.Id.ToString().ToLower()); Item->SetNumberField(TEXT("ammo"),Weapon.Ammo); Item->SetNumberField(TEXT("reserve"),Weapon.Reserve);
        Weapons.Add(MakeShared<FJsonValueObject>(Item));
    }
    Json->SetArrayField(TEXT("weapons"),Weapons); Json->SetStringField(TEXT("primaryId"),Snapshot.PrimaryId.IsNone()?TEXT(""):Snapshot.PrimaryId.ToString().ToLower()); Json->SetNumberField(TEXT("activeSlot"),Snapshot.ActiveSlot);
    if (!Snapshot.TeamMembers.IsEmpty())
    {
        if (Snapshot.Version<5) { Reject(Error,TEXT("合作扩展需要V5及以上检查点")); return nullptr; }
        TArray<TSharedPtr<FJsonValue>> Members; // 可选扩展仅在合作档输出，单人旧请求无此字段以保持幂等哈希。
        for (const FDemoCoopPlayerCheckpoint& Member:Snapshot.TeamMembers) Members.Add(MakeShared<FJsonValueObject>(WriteMember(Member,Snapshot.Version)));
        Json->SetArrayField(TEXT("teamMembers"),Members);
    }
    TSharedRef<FJsonObject> Ordered=MakeShared<FJsonObject>(); // 沿用旧V5反射导出的字段顺序，既存本地MD5水位不会仅因Codec迁移发生伪冲突。
    for (const TCHAR* Key:{TEXT("version"),TEXT("bEndless"),TEXT("bestEndlessLevel"),TEXT("pistolChallenge"),TEXT("unlockedAmmoIds"),TEXT("selectedAmmoId"),TEXT("createdLocal"),TEXT("savedUtc"),TEXT("runId"),TEXT("phase"),TEXT("difficulty"),TEXT("completedLevel"),TEXT("coins"),TEXT("silverCoins"),TEXT("kills"),TEXT("purchases"),TEXT("goldPurchases"),TEXT("silverPurchases"),TEXT("upgradeProgress"),TEXT("maxHealth"),TEXT("health"),TEXT("damageBonus"),TEXT("magazineBonus"),TEXT("healAmount"),TEXT("dashSpeed"),TEXT("weapons"),TEXT("primaryId"),TEXT("activeSlot"),TEXT("teamMembers"),TEXT("onlineOwnerKey"),TEXT("gambling"),TEXT("campaignId"),TEXT("contentRevision"),TEXT("runSeed"),TEXT("build"),TEXT("contentSnapshot"),TEXT("contentFingerprint"),TEXT("slotStorageKey"),TEXT("ownerPlayerKey"),TEXT("weaponOwnership")})
        if (Json->HasField(Key)) Ordered->SetField(Key,Json->TryGetField(Key)); // 可选团队放在尾部，单人从不写空数组或null。
    return Ordered;
}

bool DemoCheckpointJsonCodec::Decode(const TSharedPtr<FJsonObject>& Data,UDemoRunSave& Out,FString& Error)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__); Error.Reset();
    if (!Data||!Out.TeamMembers.IsEmpty()) return Reject(Error,TEXT("云检查点或导入候选非法"));
    if (!AllowOnly(*Data,{TEXT("version"),TEXT("createdLocal"),TEXT("savedUtc"),TEXT("runId"),TEXT("phase"),TEXT("difficulty"),TEXT("completedLevel"),TEXT("bEndless"),TEXT("bestEndlessLevel"),TEXT("pistolChallenge"),TEXT("coins"),TEXT("silverCoins"),TEXT("kills"),TEXT("purchases"),TEXT("goldPurchases"),TEXT("silverPurchases"),TEXT("upgradeProgress"),TEXT("unlockedAmmoIds"),TEXT("selectedAmmoId"),TEXT("maxHealth"),TEXT("health"),TEXT("damageBonus"),TEXT("magazineBonus"),TEXT("healAmount"),TEXT("dashSpeed"),TEXT("weapons"),TEXT("primaryId"),TEXT("activeSlot"),TEXT("teamMembers"),TEXT("onlineOwnerKey"),TEXT("gambling"),TEXT("campaignId"),TEXT("contentRevision"),TEXT("runSeed"),TEXT("build"),TEXT("contentSnapshot"),TEXT("contentFingerprint"),TEXT("slotStorageKey"),TEXT("ownerPlayerKey"),TEXT("weaponOwnership")},Error)) return false;
    Out.OnlineOwnerKey.Empty(); // 候选对象不能携带上一次解析的在线绑定。
    if (Data->HasField(TEXT("onlineOwnerKey")) && (!ReadString(*Data,TEXT("onlineOwnerKey"),Out.OnlineOwnerKey,36,Error) || Out.OnlineOwnerKey.IsEmpty())) return Reject(Error,TEXT("在线存档绑定格式无效"));
    FString Created,Saved,Phase,Difficulty,Primary; // 稳定文本先解析再映射，不把远端字符串写入任意UObject属性。
    if (!ReadInt(*Data,TEXT("version"),Out.Version,true,Error)||Out.Version<1||Out.Version>10) return Reject(Error,TEXT("云检查点版本不受支持"));
    if (Out.Version>=10)
    {
        const TSharedPtr<FJsonObject>* Ownership=nullptr; // 只借用本次响应，Decode深拷贝值。
        if (!ReadString(*Data,TEXT("slotStorageKey"),Out.SlotStorageKey,36,Error)||!ReadString(*Data,TEXT("ownerPlayerKey"),Out.OwnerPlayerKey,36,Error)
            ||!Data->TryGetObjectField(TEXT("weaponOwnership"),Ownership)||!DemoOwnershipJson::Decode(*Ownership,Out.Ownership)) return Reject(Error,TEXT("云根拥有权/槽身份非法"));
    }
    else if (Data->HasField(TEXT("slotStorageKey"))||Data->HasField(TEXT("ownerPlayerKey"))||Data->HasField(TEXT("weaponOwnership"))) return Reject(Error,TEXT("旧版本不能携带新拥有权"));
    Out.CampaignId = NAME_None; Out.ContentRevision = 0; Out.RunSeed = 0; Out.ContentSnapshot.Reset(); Out.ContentFingerprint.Reset(); // 旧格式显式默认，不继承解码目标的历史值。
    if (Out.Version >= 8)
    {
        FString Campaign; // 仅空或steam_demo，后续语义校验拒绝未来内容和资源路径。
        if (!ReadString(*Data, TEXT("campaignId"), Campaign, 32, Error) || (Campaign != TEXT("") && Campaign != TEXT("steam_demo"))
            || !ReadInt(*Data, TEXT("contentRevision"), Out.ContentRevision, true, Error) || !ReadInt(*Data, TEXT("runSeed"), Out.RunSeed, true, Error))
            return Reject(Error, TEXT("云战役内容字段非法"));
        Out.CampaignId = Campaign.IsEmpty() ? NAME_None : FName(*Campaign);
    }
    else if (Data->HasField(TEXT("campaignId")) || Data->HasField(TEXT("contentRevision")) || Data->HasField(TEXT("runSeed"))) return Reject(Error, TEXT("旧格式不能携带新版战役字段"));
    if (Out.Version >= 9)
    {
        if (!ReadString(*Data, TEXT("contentSnapshot"), Out.ContentSnapshot, 65536, Error)
            || !ReadString(*Data, TEXT("contentFingerprint"), Out.ContentFingerprint, 45, Error))
            return Reject(Error, TEXT("云检查点缺少或包含非法冻结内容")); // 具体UTF8字节/指纹/规范JSON边界由Definition校验。
    }
    else if (Data->HasField(TEXT("contentSnapshot")) || Data->HasField(TEXT("contentFingerprint")))
        return Reject(Error, TEXT("旧格式不能携带V9冻结内容"));
    if (!ReadBuild(*Data, Out.Build, Out.Version, Error)) return false;
    if(!ReadGamble(*Data,Out.Gambling,Out.Version,Error)) return false;
    if (!ReadString(*Data,TEXT("createdLocal"),Created,64,Error)||!ReadString(*Data,TEXT("savedUtc"),Saved,64,Error)||!ReadString(*Data,TEXT("runId"),Out.RunId,64,Error)
        ||!ReadString(*Data,TEXT("phase"),Phase,32,Error)||!ReadString(*Data,TEXT("difficulty"),Difficulty,16,Error)||!ReadString(*Data,TEXT("primaryId"),Primary,32,Error)) return false;
    if (!FDateTime::ParseIso8601(*Created,Out.CreatedLocal)||!FDateTime::ParseIso8601(*Saved,Out.SavedUtc)) return Reject(Error,TEXT("云检查点时间格式非法"));
    if (Phase==TEXT("Hub")) Out.Phase=EDemoPhase::Hub; else if (Phase==TEXT("Reward")) Out.Phase=EDemoPhase::Reward; else if (Phase==TEXT("Intermission")) Out.Phase=EDemoPhase::Intermission; else if (Phase==TEXT("Victory")) Out.Phase=EDemoPhase::Victory; else return Reject(Error,TEXT("云检查点阶段非法"));
    if (Difficulty==TEXT("easy")) Out.Difficulty=EDemoDifficulty::Easy; else if (Difficulty==TEXT("normal")) Out.Difficulty=EDemoDifficulty::Normal; else if (Difficulty==TEXT("hard")) Out.Difficulty=EDemoDifficulty::Hard; else if (Difficulty==TEXT("hell")) Out.Difficulty=EDemoDifficulty::Hell; else return Reject(Error,TEXT("云检查点难度非法"));
    Out.PrimaryId=Primary.IsEmpty()?NAME_None:FName(*Primary);
    if (!ReadInt(*Data,TEXT("completedLevel"),Out.CompletedLevel,true,Error)||!ReadInt(*Data,TEXT("coins"),Out.Coins,true,Error)||!ReadInt(*Data,TEXT("kills"),Out.Kills,true,Error)
        ||!ReadInt(*Data,TEXT("purchases"),Out.Purchases,true,Error)||!ReadInt(*Data,TEXT("activeSlot"),Out.ActiveSlot,true,Error)
        ||!ReadInt(*Data,TEXT("silverCoins"),Out.SilverCoins,false,Error)||!ReadInt(*Data,TEXT("goldPurchases"),Out.GoldPurchases,false,Error)||!ReadInt(*Data,TEXT("silverPurchases"),Out.SilverPurchases,false,Error)
        ||!ReadInt(*Data,TEXT("bestEndlessLevel"),Out.BestEndlessLevel,false,Error)||!ReadInt(*Data,TEXT("pistolChallenge"),Out.PistolChallenge,false,Error)) return false;
    if (Data->HasField(TEXT("bEndless"))&&(!Data->HasTypedField<EJson::Boolean>(TEXT("bEndless"))||!Data->TryGetBoolField(TEXT("bEndless"),Out.bEndless))) return Reject(Error,TEXT("云检查点模式类型非法"));
    if (!ReadFloat(*Data,TEXT("maxHealth"),Out.MaxHealth,Error)||!ReadFloat(*Data,TEXT("health"),Out.Health,Error)||!ReadFloat(*Data,TEXT("damageBonus"),Out.DamageBonus,Error)
        ||!ReadFloat(*Data,TEXT("magazineBonus"),Out.MagazineBonus,Error)||!ReadFloat(*Data,TEXT("healAmount"),Out.HealAmount,Error)||!ReadFloat(*Data,TEXT("dashSpeed"),Out.DashSpeed,Error)) return false;
    const TSharedPtr<FJsonObject>* Progress=nullptr; // JSON拥有对象，解析期间只借用。
    if (Data->TryGetObjectField(TEXT("upgradeProgress"),Progress))
    {
        if (!AllowOnly(**Progress,{TEXT("goldLevels"),TEXT("silverLevels"),TEXT("permanentDamage"),TEXT("permanentHealth"),TEXT("permanentMagazine")},Error)
            ||!ReadLevels(**Progress,TEXT("goldLevels"),Out.UpgradeProgress.GoldLevels,Error)||!ReadLevels(**Progress,TEXT("silverLevels"),Out.UpgradeProgress.SilverLevels,Error)
            ||!ReadFloat(**Progress,TEXT("permanentDamage"),Out.UpgradeProgress.PermanentDamage,Error)||!ReadFloat(**Progress,TEXT("permanentHealth"),Out.UpgradeProgress.PermanentHealth,Error)||!ReadFloat(**Progress,TEXT("permanentMagazine"),Out.UpgradeProgress.PermanentMagazine,Error)) return false;
    }
    else if (Out.Version>=3||(Data->HasField(TEXT("upgradeProgress"))&&!Data->HasTypedField<EJson::Null>(TEXT("upgradeProgress")))) return Reject(Error,TEXT("云检查点缺少成长账本或字段类型非法"));
    const TArray<TSharedPtr<FJsonValue>>* Ammo=nullptr; // 仅本次解析借用，不保存远端JSON指针。
    if (Data->TryGetArrayField(TEXT("unlockedAmmoIds"),Ammo))
    {
        if (Ammo->Num()>4) return Reject(Error,TEXT("云弹药类型数量非法")); Out.UnlockedAmmoIds.Reset();
        for (const TSharedPtr<FJsonValue>& Value:*Ammo)
        {
            FString Id; // 单项稳定目录ID，只允许短字符串，语义由Validate白名单校验。
            if (!Value||Value->Type!=EJson::String||!Value->TryGetString(Id)||Id.Len()>32) return Reject(Error,TEXT("云弹药ID非法")); Out.UnlockedAmmoIds.Add(FName(*Id));
        }
        FString Selected; // 已装配ID必须明确提供，Validate验证它属于已解锁集合。
        if (!ReadString(*Data,TEXT("selectedAmmoId"),Selected,32,Error)) return false; Out.SelectedAmmoId=FName(*Selected);
    }
    else if (Out.Version>=4||(Data->HasField(TEXT("unlockedAmmoIds"))&&!Data->HasTypedField<EJson::Null>(TEXT("unlockedAmmoIds")))) return Reject(Error,TEXT("云检查点缺少弹药记录或字段类型非法"));
    const TArray<TSharedPtr<FJsonValue>>* Weapons=nullptr; // 最多四个武器值对象，候选保存深拷贝值。
    if (!Data->TryGetArrayField(TEXT("weapons"),Weapons)||Weapons->Num()>4) return Reject(Error,TEXT("云武器快照非法")); Out.Weapons.Reset();
    for (const TSharedPtr<FJsonValue>& Value:*Weapons)
    {
        const TSharedPtr<FJsonObject>* Item=nullptr; // 元素必须为对象，拒绝AsObject断言式读取。
        FDemoSavedWeapon Weapon; FString Id; // 当前候选武器值，整项成功才加入Out。
        if (!Value||!Value->TryGetObject(Item)||!AllowOnly(**Item,{TEXT("id"),TEXT("ammo"),TEXT("reserve")},Error)||!ReadString(**Item,TEXT("id"),Id,32,Error)
            ||!ReadInt(**Item,TEXT("ammo"),Weapon.Ammo,true,Error)||!ReadInt(**Item,TEXT("reserve"),Weapon.Reserve,true,Error)) return Reject(Error,TEXT("云武器字段非法"));
        Weapon.Id=FName(*Id); Out.Weapons.Add(Weapon);
    }
    if (Data->HasField(TEXT("teamMembers")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Members=nullptr; // 合作扩展必须整队恢复，不允许字段存在却静默忽略。
        if (Out.Version<5||!Data->TryGetArrayField(TEXT("teamMembers"),Members)||Members->Num()<1||Members->Num()>4) return Reject(Error,TEXT("云合作队伍数量/版本非法"));
        TSet<FString> Keys; // 解码阶段先拒绝重复，领域校验继续验证身份格式和全部权限。
        for (const TSharedPtr<FJsonValue>& Value:*Members)
        {
            const TSharedPtr<FJsonObject>* Json=nullptr; FDemoCoopPlayerCheckpoint Member; // 临时候选成员，只在整项完整后提交。
            if (!Value||!Value->TryGetObject(Json)||!ReadMember(**Json,Member,Error,Out.Version)||Keys.Contains(Member.PlayerKey)) return Reject(Error,TEXT("云合作成员重复或无效"));
            Keys.Add(Member.PlayerKey); Out.TeamMembers.Add(MoveTemp(Member));
        }
    }
    if (!Out.UpgradeLegacy()) return Reject(Error,TEXT("云检查点语义/版本迁移校验失败"));
    return true;
}
