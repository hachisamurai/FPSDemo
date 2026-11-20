// 职责：闪避流线的集中美术配置；DA_DashFlow由Pawn表现组件读取，不拥有位移、冷却或免伤，也不复制粒子。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DemoDashFXProfile.generated.h"

class UNiagaraSystem;
class UMaterialInterface;
class USoundBase;
class USoundAttenuation;

/** 调色/宽度/采样密度/收尾/声音在此编辑；系统派生自Lyra，角色材质不参与形变。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoDashFXProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    // 兼容旧全身快照模式；默认false，修改后下次创建Pawn生效，旧材质与Config仍可回退。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Mode") bool bLegacyAfterimages=false;
    // 包含DashPoints/RibbonTrails的本项目系统，硬引用确保Cook包含，不依赖Lyra插件。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Assets") TObjectPtr<UNiagaraSystem> System;
    // 独立拖尾材质；每Pawn建立MID，FlowFade/FlowStrength/FlowNear/FlowFar仅影响粒子。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Assets") TObjectPtr<UMaterialInterface> Material;
    // 默认青白线性RGB；沿用服务器SetTrailColor接口后该Pawn覆盖此默认，不写存档。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Visual") FLinearColor Color=FLinearColor(.12f,.85f,.72f,.65f);
    // 世界空间流线宽度cm，0.2..8；通过Niagara User.TrailWidth驱动真实Ribbon几何。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Visual", meta=(ClampMin="0.2",ClampMax="8",Units="cm")) float Width=1.6f;
    // 一次采样轨迹数量，8..48；CPU粒子预算，远端与自己一致，四人总成本需实机测量。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Visual", meta=(ClampMin="8",ClampMax="48")) int32 PointCount=24;
    // 停止发射后的渐隐秒数，0.05..0.3；不延长推进或GAS躲避标签。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Visual", meta=(ClampMin="0.05",ClampMax="0.3",Units="s")) float FadeSeconds=.18f;
    // 第三人称发光/透明度乘数0..2；仅改变粒子，身体轮廓通过贴身流线表现。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Visual", meta=(ClampMin="0",ClampMax="2")) float Strength=.65f;
    // 本地控制Pawn乘数0..1；其第三人称测试相机也保留此乘数，远端观察者不受影响。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|First Person", meta=(ClampMin="0",ClampMax="1")) float OwnerStrength=.3f;
    // 镜头附近完全隐藏/完全显示的距离cm；两者之间平滑淡入，中心视线不叠全屏后处理。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|First Person", meta=(ClampMin="0",ClampMax="150",Units="cm")) float CameraNear=45.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|First Person", meta=(ClampMin="1",ClampMax="250",Units="cm")) float CameraFar=110.f;
    // 连续低速达到此秒数停止发射，保留起步缓冲以容忍网络平滑；碰撞不会原地堆积粒子。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Timing", meta=(ClampMin="0.03",ClampMax="0.15",Units="s")) float BlockedGrace=.06f;
    // 每次成功闪避只播放一次；本机2D、他人空间声，死亡清理，拒绝/重复Cue不播放。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Audio") TObjectPtr<USoundBase> Sound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Audio") TObjectPtr<USoundAttenuation> Attenuation; // 远端声音距离衰减，避免全地图等响。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Dash|Audio", meta=(ClampMin="0",ClampMax="1")) float Volume=.45f; // 局部声音增益，不修改全局设置。
};
