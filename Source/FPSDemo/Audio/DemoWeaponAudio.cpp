#include "Audio/DemoWeaponAudio.h"
#include "Debug/DemoLog.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

UAudioComponent* DemoWeaponAudio::PlayAtLocation(const UObject* Context, USoundBase* Cue, const FVector& Location, FName EventName)
{
    UE_LOG(LogFPSDemo, Log, TEXT("%hs event=%s owner=%s cue=%s position=%s"), __FUNCTION__, *EventName.ToString(),
        *GetNameSafe(Context), *GetNameSafe(Cue), *Location.ToString());
    if (!Context || !Cue)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("WEAPON_AUDIO_SKIPPED %s: missing context/cue; run Scripts/Audio/import_weapon_sfx.py"), *EventName.ToString());
        return nullptr;
    }
    // 参数保持 1.0；Cue 中 Modulator 已随机音高/增益，此处不叠加第二次随机。
    // 自动销毁组件归 World 管理，Actor 死亡后不会截断 0.4 秒命中尾音。
    UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(Context, Cue, Location, FRotator::ZeroRotator,
        1.f, 1.f, 0.f, nullptr, nullptr, true);
    if (Audio)
    {
        // 标签只用于运行时检查用途；不承担资产选择或复制语义。
        Audio->ComponentTags.Add(EventName);
        UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_AUDIO_STARTED event=%s component=%s cue=%s"), *EventName.ToString(), *GetNameSafe(Audio), *Cue->GetPathName());
    }
    else UE_LOG(LogFPSDemo, Log, TEXT("WEAPON_AUDIO_SKIPPED %s: no playback device, inaudible location or dedicated server"), *EventName.ToString());
    return Audio;
}
