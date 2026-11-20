// 职责：枪墙本人报价与金币首购/免费装备原子事务。使用：Owner PC组件解析身份，合法Hub/活动槽整槽本地耐久后才发布；云Outbox异步不参与退款，依赖既有Progress/RunFlow/增量库存。
// Editor单人全解锁只允许终端临时借用，独立于下方真实来源/收据/耐久交易，不编入包体或迁移枪权。
#include "Economy/DemoShopComponent.h"
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Interaction/HubShops/DemoHubShopActors.h"
#include "Interaction/HubShops/DemoHubShopWorldSubsystem.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/Coop/Ready/DemoTeamReadyComponent.h"
#include "Game/FPSDemoGameMode.h"
#include "Game/Preparation/DemoEncounterPreparationComponent.h"
#include "Game/Flow/DemoRunFlowComponent.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "GAS/DemoAttributeSet.h"
#include "Save/DemoRunSave.h"
#include "Debug/DemoEditorCheats.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Misc/SecureHash.h"

namespace
{
    /** 仅为内容一致性，不是认证；输入是已限长的稳定值，不存资源路径或余额。 */
    FString ShopDigest(const FString& Text)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        const FTCHARToUTF8 Bytes(*Text); // 同步UTF8字节仅供当前SHA计算。
        uint8 Hash[FSHA1::DigestSize]; // 固定20字节，无动态跨线程状态。
        FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash);
        return TEXT("sha1:") + BytesToHex(Hash, FSHA1::DigestSize).ToLower();
    }
    /** RPC操作ID只认规范GUID；同请求不同载荷必须拒绝，不能用自由字符串扩张收据。 */
    bool IsShopGuid(const FString& Text)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        FGuid Value; // 仅当前意图的格式检查，不授予拥有者身份。
        return FGuid::ParseExact(Text, EGuidFormats::DigitsWithHyphens, Value) && Value.IsValid() && Text == Value.ToString(EGuidFormats::DigitsWithHyphens);
    }
    /** 所有参与幂等的意图字段按固定顺序冻结，旧World请求只可回旧收据而不可再次应用。 */
    FString GunPayloadHash(const FDemoHubGunIntent& Intent)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        return ShopDigest(FString::Printf(TEXT("%s|%s|%s|%u|%s|%s|%d|%d"), *Intent.OperationId, *Intent.DisplayId.ToString(), *Intent.OfferId.ToString(),
            Intent.WorldEpoch, *Intent.SlotEpoch.ToString(EGuidFormats::DigitsWithHyphens), *Intent.QuoteKey, Intent.ExpectedPrice, Intent.ExpectedShopSerial));
    }
    /** 同步借用Pawn本人PS，房主与远端采用同一路径，不以Player0替代购买者。 */
    UDemoPlayerProgressComponent* GunProgress(const ADemoCharacter* Player)
    {
        DEMO_LOG_STATIC_TICK(Player);
        const ADemoPlayerState* State = Player ? Player->GetPlayerState<ADemoPlayerState>() : nullptr;
        return State ? State->GetProgressComponent() : nullptr;
    }
    /** 创建明确失败回执，无副作用；失败不会伪造已保存或赠枪。 */
    FDemoHubGunResult GunRejected(const FDemoHubGunIntent& Intent, FName Code, const FString& Message)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        FDemoHubGunResult Result; // 默认Rejected，仅保留本人关联ID。
        Result.OperationId = Intent.OperationId; Result.Result = FDemoCommandResult::Reject(Code, Message);
        UE_LOG(LogFPSDemo, Log, TEXT("HUB_GUN_REJECT code=%s"), *Code.ToString());
        return Result;
    }
}

bool UDemoShopComponent::IsTransactionInProgress() const { DEMO_LOG_TICK(); return bTransactionInProgress; }

FString UDemoShopComponent::GetHubShopBlockReason(ADemoCharacter* Player,FName* OutCode) const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 物理位置在Hub不等于逻辑Hub。
    const AFPSDemoGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AFPSDemoGameMode>() : nullptr; // 唯一权威出发/旅行屏障。
    const UDemoRunSaves* Saves = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>() : nullptr;
    const ADemoPlayerController* PC = Player ? Cast<ADemoPlayerController>(Player->GetController()) : nullptr; // 身份由真实控制器解析。
    if (OutCode) *OutCode=NAME_None;
    // 捕获当前this及可选本次输出指针；只在本同步校验栈格式化拒绝，不保留跨帧或从中文反推错误码。
    const auto Block=[this,OutCode](FName Code,const TCHAR* Message)->FString { DEMO_LOG_TICK(); if(OutCode)*OutCode=Code; return FString(Message); };
    if (bStopped || bTransactionInProgress || !GetOwner() || !GetOwner()->HasAuthority() || !Mode || !GunProgress(Player) || !SaveCheckpointRequest.IsBound()) return Block(TEXT("ShopUnavailable"),TEXT("商店服务尚未就绪"));
    if (!State || State->Phase != EDemoPhase::Hub) return Block(TEXT("WrongPhase"),TEXT("仅逻辑安全区可购买枪械"));
    if (!Player->GetDemoAttributes() || Player->GetDemoAttributes()->GetHealth()<=0 || Player->IsCoopEliminated()) return Block(TEXT("NotAlive"),TEXT("阵亡角色不能交易"));
    if (!PC || !PC->IsCoopRegistered() || !Player->CanUsePlayerSkills()) return Block(TEXT("ModalBlocked"),TEXT("当前菜单或角色状态不允许交易"));
    if ((Mode->GetPreparationSystem() && Mode->GetPreparationSystem()->IsPending()) || !Mode->GetRunFlow() || !Mode->GetRunFlow()->CanAcceptConnections()) return Block(TEXT("Preparing"),TEXT("准备出发或旅行时不能交易"));
    if (!Saves || Saves->GetActiveSlot() < 0 || Saves->GetActiveSlot() > 2 || !Saves->GetSlot(Saves->GetActiveSlot())) return Block(TEXT("NoActiveSlot"),TEXT("请先选择有效活动存档栏"));
    if (!GunProgress(Player)->GetSnapshot().Ownership.Validate(true)) return Block(TEXT("MigrationPending"),TEXT("枪械拥有权尚未完成迁移，请重新读取当前存档"));
    return FString();
}

FDemoHubGunQuote UDemoShopComponent::QueryGunOffer(ADemoCharacter* Player, FName DisplayId, uint32 WorldEpoch, const FGuid& SlotEpoch) const
{
    DEMO_LOG_CALL();
    FDemoHubGunQuote Quote; // 拒绝时也回传请求展示，客户端可关联最新选择。
    Quote.DisplayId = DisplayId; Quote.WorldEpoch = WorldEpoch; Quote.SlotEpoch = SlotEpoch;
    Quote.Message = GetHubShopBlockReason(Player,&Quote.Code);
    if (!Quote.Message.IsEmpty()) return Quote;
    const UDemoHubShopWorldSubsystem* World = GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>(); // 注册表而非客户端Actor路径。
    const UDemoHubShopDefinition* Data = World ? World->GetDefinition() : nullptr;
    const ADemoGunDisplay* Display = World ? World->FindDisplay(DisplayId) : nullptr;
    if (!Data || !Data->bPurchaseRoomEnabled || !Display || !SlotEpoch.IsValid()) { Quote.Code = TEXT("UnknownDisplay"); Quote.Message = TEXT("枪墙目录或展示尚未就绪"); return Quote; }
    if (!World->ValidateUse(Player, Display, WorldEpoch, Quote.Message,&Quote.Code)) return Quote;
    const FDemoHubWeaponOffer* Offer = Data->FindOffer(Display->OfferId); // 作者配置唯一商品映射。
    if (!Offer) { Quote.Code = TEXT("UnknownOffer"); Quote.Message = TEXT("商品配置不存在"); return Quote; }
    const ADemoWeaponBase* CDO = Data->ResolveWeaponCDO(Offer->WeaponId, Quote.Message); // 真实Config，不生成射击Actor。
    if (!CDO) { Quote.Code = TEXT("ContentMismatch"); return Quote; }
    const FDemoPlayerProgressSnapshot& Progress = GunProgress(Player)->GetSnapshot(); // 当前个人来源账本与高水位。
    Quote.OfferId = Offer->OfferId; Quote.WeaponId = Offer->WeaponId; Quote.bOwned = Progress.Ownership.Contains(Offer->WeaponId);
    Quote.Price = Quote.bOwned ? 0 : Offer->GoldPrice; Quote.ShopSerial = Progress.Ownership.ShopSerial;
    Quote.Name = CDO->Config.DisplayName.ToString(); Quote.Damage = CDO->Config.BaseDamage; Quote.RPM = CDO->Config.RoundsPerMinute;
    Quote.Pellets = CDO->GetPelletCount(); // 虚方法按实际武器类型：普通枪1，不误读未启用的默认PelletCount8。
    const FString Kind = Offer->WeaponId == TEXT("rifle") ? TEXT("步枪") : Offer->WeaponId == TEXT("shotgun") ? TEXT("霰弹枪") : TEXT("狙击枪");
    Quote.Type = Kind + (CDO->Config.FireMode == EDemoFireMode::Automatic ? TEXT(" · 自动") : TEXT(" · 半自动"));
    Quote.QuoteKey = ShopDigest(FString::Printf(TEXT("%u|%s|%s|%.9g|%d|%.9g|%d|%d"), Data->MakeOfferKey(Offer->OfferId), *Quote.Name, *Quote.Type,
        double(Quote.Damage), Quote.Pellets, double(Quote.RPM), CDO->Config.InitialAmmo, CDO->Config.InitialReserve));
    if (!Quote.bOwned && !Offer->bEnabled) { Quote.Code = TEXT("OfferDisabled"); Quote.Message = TEXT("该商品暂停新购，已有枪械仍可免费装备"); }
    else if (!Quote.bOwned)
    {
        int32 Required=Offer->GoldPrice; // 原报价仍完整展示，Editor本GI免扣仅改变余额资格。
#if WITH_EDITOR
        Required=DemoEditorCheats::Debit(Player,true,Required);
#endif
        if (Progress.Coins < Required) { Quote.Code = TEXT("InsufficientGold"); Quote.Message = TEXT("个人金币不足"); }
    }
    return Quote;
}

FDemoHubGunResult UDemoShopComponent::PurchaseOrEquipGun(ADemoCharacter* Player, const FDemoHubGunIntent& Intent, const FGuid& CurrentSlotEpoch)
{
    DEMO_LOG_CALL();
    if (!IsShopGuid(Intent.OperationId) || Intent.QuoteKey.Len() != 45) return GunRejected(Intent, TEXT("MalformedIntent"), TEXT("购买意图格式无效"));
    UDemoPlayerProgressComponent* Progress = GunProgress(Player); // 权威唯一账本。
    const UDemoRunSaves* ActiveSaves=GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 幂等查询也只认当前有效槽/本人，不能返回其他槽收据。
    if (!GetOwner()->HasAuthority() || bTransactionInProgress || !Player || Player->GetWorld()!=GetWorld() || !Progress || !ActiveSaves || !ActiveSaves->GetSlot(ActiveSaves->GetActiveSlot()) || !Progress->GetSnapshot().Ownership.Validate(true))
        return GunRejected(Intent,TEXT("NotReady"),TEXT("个人存档或交易正在准备中"));
    const FString PayloadHash = GunPayloadHash(Intent); // 同ID异载荷绝不能执行第二次。
    for (const FDemoWeaponShopReceipt& Receipt : Progress->GetSnapshot().Ownership.ShopReceipts)
    {
        if (Receipt.RequestId != Intent.OperationId) continue;
        if (Receipt.PayloadHash != PayloadHash) return GunRejected(Intent, TEXT("RequestConflict"), TEXT("同一请求的内容发生变化，操作已拒绝"));
        FDemoHubGunResult Replay; // 已提交只返回原事实，不再次扣款/赠弹或重跑装备动作。
        Replay.OperationId = Intent.OperationId; Replay.Result = FDemoCommandResult::Applied(true, TEXT("该操作已保存，请刷新枪墙状态")); Replay.Result.Code = TEXT("AlreadyCommitted");
        Replay.Quote = QueryGunOffer(Player, Intent.DisplayId, Intent.WorldEpoch, CurrentSlotEpoch); Replay.ActiveSlot = Player->GetWeaponComponent()->GetActiveSlot();
        return Replay;
    }
    FName BlockCode; // 未命中已耐久收据的新交易才重验当前阶段/菜单/生命，回放不会因打开菜单丢原确认。
    const FString Reason=GetHubShopBlockReason(Player,&BlockCode);
    if (!Reason.IsEmpty()) return GunRejected(Intent,BlockCode,Reason);
    if (!CurrentSlotEpoch.IsValid() || Intent.SlotEpoch != CurrentSlotEpoch) return GunRejected(Intent, TEXT("StaleEpoch"), TEXT("存档会话已变化，请重新对准商品"));
    const FDemoHubGunQuote Quote = QueryGunOffer(Player, Intent.DisplayId, Intent.WorldEpoch, CurrentSlotEpoch); // 服务端重新取真实报价/距角/LOS。
    if (!Quote.Code.IsNone()) return GunRejected(Intent, Quote.Code, Quote.Message);
    if (Quote.OfferId != Intent.OfferId || Quote.QuoteKey != Intent.QuoteKey || Quote.Price != Intent.ExpectedPrice)
        return GunRejected(Intent, TEXT("StaleQuote"), TEXT("商品报价已变化，请重新确认"));
    if (Quote.ShopSerial == MAX_int32 || Quote.ShopSerial != Intent.ExpectedShopSerial) return GunRejected(Intent, TEXT("StaleSerial"), TEXT("操作序号已变化，请刷新"));
    FDemoHubGunResult Result; Result.OperationId = Intent.OperationId; // 成功回执只在下面耐久后构建。
    {
        TGuardValue<bool> Guard(bTransactionInProgress, true); // 游戏线程串行整槽提交，阻止GAS/生成回调递归商店或出发。
        const FDemoPlayerProgressSnapshot Before = Progress->GetSnapshot(); // 未发布完整旧值，含私有娱乐局；绝不发RPC。
        FDemoPlayerProgressSnapshot Next = Before; // 本次只变钱包/拥有权/收据。
        int32 Debit = Quote.Price; // 实付初值为权威报价；中间余额用int64验证。
#if WITH_EDITOR
        Debit = DemoEditorCheats::Debit(Player, true, Debit); // 本次GI调试免扣，仍保存真实0实付与正常OfferRevision，不伪造金币。
#endif
        if (Debit < 0 || int64(Next.Coins) - Debit < 0) return GunRejected(Intent, TEXT("InsufficientGold"), TEXT("个人金币不足"));
        const UDemoHubShopDefinition* Data = GetWorld()->GetSubsystem<UDemoHubShopWorldSubsystem>()->GetDefinition(); // 同栈冻结目录，不异步热换。
        if (!Quote.bOwned && !Next.Ownership.AddPurchased(Quote.WeaponId, FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens), Debit, Data->CatalogRevision))
            return GunRejected(Intent, TEXT("OwnershipRejected"), TEXT("枪械拥有权无法提交"));
        bool bAllowSupply = !Quote.bOwned; // 首购只给一次正常Config供弹，旧合法权按冻结标记处理。
        for (FDemoWeaponOwnershipEntry& Entry : Next.Ownership.Entries) if (Entry.WeaponId == Quote.WeaponId && !Entry.bInitialSupplyGranted)
        {
            if (Next.Ownership.Revision == MAX_int32) return GunRejected(Intent, TEXT("RevisionExhausted"), TEXT("拥有权版本已到上限"));
            bAllowSupply = true; Entry.bInitialSupplyGranted = true; ++Next.Ownership.Revision;
        }
        UDemoWeaponComponent* Inventory = Player->GetWeaponComponent(); // 增量候选不改旧实例/弹量/射击截止，不在保存前装备。
        if (!Inventory || !Inventory->PrepareShopSelection(Quote.WeaponId, bAllowSupply)) return GunRejected(Intent, TEXT("InventoryPrepareFailed"), TEXT("武器资源无法准备，金币未扣除"));
        Next.Coins = int32(int64(Next.Coins) - Debit); Next.UnlockedWeaponIds = Next.Ownership.EffectiveIds();
        FDemoWeaponShopReceipt Receipt; // 与钱包/来源/目标库存同一次整槽保存，掉线重试不重复应用。
        Receipt.RequestId = Intent.OperationId; Receipt.PayloadHash = PayloadHash; Receipt.CommandSerial = ++Next.Ownership.ShopSerial;
        Receipt.WeaponId = Quote.WeaponId; Receipt.ChargedGold = Debit; Receipt.OwnershipRevision = Next.Ownership.Revision; Receipt.bBought = !Quote.bOwned;
        if (Next.Ownership.ShopReceipts.Num() >= 16) Next.Ownership.ShopReceipts.RemoveAt(0);
        Next.Ownership.ShopReceipts.Add(Receipt);
        if (!Progress->Commit(Next, false)) { Inventory->AbortShopSelection(); return GunRejected(Intent, TEXT("LedgerRejected"), TEXT("个人账本未能提交")); }
        UDemoRunSaves* Saves = GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>(); // 已验活动槽；本地成功即可发布，云另走不可变Outbox。
        const uint64 BeforeSave = Saves->GetChangeSerial(); // 防委托空成功，必须观察真实完整Store更新缓存的本地序列。
        if (!SaveCheckpointRequest.Execute() || Saves->GetChangeSerial() == BeforeSave)
        {
            const bool bRestored = Progress->Commit(Before, false); // 候选未发布，恢复整个人钱包/来源/收据。
            Inventory->AbortShopSelection();
            if (!bRestored) { bStopped = true; UE_LOG(LogFPSDemo, Error, TEXT("HUB_GUN_ROLLBACK_FAILED transactions stopped")); }
            return GunRejected(Intent, TEXT("SaveFailed"), TEXT("完整本地保存失败，购买未生效，金币与原装备已保留"));
        }
        const bool bEquipped = Inventory->PublishShopSelection(); // 从此不能退款后仍保留已落盘的枪权。
        Progress->PublishCommitted();
        Result.Result = FDemoCommandResult::Applied(true, bEquipped ? (Quote.bOwned ? TEXT("已免费装备，弹量保持") : TEXT("购买已保存，武器已装备")) : TEXT("购买已保存，装备同步中；可重新对准免费装备"));
        Result.Result.Code = bEquipped ? FName(TEXT("Committed")) : FName(TEXT("EquipmentSyncPending"));
        Result.ActiveSlot = Inventory->GetActiveSlot();
        if (AFPSDemoGameMode* Mode = GetWorld()->GetAuthGameMode<AFPSDemoGameMode>()) Mode->GetTeamReadySystem()->InvalidatePlayer(Cast<ADemoPlayerController>(Player->GetController()), TEXT("装备变化，请重新准备"));
        UE_LOG(LogFPSDemo, Display, TEXT("HUB_GUN_COMMITTED weapon=%s bought=%d serial=%d equipped=%d"), *Quote.WeaponId.ToString(), !Quote.bOwned, Receipt.CommandSerial, bEquipped); // 不公开余额/其他成员隐私。
    }
    Result.Quote = QueryGunOffer(Player, Intent.DisplayId, Intent.WorldEpoch, CurrentSlotEpoch);
    return Result;
}

FDemoCommandResult UDemoShopComponent::EquipOwnedFromTerminal(FName WeaponId,ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    const FString Reason=GetTerminalBlockReason(Player); // 原终端会话/逻辑阶段/本人距离仍必须有效。
    if (!Reason.IsEmpty()) return FDemoCommandResult::Reject(TEXT("TerminalRejected"),Reason);
    UDemoPlayerProgressComponent* Progress=GunProgress(Player);
    UDemoRunSaves* Saves=GetWorld()->GetGameInstance()->GetSubsystem<UDemoRunSaves>();
    if (!Progress || !Progress->GetSnapshot().Ownership.Validate(true)) return FDemoCommandResult::Reject(TEXT("WeaponNotOwned"),TEXT("枪械拥有权尚未就绪，请重新读取存档"));
    if (!Saves || !Saves->GetSlot(Saves->GetActiveSlot())) return FDemoCommandResult::Reject(TEXT("NoActiveSlot"),TEXT("请先选择有效存档栏"));
    if (!Progress->GetSnapshot().Ownership.Contains(WeaponId))
    {
#if WITH_EDITOR
        // 单人调试权限只借用当前Pawn库存，不进入收据/保存事务；真实拥有仍完整走下方耐久屏障。
        UDemoWeaponComponent* TestInventory=Player->GetWeaponComponent();
        if (GetWorld()->GetNetMode()==NM_Standalone&&Player->IsLocallyControlled()&&Progress->IsWeaponUnlocked(WeaponId)&&TestInventory)
        {
            if (!TestInventory->ExecuteEditorTestPrimarySelection(WeaponId)) return FDemoCommandResult::Reject(TEXT("InventoryPrepareFailed"),TEXT("调试武器资源或表现未就绪"));
            FDemoCommandResult TestResult=FDemoCommandResult::Applied(false,TEXT("已临时装备，仅本次试玩有效；原存档装备保持")); // 明确内存生效但非耐久成功，不伪报保存。
            TestResult.Code=TEXT("EditorTemporaryEquipment"); return TestResult;
        }
#endif
        return FDemoCommandResult::Reject(TEXT("WeaponNotOwned"),TEXT("此枪尚未拥有，请到枪店墙上购买"));
    }
    const TGuardValue<bool> Guard(bTransactionInProgress,true); // 统一整槽序列，不能与枪墙/赌场交错捕获。
    const FDemoPlayerProgressSnapshot Before=Progress->GetSnapshot(); FDemoPlayerProgressSnapshot Next=Before;
    if (Next.Ownership.ShopSerial==MAX_int32) return FDemoCommandResult::Reject(TEXT("StaleSerial"),TEXT("操作序号已到上限"));
    bool bAllowSupply=false; // 仅旧Legacy尚未领取的一次供弹，不为反复装备提供补给。
    for (FDemoWeaponOwnershipEntry& Entry:Next.Ownership.Entries) if (Entry.WeaponId==WeaponId && !Entry.bInitialSupplyGranted)
    {
        if (Next.Ownership.Revision==MAX_int32) return FDemoCommandResult::Reject(TEXT("RevisionExhausted"),TEXT("拥有权版本已到上限"));
        bAllowSupply=true; Entry.bInitialSupplyGranted=true; ++Next.Ownership.Revision;
    }
    UDemoWeaponComponent* Inventory=Player->GetWeaponComponent();
    if (!Inventory || !Inventory->PrepareShopSelection(WeaponId,bAllowSupply)) return FDemoCommandResult::Reject(TEXT("InventoryPrepareFailed"),TEXT("武器资源无法准备"));
    FDemoWeaponShopReceipt Receipt; // 兼容旧PC意图无持久GUID；不授新权，服务器单次成功纳入同一收据高水位。
    Receipt.RequestId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); Receipt.PayloadHash=ShopDigest(TEXT("legacy-terminal|")+WeaponId.ToString()+TEXT("|")+Receipt.RequestId);
    Receipt.CommandSerial=++Next.Ownership.ShopSerial; Receipt.WeaponId=WeaponId; Receipt.OwnershipRevision=Next.Ownership.Revision;
    if (Next.Ownership.ShopReceipts.Num()>=16) Next.Ownership.ShopReceipts.RemoveAt(0); Next.Ownership.ShopReceipts.Add(Receipt);
    const uint64 BeforeSave=Saves->GetChangeSerial();
    if (!Progress->Commit(Next,false) || !SaveCheckpointRequest.Execute() || Saves->GetChangeSerial()==BeforeSave)
    {
        if (!Progress->Commit(Before,false)) { bStopped=true; UE_LOG(LogFPSDemo,Error,TEXT("TERMINAL_EQUIP_ROLLBACK_FAILED transactions stopped")); }
        Inventory->AbortShopSelection(); return FDemoCommandResult::Reject(TEXT("SaveFailed"),TEXT("保存失败，原装备及弹量已保留"));
    }
    const bool bEquipped=Inventory->PublishShopSelection(); Progress->PublishCommitted();
    FDemoCommandResult Result=FDemoCommandResult::Applied(true,bEquipped?TEXT("已免费装备并保存，弹量保持"):TEXT("装备选择已保存，表现同步中，请重试免费装备"));
    Result.Code=bEquipped?FName(TEXT("Committed")):FName(TEXT("EquipmentSyncPending")); return Result;
}
