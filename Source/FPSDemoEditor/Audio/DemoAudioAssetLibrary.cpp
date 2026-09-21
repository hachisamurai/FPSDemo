#include "DemoAudioAssetLibrary.h"
#include "Debug/DemoLog.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "EdGraph/EdGraphNode.h"
#include "Modules/ModuleManager.h"

bool UDemoAudioAssetLibrary::ConfigureWeaponCue(USoundCue* Cue, const TArray<USoundWave*>& Waves, USoundAttenuation* Attenuation)
{
    UE_LOG(LogFPSDemo, Log, TEXT("%hs legacy defaults cue=%s"), __FUNCTION__, *GetNameSafe(Cue));
    return ConfigureWeaponCueWithSettings(Cue, Waves, Attenuation, FDemoWeaponCueSettings());
}

bool UDemoAudioAssetLibrary::ConfigureWeaponCueWithSettings(USoundCue* Cue, const TArray<USoundWave*>& Waves,
    USoundAttenuation* Attenuation, const FDemoWeaponCueSettings& Settings)
{
    UE_LOG(LogFPSDemo, Log, TEXT("%hs cue=%s waves=%d"), __FUNCTION__, *GetNameSafe(Cue), Waves.Num());
    // 所有字段先检查有限性及范围；先于 Modify，避免将 NaN/无效音高写入资产。
    if (!Cue || Waves.Num() != 3 || !Attenuation || Waves.Contains(nullptr)
        || !FMath::IsFinite(Settings.PitchRange.X) || !FMath::IsFinite(Settings.PitchRange.Y)
        || Settings.PitchRange.X <= 0.0 || Settings.PitchRange.X > Settings.PitchRange.Y
        || !FMath::IsFinite(Settings.GainDbRange.X) || !FMath::IsFinite(Settings.GainDbRange.Y)
        || Settings.GainDbRange.X < -60.0 || Settings.GainDbRange.Y > 6.0 || Settings.GainDbRange.X > Settings.GainDbRange.Y
        || !FMath::IsFinite(Settings.DurationSeconds) || Settings.DurationSeconds <= 0.f
        || !FMath::IsFinite(Settings.Volume) || Settings.Volume <= 0.f || Settings.Volume > 1.f)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("Audio authoring rejected: missing assets, not three waves, or invalid cue settings"));
        return false;
    }
    // 命令行导入也需要 AudioEditor 安装 SoundCue 图操作接口，禁止直接生成假的图序列化数据。
    FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AudioEditor"));
    Cue->Modify();
    // Modulator/Random 均由 Cue 作为 Outer 拥有，跨保存的引用存放在 Cue FirstNode/AllNodes。
    USoundNodeModulator* Modulator = Cast<USoundNodeModulator>(Cue->FirstNode);
    USoundNodeRandom* Random = Modulator && Modulator->ChildNodes.Num() == 1 ? Cast<USoundNodeRandom>(Modulator->ChildNodes[0]) : nullptr;
    if (Cue->FirstNode && (!Modulator || !Random || Random->ChildNodes.Num() != 3))
    {
        UE_LOG(LogFPSDemo, Error, TEXT("Existing cue has a different graph; refusing to overwrite custom authoring: %s"), *Cue->GetPathName());
        return false;
    }
    if (!Modulator)
    {
        Modulator = Cue->ConstructSoundNode<USoundNodeModulator>();
        Random = Cue->ConstructSoundNode<USoundNodeRandom>();
        // WaveNodes 为构图期间借用列表，SetChildNodes 复制 UObject 引用到节点的 UPROPERTY。
        TArray<USoundNode*> WaveNodes;
        // Index 同时定义等概率分支索引和编辑器图纵向位置。
        for (int32 Index = 0; Index < 3; ++Index)
        {
            // Player 归 Cue 所有，SetSoundWave 同时设置可序列化软引用与运行时硬引用。
            USoundNodeWavePlayer* Player = Cue->ConstructSoundNode<USoundNodeWavePlayer>();
            Player->SetSoundWave(Waves[Index]);
            Player->bLooping = false;
            Player->GraphNode->NodePosX = -650;
            Player->GraphNode->NodePosY = (Index-1)*170;
            WaveNodes.Add(Player);
        }
        Random->SetChildNodes(WaveNodes);
        // 每个 SoundNodeModulator 只接受一个 Random 输入，根指向 Modulator。
        TArray<USoundNode*> ModulatorInputs = {Random};
        Modulator->SetChildNodes(ModulatorInputs);
        Cue->FirstNode = Modulator;
    }
    // 重复导入更新现有三条分支，不追加新的图节点。
    // Index 为变体编号对应的分支位置，范围 0..2。
    for (int32 Index = 0; Index < 3; ++Index)
    {
        // Player 借用 Cue 已持有的分支节点，类型不符时中止以保留用户的自定义图。
        USoundNodeWavePlayer* Player = Cast<USoundNodeWavePlayer>(Random->ChildNodes[Index]);
        if (!Player) { UE_LOG(LogFPSDemo, Error, TEXT("Audio branch is not a WavePlayer")); return false; }
        Player->SetSoundWave(Waves[Index]);
        Player->bLooping = false;
    }
    Random->Weights = {1.f,1.f,1.f};
    Random->bRandomizeWithoutReplacement = true;
    Random->PreselectAtLevelLoad = 0;
    Random->bShouldExcludeFromBranchCulling = true;
    Modulator->PitchMin = Settings.PitchRange.X;
    Modulator->PitchMax = Settings.PitchRange.Y;
    // SoundNodeModulator 存 float；显式转换 FVector2D 的 double，构图/验证均用相同精度。
    // dB 转线性振幅为 10^(dB/20)，不能将 dB 直接当振幅相加。
    Modulator->VolumeMin = FMath::Pow(10.f, static_cast<float>(Settings.GainDbRange.X)/20.f);
    Modulator->VolumeMax = FMath::Pow(10.f, static_cast<float>(Settings.GainDbRange.Y)/20.f);
    Modulator->GraphNode->NodePosX = -200;
    Random->GraphNode->NodePosX = -430;
    Cue->VolumeMultiplier = Settings.Volume;
    Cue->PitchMultiplier = 1.f;
    Cue->AttenuationSettings = Attenuation;
    Cue->bOverrideAttenuation = false;
    Cue->bExcludeFromRandomNodeBranchCulling = true;
    // ConstructSoundNode 创建图节点时 Random 尚无三个输入；设置 ChildNodes 后重建引脚再连线。
    // 否则 UE5.4 SoundCueGraph 会因输入引脚数量和 ChildNodes 不同而触发断言。
    Modulator->GraphNode->ReconstructNode();
    Random->GraphNode->ReconstructNode();
    Cue->LinkGraphNodesFromSoundNodes();
    Cue->CompileSoundNodesFromGraphNodes();
    Cue->PostEditChange();
    Cue->MarkPackageDirty();
    return ValidateWeaponCueWithSettings(Cue, Settings);
}

bool UDemoAudioAssetLibrary::ValidateWeaponCue(USoundCue* Cue)
{
    UE_LOG(LogFPSDemo, Log, TEXT("%hs legacy defaults cue=%s"), __FUNCTION__, *GetNameSafe(Cue));
    return ValidateWeaponCueWithSettings(Cue, FDemoWeaponCueSettings());
}

bool UDemoAudioAssetLibrary::ValidateWeaponCueWithSettings(USoundCue* Cue, const FDemoWeaponCueSettings& Expected)
{
    UE_LOG(LogFPSDemo, Log, TEXT("%hs cue=%s"), __FUNCTION__, *GetNameSafe(Cue));
    // 校验引用仅在本次同步调用借用；从反序列化后的实际节点读取配置。
    const USoundNodeModulator* Modulator = Cue ? Cast<USoundNodeModulator>(Cue->FirstNode) : nullptr;
    const USoundNodeRandom* Random = Modulator && Modulator->ChildNodes.Num() == 1 ? Cast<USoundNodeRandom>(Modulator->ChildNodes[0]) : nullptr;
    if (!Cue || !Random || Random->ChildNodes.Num() != 3 || Random->Weights.Num() != 3 || Cue->AllNodes.Num() != 5
        || !Random->bRandomizeWithoutReplacement || Random->PreselectAtLevelLoad != 0
        || !FMath::IsNearlyEqual(Modulator->PitchMin, static_cast<float>(Expected.PitchRange.X))
        || !FMath::IsNearlyEqual(Modulator->PitchMax, static_cast<float>(Expected.PitchRange.Y))
        || !FMath::IsNearlyEqual(Modulator->VolumeMin,FMath::Pow(10.f,static_cast<float>(Expected.GainDbRange.X)/20.f))
        || !FMath::IsNearlyEqual(Modulator->VolumeMax,FMath::Pow(10.f,static_cast<float>(Expected.GainDbRange.Y)/20.f))
        || !FMath::IsNearlyEqual(Cue->VolumeMultiplier,Expected.Volume) || !FMath::IsNearlyEqual(Cue->PitchMultiplier,1.f)
        || !Cue->AttenuationSettings || Cue->bOverrideAttenuation)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("Audio validation failed: topology/modulation/attenuation reference"));
        return false;
    }
    // 通过集合证明三个分支引用不同波形，而不是同一资源重复三次。
    TSet<const USoundWave*> UniqueWaves;
    // Index 为同步校验中的分支位置，不跨调用保存。
    for (int32 Index = 0; Index < 3; ++Index)
    {
        // Player/Wave 均借用已加载 Cue 的节点及波形引用，仅验证，不改变所有权。
        const USoundNodeWavePlayer* Player = Cast<USoundNodeWavePlayer>(Random->ChildNodes[Index]);
        const USoundWave* Wave = Player ? Player->GetSoundWave() : nullptr;
        if (!Wave || Player->bLooping || Wave->NumChannels != 1 || !FMath::IsNearlyEqual(Wave->Duration,Expected.DurationSeconds,0.001f)
            || !FMath::IsNearlyEqual(Random->Weights[Index],1.f))
        {
            UE_LOG(LogFPSDemo, Error, TEXT("Audio validation failed: branch=%d wave format/duration/weight"),Index);
            return false;
        }
        UniqueWaves.Add(Wave);
    }
    // 球形衰减：内部半径为无衰减区，FalloffDistance 为其外侧淡出距离，单位 cm。
    const FSoundAttenuationSettings& Settings = Cue->AttenuationSettings->Attenuation;
    if (UniqueWaves.Num() != 3 || !Settings.bAttenuate || !Settings.bSpatialize || Settings.AttenuationShape != EAttenuationShape::Sphere
        || Settings.AttenuationShapeExtents.X <= 0.f || Settings.FalloffDistance <= 0.f)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("Audio validation failed: uniqueness or spatial settings"));
        return false;
    }
    UE_LOG(LogFPSDemo, Display, TEXT("AUDIO_CUE_VALID %s variants=3 pitch=[%.3f,%.3f] gain_db=[%.1f,%.1f] volume=%.2f duration=%.3f inner_cm=%.0f falloff_cm=%.0f"),
        *Cue->GetPathName(), Expected.PitchRange.X, Expected.PitchRange.Y, Expected.GainDbRange.X, Expected.GainDbRange.Y,
        Expected.Volume, Expected.DurationSeconds, Settings.AttenuationShapeExtents.X, Settings.FalloffDistance);
    return true;
}
