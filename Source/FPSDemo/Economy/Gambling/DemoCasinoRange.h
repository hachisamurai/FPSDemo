// 职责：个人赏金靶场的服务器射线判定、计时和保存。使用：Casino已保存开局后Spawn；仅拥有者相关，不改变Combat伤害准入。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoCasinoRange.generated.h"
class ADemoCharacter;
class UStaticMeshComponent;
UCLASS()
class FPSDEMO_API ADemoCasinoRange : public AActor
{
    GENERATED_BODY()
public:
    /** 构造两块训练靶与复制参数；不读取世界或玩家账本。 */
    ADemoCasinoRange();
    /** Authority仅创建/恢复本人已保存的靶场；已有实例复用，失败返回false。 */
    static bool Start(ADemoCharacter* Player);
    /** 查找本World该Controller拥有的运行实例，不接触Player0。 */
    static ADemoCasinoRange* Find(const ADemoCharacter* Player);
    /** GAS服务器射击入口；检查冷却/时限，射线命中才加分，先保存再移动靶。 */
    void Fire();
    /** 当前服务器复制的剩余预算/成绩供HUD读取；不作为客户端提交输入。 */
    UPROPERTY(Replicated) int64 DeadlineMs=0; // UTC截止；本地仅作显示。
    UPROPERTY(Replicated) int32 Score=0; // 身体1/头部2，0..20。
    UPROPERTY(Replicated) int32 ShotsLeft=0; // 服务端已保存剩余训练枪数。
    UPROPERTY(Replicated) int32 Hits=0; // 已击倒靶数0..10。
    UPROPERTY(Replicated) FString Feedback; // 已保存命中/脱靶/结算反馈，供本地HUD显示。
protected:
    /** 暂停仍逐帧获回调，内部每0.1真实秒检查时限/退场；不能用随World暂停的TickInterval。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 销毁时回收场地；仅仍在同Hub且持有入场Avatar时恢复位置，旅行不拉回旧出生点。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** 注册拥有者相关Actor的只读显示字段。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
    /** 创建隔离房间、记录入场点后传送；失败不修改已保存下注，可从终端重试。 */
    bool EnterRoom();
    /** 结算成功后或同Hub主动结束时恢复位置/朝向；Return标记确保最多执行一次。 */
    void ReturnFromRoom();
    /** 以当前保存的Step及Seed安排下一靶位；不随机重排、不改变玩家摄像机。 */
    void RefreshTarget();
    /** 原子保存当前成绩对应奖金；失败保留局面并稍后重试，不重复发奖。 */
    void Finish();
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Body; // 训练躯干仅查询碰撞，不承受GAS伤害。
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Head; // 头部独立碰撞用于精准射击。
    UPROPERTY() TWeakObjectPtr<ADemoCharacter> Shooter; // 权威Avatar，旅行/死亡不保留旧指针。
    UPROPERTY() TObjectPtr<class ADemoRangeRoom> Room; // 权威创建的专属房间；Range负责销毁，不持久化。
    FTransform ReturnTransform; // 入场Avatar原世界位置/旋转，不保存到检查点。
    FRotator ReturnAim; // 入场控制器视角，成功返回时恢复。
    bool bEnteredRoom=false; // 仅成功传送后为true，防止生成失败覆盖出生点。
    FTransform Anchor; // 独立房间入口视角基准，目标布局围绕其前方。
    double NextShotAt=0; // 权威单调时钟0.15秒射击间隔，客户端不能改。
    double RetireAt=0; // 结算后保留反馈3秒；0表示仍活动。
    double NextAuthorityCheck=0; // FPlatformTime秒，检查节流独立于World暂停；不保存/复制。
};
