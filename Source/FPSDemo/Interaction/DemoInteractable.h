// 职责：服务器生成的备战终端及本地外观。交互只提交意图，权限属于 TerminalComponent。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoInteractable.generated.h"

class ADemoCharacter;
class UStaticMeshComponent;
class UTextRenderComponent;
class UBoxComponent;
class UStaticMesh;

/** 当前备战区的升级终端或下一关入口；安全区及原地清关共用，进入战斗前成对销毁。 */
UCLASS()
class FPSDEMO_API ADemoInteractable : public AActor
{
	GENERATED_BODY()
public:
	/** 创建共用的交互盒、无碰撞显示组件和编辑器标牌；普通战区使用基础圆柱。 */
	ADemoInteractable();
	/** OutLifetimeProps 只同步终端种类和Actor位置，不复制服务器交互权限。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 服务器生成时设置 bShop（升级或出发），并立即更新本地外观。 */
	void Configure(bool bShop);
	/** Player 为触发角色；验证当前注册对象、250cm 距离、备战阶段及菜单互斥，入口仅打开出发确认。 */
	void Interact(ADemoCharacter* Player);
	/** 获取可显示的交互提示，不返回可写内部状态。 */
	FString GetPrompt() const;
protected:
	/** 初始复制完成后也刷新默认升级类型；初值与CDO相同时可能没有RepNotify。 */
	virtual void BeginPlay() override;
	// Hub原生子类构造时绑定的本地展示资源。普通终端两者为空；不会通过网络传送资产指针。
	UPROPERTY() TObjectPtr<UStaticMesh> UpgradeModel;
	UPROPERTY() TObjectPtr<UStaticMesh> DepartureModel; // 出发门框；由原生类默认对象强引用并带入Cook依赖。
private:
	/** 在配置、初始显示或类型复制后按当前状态一次性选网格与编辑器标签。 */
	void RefreshTerminalView();
	/** 客户端收到类型后重建本地表现，仍由同一个服务器Actor处理交互。 */
	UFUNCTION() void OnRep_TerminalKind();
	// 明确形状的根碰撞用于出生避障与玩家阻挡；半尺寸 60/60/80cm，随 Actor 销毁。
	UPROPERTY() TObjectPtr<UBoxComponent> Collision;
	// 唯一可见网格，始终无碰撞；根碰撞和HUD定位独立于显示资源的尺寸和枢轴。
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Mesh;
	// 场景英文标牌（UPGRADE TERMINAL / ENTER NEXT SECTOR）。2026-11-19起游戏内默认隐藏：与HUD中文短名重复且中英混排；编辑器视口仍显示，组件保留供将来换中文离线字体或图标。
	UPROPERTY() TObjectPtr<UTextRenderComponent> Label;
	// 生成后保持不变的类型复制给所有玩家，权限仍在房主Terminal服务。
	UPROPERTY(ReplicatedUsing=OnRep_TerminalKind) bool bIsShop = true;
};

/** 带正式模型的原生外观变体（升级石碑/出发门）。2026-11-20起安全区与局内清关终端都生成此类；基类圆柱只作资产缺失后备。沿用基类全部交互与复制字段。 */
UCLASS()
class FPSDEMO_API ADemoHubInteractable : public ADemoInteractable
{
	GENERATED_BODY()
public:
	/** 构造时缓存两件正式带材质的网格，类型变化时无需加载资产。 */
	ADemoHubInteractable();
};
