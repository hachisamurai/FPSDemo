// 职责：PlayerState拥有的个人经济与权限账本。使用：权威Commit后向拥有者发布；依赖值契约和GAS，Editor可叠加本机临时模式权限。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Contracts/DemoGameplayContracts.h"
#include "DemoPlayerProgressComponent.generated.h"

// 本机值变化通知，服务器提交与拥有者 OnRep 共用；订阅者自行解绑。
DECLARE_MULTICAST_DELEGATE(FDemoPlayerProgressChanged);

/** 每名 PlayerState 唯一经济来源；服务器写入，私有值只复制给拥有者。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoPlayerProgressComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无 Tick，默认开启子对象复制，生命周期不绑定 Pawn。 */
    UDemoPlayerProgressComponent();
    /** OutLifetimeProps 为引擎复制注册表，只向拥有者发布个人钱包。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    /** 返回借用只读值；客户端展示不得直接提交修改后的快照。 */
    const FDemoPlayerProgressSnapshot& GetSnapshot() const;
    /** 拷贝可复制的本人账本，剔除私有雷位；菜单RPC和属性复制只能使用此接口。 */
    FDemoPlayerProgressSnapshot GetPublicSnapshot() const;
    /** Next为权威结果；单人Editor提交/读档时叠加当前GI模式权限。bNotify=false仅供同栈回滚，保存前禁止UI/RPC揭晓。 */
    bool Commit(const FDemoPlayerProgressSnapshot& Next, bool bNotify = true);
    /** 同步事务落盘成功后发布一次已提交结果；不能跨帧保留尚未发布的候选。 */
    void PublishCommitted();
    /** WeaponId为目录标识；查询本人永久来源，Editor另叠本机Standalone当前GI测试权限，不改账本或队友权限。 */
    bool IsWeaponUnlocked(FName WeaponId) const;
    /** 结束挑战时清银币和临时账本；永久钱包及解锁保留。 */
    void ResetTemporary();
    /** Actor 状态变化后供本地 UI 精确刷新，不需要 DrawHUD 读磁盘。 */
    FDemoPlayerProgressChanged OnChanged;
private:
    /** 拥有者收到快照后广播；只读展示不触发保存或交易。 */
    UFUNCTION() void OnRep_Snapshot();
    // 仅服务端 Commit 写入；COND_OwnerOnly 不向其他客户端暴露个人钱包。
    UPROPERTY() FDemoPlayerProgressSnapshot Snapshot; // 服务端完整账本仅本机保存，禁止注册复制或用于菜单RPC。
    UPROPERTY(ReplicatedUsing=OnRep_Snapshot) FDemoPlayerProgressSnapshot PublicSnapshot; // 显式脱敏的拥有者网络副本。
};
