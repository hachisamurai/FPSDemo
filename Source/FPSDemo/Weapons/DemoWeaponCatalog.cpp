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
    // 条件明确指完整十关；困难不会隐式代替普通或简单的记录。
    static const TCHAR* Texts[] = { TEXT("默认解锁 · 固定副武器"), TEXT("通关简单难度全部十关"), TEXT("通关普通难度全部十关"), TEXT("通关困难难度全部十关") };
    return Index >= 0 && Index < 4 ? Texts[Index] : TEXT("未知武器");
}
