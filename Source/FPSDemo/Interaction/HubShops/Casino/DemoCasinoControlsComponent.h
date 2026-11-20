// 职责：注册设备上的稳定QueryOnly控件与公开静态按键标牌；不承载私人金额/骰面，不随Owner本机动画移动，不生成经济事实。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlTypes.h"
#include "DemoCasinoControlsComponent.generated.h"
class UBoxComponent;
class UPrimitiveComponent;
class ADemoCharacter;
class ADemoCasinoDevice;
class UDemoCasinoDeviceProfile;
class UStaticMeshComponent;

UCLASS()
class FPSDEMO_API UDemoCasinoControlsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 无Tick，设备Refresh/OnRep显式重建，服务器与客户端同可信绑定。 */
    UDemoCasinoControlsComponent();
    /** DeviceMode/资源Refresh时同步加载验证正式Profile或显式原型；非Mode1撤控件，指定非法正式资产不回退。 */
    void Configure(int32 DeviceMode);
    /** 幂等撤销本组件创建的查询/标牌，Actor卸载与设备失效调用。 */
    void Clear();
    /** Actor卸载时清理动态组件，不修改共享注册表或个人未结局。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** 当前Mode1绑定完整且版本相符；仅存在不等于玩家获租约。 */
    bool IsReady() const;
    /** 当前实际构建版本，0未就绪；Formal来自Profile，不使用旧全局原型2给新模型补签。 */
    uint32 GetBindingRevision() const;
    /** 指定正式资源但无效时Actor必须禁用而非落回旧P0/原型几何。 */
    bool IsModelRequested() const;
    /** 当前已验证正式Profile，空仅代表明确旧原型或未启用；不在Tick同步加载。 */
    const UDemoCasinoDeviceProfile* GetModelProfile() const;
    /** Profile Parts同索引公共中性Primitive，用于本人PC逐视图替换；不允许LocalView移动它。 */
    UStaticMeshComponent* GetNeutralPart(int32 Index) const;
    /** 把当前真实Hit组件解析为白名单绑定；未知/失效组件返回nullptr。 */
    const FDemoCasinoControlBinding* FindHit(const UPrimitiveComponent* Component) const;
    /** 查可信稳定ID，不能由客户端创建绑定或改变Action/Index。 */
    const FDemoCasinoControlBinding* FindControl(FName ControlId) const;
    /** 返回当前固定查询中心的世界cm位置，用于真实夹具和权威LOS；失败不返回伪原点。 */
    bool GetControlPoint(FName ControlId,FVector& Point) const;
    /** Server当前Avatar共享眼位/原控制视向首次Visibility命中必须是指定Query；不使用旧BaseEyeHeight，外层仍重验注册/租约。 */
    bool ValidateControlHit(ADemoCharacter* Pawn,FName ControlId,FString& Error) const;
private:
    /** 已验证Profile生成公共静态/中性件及根Query，无经济或私人内容。 */
    void BuildModel();
    UPROPERTY(Transient) TArray<FDemoCasinoControlBinding> Bindings; // 本组件可信构建，绝不复制客户端提交值。
    UPROPERTY(Transient) TArray<TObjectPtr<UBoxComponent>> Queries; // 与Bindings相同索引，仅查询不阻挡Pawn。
    UPROPERTY(Transient) TArray<TObjectPtr<UActorComponent>> Parts; // 公开静态按钮/文字，仅本Actor生命周期。
    UPROPERTY(Transient) TObjectPtr<UDemoCasinoDeviceProfile> ModelProfile; // 本次已验证资源强引用，变更时整组重建。
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> NeutralParts; // 与Profile.Parts同索引，NoCollision；每视图隐藏不改其公共显隐。
    uint32 BuiltRevision=0; // 同Mode也必须按绑定版本幂等；不写存档。
    uint32 BuiltFingerprint=0; // 同revision资源内容变更时拒绝复用，而非保留旧Query。
    uint32 LastModelRevision=0; // 跨Clear保留上次正式版本，防同版本换内容后下一Configure偷偷接受。
    uint32 LastModelFingerprint=0; // 上次正式内容指纹，修改须递增revision。
    FName LastModelId; // 上次正式ProfileId；不复制、不当作玩家身份。
    bool bModelRequested=false; // 资源路径已明确指定；无效时禁物理/禁旧几何回退。
};
