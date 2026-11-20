// 职责：GameMode终端成对生命周期/会话；清关复活前捕获服务器生存者，生成时交独立物理摆放助手。
// 使用：Flow在Refill前CaptureVictoryPlacementOrigin、原CreateTerminals后成对复制；Hub/读档仍用作者锚点，无Tick。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/DemoTerminalPlacement.h"
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
    /** 首次在复活前记录胜利生存者。bRetryAfterClear 仅供已清关且终端仍缺失时，重采当前存活队员位置。 */
    void CaptureVictoryPlacementOrigin(bool bRetryAfterClear=false);
    /** Area是本次冻结区域；2026-11-20起局内外统一生成Hub外观类（升级石碑/出发门），bHubAppearance只保留给调用方记录物理区域，不再切换模型。清关成对位置带朝向，操作面转向清关位置。 */
    bool CreateTerminals(const FDemoAreaSnapshot& Area, bool bHubAppearance=false);
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
    /** Player/Terminal 经服务器验证后登记交互；每名玩家独立，不依赖客户端菜单字段。 */
    bool BeginInteraction(ADemoCharacter* Player, ADemoInteractable* Terminal);
    /** Player 关闭页面、死亡或离开时撤销会话，重复调用安全。 */
    void EndInteraction(ADemoCharacter* Player);
    /** Player 为 RPC 拥有者的 Pawn，bShop 指定操作类型；重新验证代数/距离/生命。 */
    FString ValidateSession(const ADemoCharacter* Player, bool bShop) const;
    /** EndPlayReason由World提供，独立撤销交互，不依赖RunFlow先停止。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    // 会话只存在服务器当前 World；弱 Pawn 防止重生/断线后旧请求被新 Avatar 复用。
    TMap<TWeakObjectPtr<ADemoCharacter>, TPair<TWeakObjectPtr<ADemoInteractable>, uint64>> Sessions;
    UPROPERTY() TObjectPtr<ADemoInteractable> ShopTerminal; // World持有，只在当前备战阶段有效。
    UPROPERTY() TObjectPtr<ADemoInteractable> NextLevelTerminal; // 与升级终端成对创建/销毁。
    uint64 Generation = 0; // World内单调交互版本，不进入存档或复制。
    bool bStopped = false; // 只在旅行或卸载置位；普通关间清理仍可再次创建。
    TArray<FDemoTerminalPlacementOrigin> VictoryOrigins; // 最多四个复活前服务器纯值，下一次创建/清理即消费，不复制/存档。
    int32 VictoryLevel=INDEX_NONE; // 捕获时真实关号，禁止上一关快照复用。
    uint32 VictoryPhaseRevision=0; // 捕获时Combat阶段版本，防迟到创建消费旧战斗位置。
    bool bVictoryCaptured=false; // 区分没有清关快照与已捕获但无生存者；后者不能降级旧中心。
};
