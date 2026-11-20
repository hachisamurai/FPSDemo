// 职责：验证并聚合天赋内容。用法：经济/UI查询与战斗快照；FGCObject持有具体Scope和定义，禁止实例化抽象UObject或向CDO写运行时硬引用。
#include "GAS/Talents/DemoTalentCatalog.h"
#include "GAS/Talents/DemoTalentDefinition.h"
#include "Economy/Gambling/DemoGambleTypes.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "AbilitySystemComponent.h"
#include "Debug/DemoLog.h"
#include "UObject/GCObject.h" // 原生引用器由UE全局GC引用器追踪，不依赖CDO的UPROPERTY遍历。

// 使用UHT生成的UE类标志做编译期防回归；C++的is_abstract无法识别UCLASS(Abstract)。
static_assert((UDemoTalentDefinitionScope::StaticClassFlags & CLASS_Abstract) == 0,
    "Talent cache scope must be a concrete UClass in packaged builds.");

/** 目录独占的原生缓存；只保活内容，不保存目录UObject、玩家或World，防止反向保活环。 */
struct FDemoTalentCatalogCache final : public FGCObject
{
    /** 首次查询时注册引用器；此时只初始化空字段，临时Outer由调用方在游戏线程创建。 */
    FDemoTalentCatalogCache()
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] FDemoTalentCatalogCache::FDemoTalentCatalogCache"));
    }
    /** 目录BeginDestroy或析构时注销引用器；不手动销毁定义，其他ASC仍可持有它们。 */
    virtual ~FDemoTalentCatalogCache() override
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] FDemoTalentCatalogCache::~FDemoTalentCatalogCache"));
    }
    /** GC回调仅向Collector报告现有对象，可能处于收集线程；禁止创建UObject或访问World。 */
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] FDemoTalentCatalogCache::AddReferencedObjects"));
        Collector.AddReferencedObject(DefinitionsOuter);
        Collector.AddReferencedObjects(NativeDefinitions);
        Collector.AddReferencedObjects(CachedEntries);
    }
    /** UE引用链诊断使用的稳定名称；不依赖目录UObject有效性。 */
    virtual FString GetReferencerName() const override
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] FDemoTalentCatalogCache::GetReferencerName"));
        return TEXT("FDemoTalentCatalogCache");
    }
    /** 具体且非抽象的临时命名空间；只用于子对象唯一命名，Outer本身不充当GC所有权。 */
    TObjectPtr<UDemoTalentDefinitionScope> DefinitionsOuter;
    /** 原生回退定义，含15个稳定ID；由上面的Collector保活，不进入CDO反射属性。 */
    TArray<TObjectPtr<UDemoTalentDefinition>> NativeDefinitions;
    /** 已验证资产/回退的只读顺序视图；与原生数组一起明确注册GC引用。 */
    TArray<TObjectPtr<UDemoTalentDefinition>> CachedEntries;
};

void FDemoTalentCatalogCacheDeleter::operator()(FDemoTalentCatalogCache* Cache) const
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] FDemoTalentCatalogCacheDeleter::operator()"));
    delete Cache; // 类型在此完整，FGCObject析构必定注销全局引用器。
}

UDemoTalentDefinitionScope::UDemoTalentDefinitionScope() { DEMO_LOG_CALL(); }
UDemoTalentCatalog::UDemoTalentCatalog() { DEMO_LOG_CALL(); }
UDemoTalentCatalog::~UDemoTalentCatalog() { DEMO_LOG_CALL(); }
void UDemoTalentCatalog::BeginDestroy()
{
    DEMO_LOG_CALL();
    RuntimeCache.Reset(); // 先注销原生引用器，普通GC决定内容是否仍被ASC或其他使用者持有。
    Super::BeginDestroy();
}
FDemoTalentCatalogCache& UDemoTalentCatalog::GetRuntimeCache() const
{
    DEMO_LOG_TICK();
    check(IsInGameThread()); // 目录查询/软加载只在游戏线程；GC回调不调用此入口。
    if(!RuntimeCache)
    {
        RuntimeCache.Reset(new FDemoTalentCatalogCache()); // 自定义删除器兼容UHT生成代码中的不完整缓存类型。
        RuntimeCache->DefinitionsOuter=NewObject<UDemoTalentDefinitionScope>(GetTransientPackage(),NAME_None,RF_Transient); // 包体必须实例化具体UClass，不能直接创建抽象UObject基类。
        UE_LOG(LogFPSDemo,Log,TEXT("TALENT_CATALOG GC cache created owner=%s"),*GetNameSafe(this));
    }
    return *RuntimeCache;
}
const UDemoTalentDefinition* UDemoTalentCatalog::GetNativeDefinition(FName Id) const
{
    DEMO_LOG_CALL();
    FDemoTalentCatalogCache& Cache=GetRuntimeCache(); // 原生GC引用器持有，禁止回写CDO的强引用属性。
    if(Cache.NativeDefinitions.IsEmpty()) BuildDefaults(Cache.DefinitionsOuter,Cache.NativeDefinitions);
    for(const UDemoTalentDefinition* Definition:Cache.NativeDefinitions) if(Definition->TalentId==Id) return Definition; // 内容工具只读借用。
    UE_LOG(LogFPSDemo,Log,TEXT("TALENT_CATALOG unknown native id=%s"),*Id.ToString());
    return nullptr;
}
const TArray<TObjectPtr<UDemoTalentDefinition>>& UDemoTalentCatalog::Entries() const
{
    DEMO_LOG_TICK();
    FDemoTalentCatalogCache& Cache=GetRuntimeCache(); // CDO只持有原生缓存句柄；引用由FGCObject追踪。
    TArray<TObjectPtr<UDemoTalentDefinition>>& CachedEntries=Cache.CachedEntries; // 返回目录生命周期内的只读视图。
    if(!CachedEntries.IsEmpty()) return CachedEntries;
    if(Cache.NativeDefinitions.IsEmpty()) BuildDefaults(Cache.DefinitionsOuter,Cache.NativeDefinitions);
    for(UDemoTalentDefinition* Native:Cache.NativeDefinitions) // 内置顺序是旧存档/UI协议契约。
    {
        UDemoTalentDefinition* Selected=Native; // 资产无效时使用只读原生回退。
        if(const TSoftObjectPtr<UDemoTalentDefinition>* Override=DefinitionOverrides.Find(Native->TalentId))
        {
            UDemoTalentDefinition* Loaded=Override->LoadSynchronous(); // 仅首次访问，本地受信路径，不在命中时加载。
            if(Loaded&&Loaded->TalentId==Native->TalentId&&Loaded->TalentTag==Native->TalentTag&&Loaded->Validate()) Selected=Loaded;
            else UE_LOG(LogFPSDemo,Warning,TEXT("TALENT_CATALOG invalid definition override id=%s; native fallback"),*Native->TalentId.ToString());
        }
        CachedEntries.Add(Selected);
    }
    for(const TSoftObjectPtr<UDemoTalentDefinition>& Soft:AdditionalDefinitions) // 额外内容仍必须通过身份/类型边界。
    {
        UDemoTalentDefinition* Loaded=Soft.LoadSynchronous(); // 强引用由缓存持有。
        bool bDuplicate=false; // ID和权限Tag均唯一，防止A项授权B项。
        for(const UDemoTalentDefinition* Existing:CachedEntries)
            bDuplicate|=Loaded&&(Existing->TalentId==Loaded->TalentId||Existing->TalentTag==Loaded->TalentTag);
        if(!Loaded||!Loaded->Validate()||bDuplicate||CachedEntries.Num()>=64)
        { UE_LOG(LogFPSDemo,Warning,TEXT("TALENT_CATALOG rejected additional definition %s"),*Soft.ToString()); continue; }
        CachedEntries.Add(Loaded);
    }
    return CachedEntries;
}
const UDemoTalentDefinition* UDemoTalentCatalog::Find(FName Id) const
{
    DEMO_LOG_TICK();
    for(const UDemoTalentDefinition* Definition:Entries()) if(Definition->TalentId==Id) return Definition; // 有界只读查询。
    return nullptr;
}
FDemoTalentModifiers UDemoTalentCatalog::EvaluateProgress(const FDemoGambleProgress& Data) const
{
    DEMO_LOG_TICK();
    FDemoTalentModifiers Result; // 恒等初值，不污染永久属性或任何共享CDO。
    for(FName Id:Data.Talents) // 各定义组合自己的策略，目录没有天赋ID分派。
        if(const UDemoTalentDefinition* Definition=Find(Id)) Definition->BuildModifiers(Data.Rank(Id),Result);
    const float Values[]={Result.FireRate,Result.PelletDamage,Result.Spread,Result.RicochetRatio,Result.RicochetRadius,Result.SiphonRatio,Result.SiphonBudget,
        Result.ExplosionMultiplier,Result.FrozenMultiplier,Result.PierceBonus,Result.PierceScale,Result.DashFraction,Result.EvadeHeal,Result.ExecuteThreshold,Result.ExecuteMultiplier}; // 当前同步验证通道。
    for(float Value:Values) if(!FMath::IsFinite(Value)||Value<0.f)
    { UE_LOG(LogFPSDemo,Warning,TEXT("TALENT_MODIFIERS rejected nonfinite/negative strategy output")); return FDemoTalentModifiers(); }
    Result.Projectiles=FMath::Clamp(Result.Projectiles,1,2); Result.Spread=FMath::Min(Result.Spread,5.f);
    Result.PrecisionRounds=FMath::Max(0,Result.PrecisionRounds); Result.RicochetRadius=FMath::Min(Result.RicochetRadius,3000.f);
    Result.FireRate=FMath::Max(Result.FireRate,1.f); Result.PierceScale=FMath::Max(1.f,Result.PierceScale);
    Result.DashFraction=FMath::Min(1.f,Result.DashFraction); Result.ExecuteThreshold=FMath::Min(1.f,Result.ExecuteThreshold);
    return Result;
}
FDemoTalentModifiers UDemoTalentCatalog::Evaluate(const UAbilitySystemComponent* ASC) const
{
    DEMO_LOG_TICK();
    const ADemoPlayerState* PS=ASC?Cast<ADemoPlayerState>(ASC->GetOwnerActor()):nullptr; // 不跨玩家查询等级。
    if(!PS) return FDemoTalentModifiers();
    FDemoGambleProgress Authorized; // 权限GE与复制账本同时存在才参与快照。
    const FDemoGambleProgress& Data=PS->GetProgressComponent()->GetSnapshot().Gambling; // 同步借用，不跨帧缓存引用。
    for(FName Id:Data.Talents) if(const UDemoTalentDefinition* Definition=Find(Id))
        if(ASC->HasMatchingGameplayTag(Definition->TalentTag)) { Authorized.Talents.Add(Id); Authorized.TalentRanks.Add(Data.Rank(Id)); }
    return EvaluateProgress(Authorized);
}
