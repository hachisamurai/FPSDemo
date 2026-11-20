#include "Weapons/DemoWeaponDefinition.h"
#include "Weapons/DemoWeaponBase.h"
#include "Debug/DemoLog.h"

bool FDemoWeaponDefinition::Validate(FString& Error) const
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs id=%s"), __FUNCTION__, *WeaponId.ToString());
    Error.Reset();
    if (WeaponId.IsNone()) Error = TEXT("Weapon definition requires stable WeaponId");
    else if (AllowedSlot != 1 && AllowedSlot != 2) Error = TEXT("Weapon definition AllowedSlot must be 1 or 2");
    else if (WeaponClass.IsNull()) Error = TEXT("Weapon definition requires actor class");
    for (const FName Fact : CompletionFacts) // 配置不能引入未知通关事实从而误授予权限。
        if (Fact != TEXT("easy") && Fact != TEXT("normal") && Fact != TEXT("hard") && Fact != TEXT("hard_pistol") && Fact != TEXT("hell")) Error = TEXT("Weapon definition contains unknown completion fact");
    return Error.IsEmpty();
}

UDemoWeaponDefinitionCatalog::UDemoWeaponDefinitionCatalog()
{
    DEMO_LOG_CALL();
    // 仅默认目录定义资源路径；装备组件的旧Class数组继续作为既有BP兼容覆盖。
    const FName Ids[] = { TEXT("pistol"), TEXT("rifle"), TEXT("shotgun"), TEXT("sniper") }; // 历史协议键，不随UI文案变化。
    const TCHAR* Classes[] = { TEXT("Pistol"), TEXT("Rifle"), TEXT("Shotgun"), TEXT("Sniper") }; // 既有Blueprint资源后缀。
    const TCHAR* Descriptions[] = { TEXT("默认解锁 · 固定副武器"), TEXT("通关简单难度全部十关"), TEXT("通关普通或更高难度全部十关"), TEXT("通关困难或地狱难度全部十关") }; // 只读默认展示。
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ids); ++Index) // 构造纯定义，不产生World或玩家状态。
    {
        FDemoWeaponDefinition Definition; // 本次值随后归目录数组持有。
        Definition.WeaponId = Ids[Index];
        Definition.WeaponClass = TSoftClassPtr<ADemoWeaponBase>(FSoftObjectPath(FString::Printf(TEXT("/Game/Weapons/Blueprints/BP_Weapon_%s.BP_Weapon_%s_C"), Classes[Index], Classes[Index])));
        Definition.AllowedSlot = Index == 0 ? 2 : 1;
        Definition.bUnlockedByDefault = Index == 0;
        Definition.UnlockDescription = FText::FromString(Descriptions[Index]);
        if (Index == 1) Definition.CompletionFacts = { TEXT("easy") };
        else if (Index == 2) Definition.CompletionFacts = { TEXT("normal"), TEXT("hard"), TEXT("hard_pistol"), TEXT("hell") };
        else if (Index == 3) Definition.CompletionFacts = { TEXT("hard"), TEXT("hard_pistol"), TEXT("hell") };
        Definitions.Add(MoveTemp(Definition));
    }
}

bool UDemoWeaponDefinitionCatalog::Validate(FString& Error) const
{
    DEMO_LOG_CALL();
    Error.Reset();
    const FName LegacyIds[] = { TEXT("pistol"), TEXT("rifle"), TEXT("shotgun"), TEXT("sniper") }; // 旧Index协议只作迁移边界，不参与实例射击身份推导。
    if (Definitions.Num() < UE_ARRAY_COUNT(LegacyIds)) { Error = TEXT("Weapon catalog must preserve four legacy definitions"); return false; }
    TSet<FName> Seen; // 本次目录验证去重集合，不持久化。
    for (int32 Index = 0; Index < Definitions.Num(); ++Index) // 新项允许追加主槽，旧四项身份/顺序/槽不可重排。
    {
        const FDemoWeaponDefinition& Definition = Definitions[Index]; // 目录拥有的只读项。
        if (!Definition.Validate(Error) || Seen.Contains(Definition.WeaponId)) { if (Error.IsEmpty()) Error = TEXT("Duplicate weapon definition ID"); return false; }
        if ((Index < UE_ARRAY_COUNT(LegacyIds) && Definition.WeaponId != LegacyIds[Index]) || Definition.AllowedSlot != (Index == 0 ? 2 : 1))
        { Error = TEXT("Weapon catalog must preserve legacy ordering and two-slot contract"); return false; }
        Seen.Add(Definition.WeaponId);
    }
    return true;
}

const FDemoWeaponDefinition* UDemoWeaponDefinitionCatalog::FindDefinition(FName WeaponId) const
{
    DEMO_LOG_TICK();
    for (const FDemoWeaponDefinition& Definition : Definitions) // 目录数量小，查询只返回借用定义，不加载类。
        if (Definition.WeaponId == WeaponId) return &Definition;
    return nullptr;
}
