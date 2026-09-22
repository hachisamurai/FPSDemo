#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoTerminalComponent.generated.h"
class ADemoInteractable;
class ADemoCharacter;
struct FDemoAreaSnapshot;

/** GameMode持有的终端生命周期/公共交互校验服务，不处理价格和阶段推进。 */
UCLASS(ClassGroup=(Demo))
class FPSDEMO_API UDemoTerminalComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无Tick，终端仅按战局阶段请求创建。 */
    UDemoTerminalComponent();
    /** Area是本次冻结区域快照；成对生成，任一失败撤销全部。 */
    bool CreateTerminals(const FDemoAreaSnapshot& Area);
    /** 先使交互版本失效再销毁Actor；重复清理安全。 */
    void ClearTerminals();
    /** 旅行/EndPlay时永久撤销本World服务，迟到创建请求不能重新开放交互。 */
    void Shutdown();
    /** 返回World拥有的升级/出发终端借用引用，不保留跨旅行对象。 */
    ADemoInteractable* GetShopTerminal() const;
    ADemoInteractable* GetNextLevelTerminal() const;
    /** 当前交互版本；终端重建/销毁递增，0仅为未创建的初值。 */
    uint64 GetGeneration() const;
    /** Player为当前玩家，Terminal必须是当前成对对象之一；ExpectedGeneration=0表示本次直接交互，否则验证打开时版本。 */
    FString ValidateInteraction(const ADemoCharacter* Player, const ADemoInteractable* Terminal, uint64 ExpectedGeneration = 0) const;
    /** EndPlayReason由World提供，独立撤销交互，不依赖RunFlow先停止。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY() TObjectPtr<ADemoInteractable> ShopTerminal; // World持有，只在当前备战阶段有效。
    UPROPERTY() TObjectPtr<ADemoInteractable> NextLevelTerminal; // 与升级终端成对创建/销毁。
    uint64 Generation = 0; // World内单调交互版本，不进入存档或复制。
    bool bStopped = false; // 只在旅行或卸载置位；普通关间清理仍可再次创建。
};
