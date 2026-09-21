#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoAudioAssetLibrary.generated.h"

class USoundCue;
class USoundWave;
class USoundAttenuation;

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
};
