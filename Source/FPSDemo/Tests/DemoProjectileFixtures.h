#pragma once
#include "CoreMinimal.h"
#include "Combat/DemoEnemyHitZones.h"

class ADemoCharacter;
class ADemoEnemy;
class USkeletalMeshComponent;

/** 真实骨骼靶工具；实现在HitZone专项文件中，实体弹专项复用相同生产PhysicsAsset探针。 */
namespace DemoHitZoneTest
{
    /** Enemy由World拥有；Action为序列路径，Seconds为固定采样秒数，返回借用网格。 */
    USkeletalMeshComponent* Pose(ADemoEnemy* Enemy,const FString& Action,float Seconds);
    /** Mesh/Profile为只读生产资产；Wanted是区域，OutPoint接收有2cm邻域余量的模型空间点。 */
    bool FindRegion(USkeletalMeshComponent* Mesh,const UDemoEnemyHitProfile* Profile,EDemoEnemyHitRegion Wanted,FVector& OutPoint);
    /** Enemy/Player为World对象；Point为模型空间探针，Distance为相机到表面距离cm。 */
    void Aim(ADemoEnemy* Enemy,ADemoCharacter* Player,const FVector& Point,float Distance);
    /** Player为测试Avatar，Id为normal/fire/frost/piercing，只改本次实例。 */
    void Ammo(ADemoCharacter* Player,FName Id);
    /** Player使用当前真实装备通过Fire GA开火一次，调用方负责等待成本/命中时序。 */
    void Fire(ADemoCharacter* Player);
}

/** 测试进程专有工具；不改变生产权限，不创建供正常游戏调用的调试接口。 */
namespace DemoProjectileFixtures
{
    /** World为当前测试世界；只统计有效的实体玩家子弹，不延长Actor生命周期。 */
    int32 CountLive(UWorld* World);
    /** Player为当前装备Avatar；bPending/Deadline由测试Actor保存，首次开火后逐帧等待，超时退出失败。 */
    bool FireAndWait(ADemoCharacter* Player,bool& bPending,float& Deadline);
    /** Player为隔离档Avatar；Index为0步枪/1散弹/2狙击，真实解锁及终端选择后恢复测试位置/阶段。 */
    bool EquipPrimary(ADemoCharacter* Player,int32 Index);
}

namespace DemoAmmoSnapshotTests
{
    /** World/Player为当前隔离测试上下文；同步验证首层参数冻结、火冰隔离与阈值1重入，结束清理自有靶。 */
    bool Run(UWorld* World,ADemoCharacter* Player);
}
