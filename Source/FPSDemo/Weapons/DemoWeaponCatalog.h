#pragma once
#include "CoreMinimal.h"
#include "Weapons/DemoWeaponDefinition.h"
class ADemoWeaponBase;

/** 项目唯一身份目录；旧四项顺序只兼容UI索引，新增项追加，存档/服务器始终交换稳定ID。 */
namespace DemoWeaponCatalog
{
    /** 返回项目已验证目录；可选配置失效时回退原生四枪CDO，资源由设置CDO强持有。 */
    const UDemoWeaponDefinitionCatalog* GetCatalog();
    /** WeaponId为稳定键；借用只读项，不从当前装备槽或Actor类名推导身份。 */
    const FDemoWeaponDefinition* FindDefinition(FName WeaponId);
    /** 目录项总数，兼容旧索引API的消费者可据此扩展到新增主武器。 */
    int32 Num();
    /** Index=0手枪/1步枪/2散弹/3狙击；非法返回None。 */
    FName IdAt(int32 Index);
    /** Id为持久化键；未知返回INDEX_NONE。 */
    int32 IndexOf(FName Id);
    /** Index同目录；返回中文解锁条件，非法返回错误提示。 */
    FString UnlockText(int32 Index);
    /** DifficultyId为真实easy/normal/hard/hard_pistol/hell事实；向OutIds追加派生权限但不补造较低难度记录，未知返回false。 */
    bool AppendUnlocksForClear(const FString& DifficultyId, TArray<FName>& OutIds);
}
