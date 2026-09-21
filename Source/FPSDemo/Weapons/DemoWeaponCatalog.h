#pragma once
#include "CoreMinimal.h"
class ADemoWeaponBase;

/** 固定目录顺序只用于UI和图标；存档/服务器交换稳定ID，不交换Actor或数组下标。 */
namespace DemoWeaponCatalog
{
    /** Index=0手枪/1步枪/2散弹/3狙击；非法返回None。 */
    FName IdAt(int32 Index);
    /** Id为持久化键；未知返回INDEX_NONE。 */
    int32 IndexOf(FName Id);
    /** Index同目录；返回中文解锁条件，非法返回错误提示。 */
    FString UnlockText(int32 Index);
    /** DifficultyId为真实easy/normal/hard/hard_pistol/hell事实；向OutIds追加派生权限但不补造较低难度记录，未知返回false。 */
    bool AppendUnlocksForClear(const FString& DifficultyId, TArray<FName>& OutIds);
}
