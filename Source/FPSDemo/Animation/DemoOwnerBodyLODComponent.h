// 职责：本机身体分区与主Body完整骨骼LOD保护；兼容Leader或独立下身图，PrePhysics早于两者求值，不改动画/网格/全局画质。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoOwnerBodyLODComponent.generated.h"

class USkeletalMeshComponent;

/** 本Pawn局部渲染策略，不复制；只在本机当前全身视角覆盖ForcedLOD，退出恢复原组件值。 */
UCLASS(Transient, NotBlueprintable)
class FPSDEMO_API UDemoOwnerBodyLODComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 建立PrePhysics Tick；Body显式依赖本组件，保证先设LOD再构建RequiredBones。 */
    UDemoOwnerBodyLODComponent();
    /** Leader为真实动画Body、Follower为本人显示分区（可独立求值）；均借用同Pawn组件，在二者求值前建立LOD先决条件。 */
    void Initialize(USkeletalMeshComponent* Leader, USkeletalMeshComponent* Follower);
    /** 初始化或装备切换更换Follower资源后复核实际权重骨，仅读取渲染数据不改蒙皮。 */
    void ValidateCoverage();
    /** 在动画求值前处理本机控制/观战/死亡/画质变更；DeltaTime不用于累计动画。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    /** 显式停用时恢复进入前LOD；诊断进程据此隔离旧问题，普通游戏不调用。 */
    virtual void Deactivate() override;
    /** 卸载先恢复LOD并解除先决条件，不销毁真正的Body/Follower。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    /** 读取当前本机ViewTarget；仅当前Pawn可见的全身视角需要完整蒙皮骨。 */
    void UpdatePolicy();
    /** 幂等释放本组件覆盖；保存值属于进入覆盖时的组件，绝不强行设远端为LOD0。 */
    void RestoreLOD();
    TWeakObjectPtr<USkeletalMeshComponent> Body; // 真实动画来源；弱引用不延长Pawn生命。
    TWeakObjectPtr<USkeletalMeshComponent> OwnerBody; // 同Pawn显示分区，装备切换可换网格但组件不变。
    int32 SavedBodyLOD=0, SavedOwnerLOD=0; // 进入本机视角前的ForcedLOD原值；0自动，1表示LOD0。
    bool bOverriding=false; // 仅本组件已保存原值时为true，退出/停用/销毁恢复一次。
};
