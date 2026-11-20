// 职责：正式老虎机同版模型/控件/私有机械表现的可信作者定义；只含实测几何，不含赔率、钱包或随机。由独立Settings选择，Editor创建资产后才可配置引用。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Interaction/HubShops/Casino/DemoCasinoControlTypes.h"
#include "DemoCasinoDeviceProfile.generated.h"
class UStaticMesh;
class UMaterialInterface;

/** 刚性件表现职责；只有本机Owner替换件运动，服务器Query从不附着这些件。 */
UENUM(BlueprintType)
enum class EDemoCasinoModelPartKind : uint8 { Static, Key, Reel, Lever };

/** 一个独立刚性部件；网格原点即真实pivot，RestTransform为设备根局部cm/旋转，单位缩放。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoCasinoModelPart
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) FName PartId; // 同Profile唯一稳定部件身份，不能用数组次序授权。
    UPROPERTY(EditAnywhere) TObjectPtr<UStaticMesh> Mesh; // 同版真实导入网格；空件令正式Profile不可用。
    UPROPERTY(EditAnywhere) FTransform RestTransform; // 根空间实际装配姿态，活动件原点必须在自身pivot。
    UPROPERTY(EditAnywhere) TArray<TObjectPtr<UMaterialInterface>> Materials; // 可空覆盖槽，不含私人状态或概率。
    UPROPERTY(EditAnywhere) EDemoCasinoModelPartKind Kind=EDemoCasinoModelPartKind::Static; // Static公共，其他由Owner本机层替换公共中性件。
    UPROPERTY(EditAnywhere) FName ControlId; // Key/Lever对应可信控件；Static/Reel为空。
    UPROPERTY(EditAnywhere) int32 ReelIndex=INDEX_NONE; // 仅Reel的0..2，按操作者画面左中右。
    UPROPERTY(EditAnywhere) FVector MotionAxis=FVector(0,1,0); // 自身pivot框的单位运动轴，Key平移、其余旋转；实际源测量。
    UPROPERTY(EditAnywhere) float MotionAmount=0.f; // Key压入cm(0..2)或Lever角度deg(-120..120)，不填原型默认动作。
    UPROPERTY(EditAnywhere) TArray<float> SymbolStopAngles; // 仅Reel五项deg，0普通/1火/2冰/3穿透/4金，必须同版实测。
};

/** S01可信模型闭包；空默认值明确不可运行，不制造最终网格/测量坐标。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoCasinoDeviceProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 作者/加载时校验15个唯一语义、真实网格/轴/源版本；Error带可定位原因，不在Tick加载。 */
    UFUNCTION(BlueprintCallable,Category="Casino") bool Validate(FString& Error) const;
    /** 同步纯作者指纹，检测同revision修改内容；不是交易签名或客户端权限。 */
    uint32 ContentFingerprint() const;
    UPROPERTY(EditAnywhere) FName ProfileId; // 例如casino.slots.s01；空不回退原型。
    UPROPERTY(EditAnywhere) int32 BindingRevision=3; // 正式>=3，原型2保留；绑定/模型改变须递增，Owner复用现uint32字段。
    UPROPERTY(EditAnywhere) FString SourceSHA256; // 同版实际blend完整64位十六进制SHA，未交模型前保持空而非伪造。
    UPROPERTY(EditAnywhere) TArray<FDemoCasinoModelPart> Parts; // 公共中性件及Owner真实机械件模板，有界<=64。
    UPROPERTY(EditAnywhere) TArray<FDemoCasinoControlBinding> Controls; // 精确15项，含根局部查询中心/旋转/半尺寸；仅可信资产提供。
    UPROPERTY(EditAnywhere) FTransform FocusTransform; // 稳定根局部实际Focus框，不跟机械动画。
    UPROPERTY(EditAnywhere) FTransform InfoTransform; // 静态标牌框，根局部cm。
    UPROPERTY(EditAnywhere) FTransform CollisionTransform; // 简化柜体阻挡盒根局部框，不使用旧110立方尺寸。
    UPROPERTY(EditAnywhere) FVector CollisionExtent=FVector::ZeroVector; // 实测阻挡半尺寸cm，零值拒绝而非猜测。
    UPROPERTY(EditAnywhere) FTransform ScreenTransform; // Owner显示面根局部框，实际源提供，不采用旧82/0/180。
    UPROPERTY(EditAnywhere) float ScreenTextSizeCm=0.f; // 同版屏实际可读字号cm，0无效，范围.5..20。
    UPROPERTY(EditAnywhere) float RevealSeconds=.8f; // 已保存后纯视觉秒(.1..3)，不超过原Flow三秒防重复窗口，不改扣款/随机。
    UPROPERTY(EditAnywhere) float MechanicalSeconds=.35f; // 已保存后杆回弹/键反馈秒(.05..2)，不改事务deadline。
private:
    /** 同步结构校验实现；公共Validate统一记录所有拒绝原因，便于作者与脚本定位。 */
    bool ValidateDefinition(FString& Error) const;
};
