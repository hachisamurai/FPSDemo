#include "Weapons/DemoWeaponCatalog.h"
#include "Debug/DemoLog.h"
FName DemoWeaponCatalog::IdAt(int32 Index)
{
    // 命名空间函数无UObject this，使用与DEMO_LOG_TICK同级的完整入口日志。
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[WeaponCatalog] %hs"), __FUNCTION__);
    // 稳定协议键，变更名称需迁移旧存档；不能随中文展示文案调整。
    static const FName Ids[] = { TEXT("pistol"), TEXT("rifle"), TEXT("shotgun"), TEXT("sniper") };
    return Index >= 0 && Index < 4 ? Ids[Index] : NAME_None;
}
int32 DemoWeaponCatalog::IndexOf(FName Id)
{
    // 命名空间函数无UObject this，使用与DEMO_LOG_TICK同级的完整入口日志。
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[WeaponCatalog] %hs"), __FUNCTION__);
    for (int32 Index = 0; Index < 4; ++Index) // 固定四项，不分配临时目录。
        if (IdAt(Index) == Id) return Index;
    return INDEX_NONE;
}
FString DemoWeaponCatalog::UnlockText(int32 Index)
{
    // 命名空间函数无UObject this，使用与DEMO_LOG_TICK同级的完整入口日志。
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[WeaponCatalog] %hs"), __FUNCTION__);
    // 散弹枪接受普通及更高难度的真实通关事实；武器权限继承不等于额外通关记录或金币奖励。
    static const TCHAR* Texts[] = { TEXT("默认解锁 · 固定副武器"), TEXT("通关简单难度全部十关"), TEXT("通关普通或更高难度全部十关"), TEXT("通关困难或地狱难度全部十关") };
    return Index >= 0 && Index < 4 ? Texts[Index] : TEXT("未知武器");
}
bool DemoWeaponCatalog::AppendUnlocksForClear(const FString& DifficultyId, TArray<FName>& OutIds)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[WeaponCatalog] %hs difficulty=%s"), __FUNCTION__, *DifficultyId);
    if (DifficultyId == TEXT("easy")) OutIds.AddUnique(TEXT("rifle")); // 步枪仍要求简单通关，此次只扩展散弹枪条件。
    else if (DifficultyId == TEXT("normal")) OutIds.AddUnique(TEXT("shotgun"));
    else if (DifficultyId == TEXT("hard") || DifficultyId == TEXT("hard_pistol") || DifficultyId == TEXT("hell"))
    {
        OutIds.AddUnique(TEXT("shotgun"));
        OutIds.AddUnique(TEXT("sniper"));
    }
    else { UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_UNLOCK rejected unknown completion fact")); return false; }
    return true;
}
