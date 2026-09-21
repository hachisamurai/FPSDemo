#pragma once

#include "CoreMinimal.h"

class USoundBase;
class UAudioComponent;

/** 世界坐标一次性播放接口；随机和衰减由可编辑 SoundCue 资产负责。 */
namespace DemoWeaponAudio
{
    /** Context 提供当前 World，Cue 是 Actor 强引用资产，Location 为发声世界坐标（cm），EventName 为日志用途。
     * 返回引擎自动销毁的 AudioComponent 借用引用；不附着死亡目标，因此致命击中尾音可继续播放。
     * 无音频设备/专服/资产缺失返回 nullptr 并记录，不影响伤害逻辑。 */
    FPSDEMO_API UAudioComponent* PlayAtLocation(const UObject* Context, USoundBase* Cue, const FVector& Location, FName EventName);
}
