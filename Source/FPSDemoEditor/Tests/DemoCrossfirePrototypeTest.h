// 职责：Editor独立交叉火力战斗边界夹具。使用：专用启动旗标+控制台命令创建，不参与正式核心、存档或包体。
// 依赖：真实游戏World/Flow会话、原PS ASC、Enemy Health GE及原武器发射事务；不直接广播原型桥或伪造消费结果。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoCrossfirePrototypeTest.generated.h"
class ADemoCharacter;
class ADemoEnemy;
class UDemoShotContext;
class UAbilitySystemComponent;
struct FOnAttributeChangeData;
struct FHitResult;

/** 仅FPSDemoEditor模块中的显式隔离进程测试；普通试玩/非Editor无创建入口或类型。 */
UCLASS(Transient,NotBlueprintable)
class ADemoCrossfirePrototypeTest : public AActor
{
    GENERATED_BODY()
public:
    /** 设置有界状态机Tick；构造本身不授予能力、存档或创建靶子。 */
    ADemoCrossfirePrototypeTest();
    /** DeltaSeconds为游戏帧秒；等待正式资源握手与3秒窗口，不用阻塞Sleep。 */
    virtual void Tick(float DeltaSeconds) override;
    /** EndPlay清理本测试属性回调；候选由原会话/World生命周期清理，不影响其它GA、玩家或资产。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** Condition/Message为独立实际结果断言；首次失败立即以非零状态请求结束隔离进程。 */
    bool Check(bool Condition,const TCHAR* Message);
    /** Delay为游戏秒；推进状态后按World时间等待，真实总超时独立计时。 */
    void Advance(float Delay=.28f);
    /** 创建高HP真实Enemy/PhysicsAsset靶；不登记生成器，死亡不会人为完成整关。 */
    ADemoEnemy* SpawnTarget(float Health=5000.f);
    /** Enemy为当前世界靶；通过真实骨骼刚体射线查机身1倍区域，OutHit不预填骨名。 */
    bool BodyHit(ADemoEnemy* Enemy,FHitResult& OutHit) const;
    /** Player为原Avatar；原枪完整准备/扣弹/冷却/发射后借Context，立即取消运输供边界时序重现。 */
    UDemoShotContext* FireShot(ADemoCharacter* Player);
    /** Enemy为同会话目标；经Health GE真实扣血，再按生产近战确认接口通知，不生成虚假伤害/消费回执。 */
    bool ConfirmMelee(ADemoCharacter* Player,ADemoEnemy* Enemy,float RawDamage=20.f);
    /** Shot必须来自成功的真实发射事务；调用生产ProjectileDamage和实际PhysicsAsset命中，比较实际HP损失。 */
    bool Direct(UDemoShotContext* Shot,ADemoEnemy* Enemy,float ExpectedLoss,int32 Pellet=0,bool Secondary=false);
    /** 仅本用例在原枪实际扣血回调后加生产免伤Tag，验证额外GE被免疫时不冒称冲击成功。 */
    void ImmunizeAfterDamage(const FOnAttributeChangeData& Data);
    int32 Step=0; // 独立状态机，不改变生产关号或核心目录。
    int32 Passed=0; // 本次进程实际通过断言数，最终成功汇总才代表跑完。
    int32 SpawnCount=0; // 靶子分隔编号，防真实射线被其它用例靶遮住。
    double NextTime=1.; // World秒等待截止，暂停不会偷跑充能窗口。
    double StartedAt=0.; // 单调实际秒，防资源或菜单永久等待。
    bool Failed=false; // 首次失败后不继续写状态。
    bool ImmunizeOnNextDamage=false; // 仅一次实际原枪扣血后启用免伤，验证原枪与冲击不同结果。
    FDelegateHandle ImmunityBinding; // 本测试精确绑定，目标销毁或退出前移除。
    TWeakObjectPtr<UAbilitySystemComponent> ImmunityASC; // 只弱借用本用例目标ASC，不延长Enemy寿命。
    TWeakObjectPtr<ADemoEnemy> Target; // World拥有的主要真实靶。
    TWeakObjectPtr<ADemoEnemy> Other; // 异目标不会消费破势凭据。
    UPROPERTY() TObjectPtr<UDemoShotContext> Shot; // 取消实体弹运输后保活真实发射上下文，绝不另造ShotId。
    UPROPERTY() TObjectPtr<UDemoShotContext> OldShot; // 近战之前的真实发射，必须被旧弹门禁拒绝额外加成。
};
