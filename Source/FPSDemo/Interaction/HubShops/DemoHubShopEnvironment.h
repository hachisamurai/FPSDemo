// 职责：持久Hub环境的可信身份/成员闭包与本World替换门；只选环境，不生成Gameplay、不解析外部JSON。
// 使用：TA在正式Map放独立EnvironmentRoot并填Entries/专用Tag；Blockout重建调用ResolveForWorld。
// 依赖项目Definition、UE普通StaticMeshActor/PointLight/RectLight；无网络/存档字段或第三方依赖。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "DemoHubShopEnvironment.generated.h"

class UDemoHubShopDefinition;
class USceneComponent;

/** 一条持久环境身份，引用同PersistentLevel的普通环境Actor；数组顺序无语义。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoHubShopEnvironmentEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub Environment") FName EntryId; // 同一根内唯一稳定ID；不使用Label/自动对象名。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub Environment") TObjectPtr<AActor> Actor; // 地图硬引用，专用Member Tag+直接挂EnvironmentRoot双归属；仅普通Mesh/Point/Rect，不跨World/Level。
};

/** 专用环境成员的本World抑制快照。运行/Editor失败回退可逆，不序列化为新的作者默认值。 */
UCLASS(Transient)
class FPSDEMO_API UDemoHubShopEnvironmentVisibilityState : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 解析阶段同步调用；每个抑制周期首次捕获原状态、幂等抑制、恢复后清快照；不能保存回退态地图。 */
    void SetSuppressed(bool bInSuppressed);
private:
    bool bCaptured=false; // 本次抑制周期首次捕获，恢复后清空；不存档/复制。
    bool bSuppressed=false; // 幂等状态，避免重复重建刷新/覆盖原始快照。
    bool bWasHidden=false; // Actor原HiddenInGame；不读取/修改Editor选择隐藏标记。
    bool bHadCollision=false; // Actor原全局碰撞门；各Primitive碰撞Profile/响应完全保持。
    TArray<TPair<TWeakObjectPtr<USceneComponent>,bool>> ComponentVisibility; // 原组件可见性含局部灯，弱引用防成员删除后悬挂。
};

/** TA作者的独立环境根，不是Blockout，不能替代其Root/WorldEpoch/16锚点/三枪七机。 */
UCLASS(BlueprintType)
class FPSDEMO_API ADemoHubShopEnvironmentRoot : public AActor
{
    GENERATED_BODY()
public:
    /** 创建无碰撞普通根，默认专用Root Tag；静态地图Actor不复制，所有端各自读相同闭包。 */
    ADemoHubShopEnvironmentRoot();
    /** 校验当前根与DA、Hub实际框及本PersistentLevel成员/资源，不修改Actor或材质；失败给明确原因。 */
    UFUNCTION(BlueprintCallable, Category="Hub Environment") bool ValidateEnvironment(const UDemoHubShopDefinition* Definition,const FTransform& HubFrame,FString& Error) const;
    /** Blockout重建的唯一替换门：完整唯一根才启用；其余先抑制专用环境再允许旧白模。无Tick/加载器。 */
    static bool ResolveForWorld(UWorld* World,const UDemoHubShopDefinition* Definition,const FTransform& HubFrame,FString& Error);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub Environment") FName EnvironmentId; // 与可信DA精确相同；不能用ActorLabel匹配。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub Environment") int32 Revision=0; // 1起，同版闭包变更时递增并同步DA；与网络/磁盘版本无关。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hub Environment") TArray<FDemoHubShopEnvironmentEntry> Entries; // 完整动态数量闭包，和DA ItemCount一致，不固定350。
};
