// 职责：Editor隔离战斗天赋回归。使用：-DemoCombatTalentTest经启动器生成，实际伤害/弹丸/GE时钟为断言，不访问云服务。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/DemoEnemyHitZones.h"
#include "Contracts/DemoGameplayContracts.h" // 先在安全区取得真实霰弹库存快照，再于隔离战斗测试恢复。
#include "DemoCombatTalentTest.generated.h"
/** 显式-DemoCombatTalentTest的Editor临时World回归；普通试玩和任何包体不启用测试逻辑。 */
UCLASS(Transient,NotBlueprintable)
class ADemoCombatTalentTest : public AActor
{
    GENERATED_BODY()
public:
    /** 无自动BeginPlay副作用，只有Bootstrap显式参数创建。 */
    ADemoCombatTalentTest();
    /** DeltaSeconds为World秒；状态机等待真实子弹运动和GAS到期。 */
    virtual void Tick(float DeltaSeconds) override;
private:
#if WITH_EDITOR
    /** Condition/Message是实际断言；失败退出隔离进程，成功继续下一步。 */
    bool Check(bool Condition,const TCHAR* Message);
    /** Delay为游戏秒等待，推进一次测试阶段。 */
    void Advance(float Delay);
    /** Player为当前Avatar，Distance为前方cm，Side为横向cm；返回World拥有的真实骨骼靶。 */
    class ADemoEnemy* SpawnTarget(class ADemoCharacter* Player,float Distance,float Side=0.f);
    /** Enemy为借用靶，Region为真实部位，OutHit返回正式PhysicsAsset查询；不伪造骨名。 */
    bool FindHit(class ADemoEnemy* Enemy,EDemoEnemyHitRegion Region,FHitResult& OutHit);
    /** Player为来源，返回测试Actor强持有的一枪快照，伤害按当前武器/天赋冻结。 */
    class UDemoShotContext* MakeShot(class ADemoCharacter* Player);
    /** Player为来源，沿真实首靶机身表面发射速度1000cm/s的独立弹，用于观察弹射时间。 */
    bool LaunchAtTarget(class ADemoCharacter* Player);
    /** Player为当前权威Avatar；验证独立GA、破绽伤害、传播/冰爆防递归并启动移速到期测试。 */
    bool CheckTalentSystem(class ADemoCharacter* Player);
    int32 Step=0; // 每次只推进一个阶段，首个断言失败停止。
    double Next=1.; // World秒截止，不使用阻塞Sleep。
    bool bFailed=false; // 同一进程仅记录首个失败。
    float ExpectedRearDamage=0.f; // 由后靶真实部位倍率计算的半伤期望HP。
    float RearBefore=0.f; // 两次弹射测试各自冻结后靶HP。
    float FrontBefore=0.f; // 检查先命中前靶、后靶必须等待实际到达。
    int32 DashAmmo=0; // 冲刺开始时原枪弹数，用于验证延迟/备用守恒。
#endif
    UPROPERTY() TObjectPtr<class UDemoShotContext> Shot; // 跨测试Tick保活当前枪快照，生产子弹也持强引用。
    UPROPERTY() FDemoLoadoutSnapshot ShotgunLoadout; // 本次Editor已真实解锁/装备的霰弹快照，不改生产权限。
    TWeakObjectPtr<class ADemoEnemy> Target; // World拥有的高HP真实机身/核心靶。
    TWeakObjectPtr<class ADemoEnemy> Rear; // 弹射的第二靶，侧向偏离原直线。
    TWeakObjectPtr<AActor> Wall; // 仅当前隔离World的阻挡夹具。
};
