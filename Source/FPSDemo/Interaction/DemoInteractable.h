#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoInteractable.generated.h"

class ADemoCharacter;
class UStaticMeshComponent;
class UTextRenderComponent;
class UBoxComponent;

/** 当前备战区的升级终端或下一关入口；安全区及原地清关共用，进入战斗前成对销毁。 */
UCLASS()
class FPSDEMO_API ADemoInteractable : public AActor
{
	GENERATED_BODY()
public:
	/** 构建无需蓝图配置的圆柱终端和文字标签。 */
	ADemoInteractable();
	/** bShop=true 为金币商店、false 为关卡入口，仅生成时调用。 */
	void Configure(bool bShop);
	/** Player 为触发角色；验证当前注册对象、250cm 距离、备战阶段及菜单互斥，入口仅打开出发确认。 */
	void Interact(ADemoCharacter* Player);
	/** 获取可显示的交互提示，不返回可写内部状态。 */
	FString GetPrompt() const;
private:
	// 明确形状的根碰撞用于出生避障与玩家阻挡；半尺寸 60/60/80cm，随 Actor 销毁。
	UPROPERTY() TObjectPtr<UBoxComponent> Collision;
	// World 持有的可见圆柱组件；自身不碰撞，独立于根碰撞避免 FindTeleportSpot 无法调整网格根。
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
	// 场景内标识，避免仅靠 HUD 才能辨认终端。
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
	// 生成后保持不变的交互类型，仅权威单人使用。
	bool bIsShop = true;
};
