// 职责：持有玩家ASC、属性、经济、天赋与连接语音组件。用法：Pawn重生绑定此PS作为Owner；语音不写进度，依赖GAS/Voice而非共享GameState经济。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "DemoPlayerState.generated.h"

class UDemoAbilitySystemComponent;
class UDemoAttributeSet;

/** 参考 Lyra：玩家 ASC/属性由 PlayerState 拥有，Pawn 仅作为当前 Avatar。 */
UCLASS()
class FPSDEMO_API ADemoPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	/** 创建 ASC 与属性默认子对象，生命周期与本局 PlayerState 一致。 */
	ADemoPlayerState();
	/** GAS 标准接口，返回此 PlayerState 持有的 ASC。 */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	/** 只读引用入口，调用者不接管所有权。 */
	UDemoAbilitySystemComponent* GetDemoASC() const;
	/** 属性对象由 PlayerState 保活，调用者不得手动销毁。 */
	const UDemoAttributeSet* GetAttributes() const;
	/** 本人唯一钱包/成长/解锁容器，客户端仅有拥有者复制视图。 */
	class UDemoPlayerProgressComponent* GetProgressComponent() const;
	/** 本人天赋句柄管理器；服务器提交账本时增量同步，客户端只读复制的GAS/账本。 */
	class UDemoTalentComponent* GetTalentComponent() const;
    /** 核心/分支组件，跨Pawn重生保留，只有整轮结束清除。 */
    class UDemoRunBuildComponent* GetRunBuildComponent() const;
    /** 最近一次结束前冻结的个人战报；服务端只通过本人菜单视图公开，不参与经济/保存。 */
    class UDemoRunSummaryComponent* GetRunSummaryComponent() const;
	/** 连接拥有的语音组件；输入/HUD/嘴型只借用，重生不会打开或重置其他玩家麦克风。 */
	class UDemoVoiceComponent* GetVoiceComponent() const;
private:
    UPROPERTY(VisibleAnywhere, Category="Demo|Summary") TObjectPtr<class UDemoRunSummaryComponent> RunSummary; // 随PS保活，无Tick，无公共复制，不随Pawn死亡清空。
    UPROPERTY(VisibleAnywhere, Category="Demo|Build") TObjectPtr<class UDemoRunBuildComponent> RunBuild; // PS拥有，公开核心与OwnerOnly细节分离。
	// 同PS创建/销毁的复制子对象，服务器中转本人语音，客户端保存短暂音频与嘴型，不保存录音。
	UPROPERTY(VisibleAnywhere, Category="Demo|Voice") TObjectPtr<class UDemoVoiceComponent> Voice;
	/** 随PS保活，不随Avatar重建；独占本人的天赋授予句柄。 */
	UPROPERTY(VisibleAnywhere,Category="Demo|Talents") TObjectPtr<class UDemoTalentComponent> Talents;
	// 默认子对象随 PlayerState 保活，Avatar 更换不清空个人经济。
	UPROPERTY(VisibleAnywhere, Category="Demo|Progress") TObjectPtr<class UDemoPlayerProgressComponent> Progress;
	// 玩家持久能力容器，复制模式 Mixed；Avatar 可重新绑定。
	UPROPERTY(VisibleAnywhere, Category="Demo|GAS")
	TObjectPtr<UDemoAbilitySystemComponent> AbilitySystem;
	// 复制属性集合，作为默认子对象被 ASC 自动注册。
	UPROPERTY()
	TObjectPtr<UDemoAttributeSet> Attributes;
};
