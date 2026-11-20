// 职责：解析只读武器目录并提供稳定ID查询。依赖Settings的原生GC引用器保活软加载资产，不向CDO反射属性写入运行时对象。
#include "Weapons/DemoWeaponCatalog.h"
#include "Debug/DemoLog.h"

const UDemoWeaponDefinitionCatalog* DemoWeaponCatalog::GetCatalog()
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    UDemoWeaponDefinitionSettings* Settings = GetMutableDefault<UDemoWeaponDefinitionSettings>(); // CDO只保存配置与原生缓存；加载资产由FGCObject引用器保活。
    if (!Settings->bCatalogResolved)
    {
        Settings->bCatalogResolved = true; // 加载前置位，避免资源构造期间递归解析项目配置。
        if (!Settings->CatalogAsset.IsNull())
        {
            UDemoWeaponDefinitionCatalog* Loaded = Settings->CatalogAsset.LoadSynchronous(); // 首次目录访问时加载，不生成任何武器实例。
            FString Error; // 非法自定义目录明确日志并使用兼容默认，绝不引入第二套数值。
            if (Loaded && Loaded->Validate(Error)) Settings->LoadedCatalog.Reset(Loaded);
            else UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_CATALOG invalid asset=%s error=%s; using native definitions"), *Settings->CatalogAsset.ToString(), *Error);
        }
    }
    return Settings->LoadedCatalog.IsValid() ? Settings->LoadedCatalog.Get() : GetDefault<UDemoWeaponDefinitionCatalog>();
}
const FDemoWeaponDefinition* DemoWeaponCatalog::FindDefinition(FName WeaponId)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    return GetCatalog()->FindDefinition(WeaponId);
}
int32 DemoWeaponCatalog::Num()
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    return GetCatalog()->Definitions.Num();
}
FName DemoWeaponCatalog::IdAt(int32 Index)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    const TArray<FDemoWeaponDefinition>& Definitions = GetCatalog()->Definitions; // 仅兼容UI索引；实例射击不使用此转换。
    return Definitions.IsValidIndex(Index) ? Definitions[Index].WeaponId : NAME_None;
}
int32 DemoWeaponCatalog::IndexOf(FName Id)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    const TArray<FDemoWeaponDefinition>& Definitions = GetCatalog()->Definitions; // 稳定ID对应项目目录中的展示位置。
    for (int32 Index = 0; Index < Definitions.Num(); ++Index) // 原四项顺序不变，新增定义无需修改本函数。
        if (Definitions[Index].WeaponId == Id) return Index;
    return INDEX_NONE;
}
FString DemoWeaponCatalog::UnlockText(int32 Index)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    const TArray<FDemoWeaponDefinition>& Definitions = GetCatalog()->Definitions; // 中文说明只用于显示，不能作为服务器授权依据。
    if (!Definitions.IsValidIndex(Index)) return TEXT("未知武器");
    return Index == 0 ? TEXT("固定携带副武器") : TEXT("未拥有 · 前往安全区枪墙购买"); // 已取消通关条件解锁，主武器仅通过购买获取。
}
bool DemoWeaponCatalog::AppendUnlocksForClear(const FString& DifficultyId, TArray<FName>& OutIds)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs difficulty=%s (purchase-only)"), __FUNCTION__, *DifficultyId);
    const FName Fact(*DifficultyId); // 仅校验通关事实合法性；按最新规则主武器不再随通关自动解锁，只通过安全区购买获取。
    if (Fact != TEXT("easy") && Fact != TEXT("normal") && Fact != TEXT("hard") && Fact != TEXT("hard_pistol") && Fact != TEXT("hell"))
    { UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_UNLOCK rejected unknown completion fact")); return false; }
    return true;
}
