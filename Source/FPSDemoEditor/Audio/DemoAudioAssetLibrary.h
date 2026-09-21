#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoAudioAssetLibrary.generated.h"

class USoundCue;
class USoundWave;
class USoundAttenuation;

/** 离线 Cue 制作/校验使用同一组参数；默认兼容旧 Sword 资源，不参与运行时复制。 */
USTRUCT(BlueprintType)
struct FDemoWeaponCueSettings
{
    GENERATED_BODY()

    /** 每次开火的随机音高倍率，保持正数且最小值不大于最大值。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
    FVector2D PitchRange = FVector2D(0.95, 1.05);
    /** 随机增益端点，单位 dB；实际 Modulator 使用转换后的线性振幅。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
    FVector2D GainDbRange = FVector2D(-1.5, 1.5);
    /** 原始每个变体的预期时长，单位秒；允许导入误差 1ms。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
    float DurationSeconds = 0.4f;
    /** Cue 总线性音量，范围 (0,1]；与源 WAV 峰值分开，给游戏混音留余量。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
    float Volume = 1.f;
};

/** UE5.4 Python 无公开 SoundCue 图连线 API；通过此编辑器专用桥接使用引擎原生图构建。
 * 资产创建/保存仍由 Editor Python AssetTools 完成，不手写二进制包。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoAudioAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Cue 是 AssetTools 创建的目标，Waves 恰好三个独立 SoundWave，Attenuation 是共享衰减资产。
     * 返回 false 时 Python 必须停止保存；已有正确拓扑会更新，不累计多余节点。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Audio")
    static bool ConfigureWeaponCue(USoundCue* Cue, const TArray<USoundWave*>& Waves, USoundAttenuation* Attenuation);

    /** Cue 为刚保存/重新加载的资产；校验真实拓扑、随机范围、波形规格和空间配置，失败有日志。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Audio")
    static bool ValidateWeaponCue(USoundCue* Cue);

    /** Cue/Waves/Attenuation 为同步借用的 Editor 资产；Settings 定义音色随机范围、时长和混音余量。
     * 只更新匹配的三分支图；非法参数拒绝修改，返回 false 时调用者停止保存。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Audio")
    static bool ConfigureWeaponCueWithSettings(USoundCue* Cue, const TArray<USoundWave*>& Waves,
        USoundAttenuation* Attenuation, const FDemoWeaponCueSettings& Settings);

    /** Cue 为已加载资产，Settings 为制作时预期参数；只读核对节点、范围、时长和 3D 衰减。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Editor|Audio")
    static bool ValidateWeaponCueWithSettings(USoundCue* Cue, const FDemoWeaponCueSettings& Settings);
};
