// 职责：按T采集→16kHz单声道Opus→房主转发→讲话Pawn位置的3D播放/嘴型。使用现有NetDriver及UE球形衰减，不创建额外语音连接，不记录音频内容。
#include "Player/Voice/DemoVoiceComponent.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Game/DemoGameState.h"
#include "Debug/DemoLog.h"
#include "VoiceModule.h"
#include "Interfaces/VoiceCapture.h"
#include "Interfaces/VoiceCodec.h"
#include "Sound/SoundWaveProcedural.h"
#include "Sound/SoundAttenuation.h" // 使用UE真实混音器衰减/声道定位，不在业务层重复计算音量导致双重衰减。
#include "GameFramework/Pawn.h" // 远端没有PC也能通过复制PS解析当前Pawn和眼部世界位置。
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/App.h"

namespace DemoVoice
{
    constexpr int32 SampleRate = 16000; // 两端协议固定Hz，不允许仅修改一侧配置。
    constexpr int32 FrameSamples = 320; // UE5.4 Opus固定20ms帧；16bit PCM。
    constexpr int32 FrameBytes = FrameSamples * sizeof(int16); // 每网络帧解码后恰好640字节。
    constexpr int32 MaxQueuedBytes = FrameBytes * 6; // FIFO预算120ms；拥塞丢弃旧音频，防持续积压。
}

bool FDemoVoicePacket::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] FDemoVoicePacket::NetSerialize")); // 值结构不是UObject，不能使用要求GetNameSafe(this)的TICK宏；保持相同级别/限时开关。
    Ar << Epoch << Sequence;
    Ar.SerializeBits(&Size, 9); // 先读取有界长度；不能用客户端提供的数组长度申请内存。
    if (Size > Capacity) { Ar.SetError(); bOutSuccess = false; return true; }
    Ar.Serialize(Payload, Size);
    bOutSuccess = !Ar.IsError();
    return true;
}

UDemoVoiceComponent::UDemoVoiceComponent()
{
    DEMO_LOG_CALL();
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
    PrimaryComponentTick.TickInterval = .02f; // 只轮询50Hz；静止时只做门禁/超时判断，无采集/编解码。
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // 声源跟随本帧移动/观战视图更新之后的位置，避免先于角色移动造成方向滞后。
}

ADemoPlayerController* UDemoVoiceComponent::GetVoiceController() const
{
    DEMO_LOG_TICK();
    return GetOwner() ? Cast<ADemoPlayerController>(GetOwner()->GetOwner()) : nullptr;
}

bool UDemoVoiceComponent::CanCapture() const
{
    DEMO_LOG_TICK();
    const ADemoPlayerController* PC = GetVoiceController(); // PS.Owner为真实拥有者PC，远端无PC时返回false。
    const ADemoGameState* State = GetWorld() ? GetWorld()->GetGameState<ADemoGameState>() : nullptr; // 大厅不打开设备。
    const UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr; // 只使用当前World视口，PIE多窗口互不借用。
    return PC && PC->IsLocalController() && PC->IsCoopRegistered() && !PC->HasBlockingOverlay()
        && !PC->IsUpgradeMenuOpen() && !PC->IsNextLevelConfirmationOpen() && !PC->IsPaused()
        && State && State->Phase != EDemoPhase::Lobby
        && Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus() && FApp::HasFocus();
}

void UDemoVoiceComponent::StartTalking()
{
    DEMO_LOG_CALL();
    const double Now = FPlatformTime::Seconds(); // 真实时间限制快速重复按键，不随World暂停。
    if (bCapturing || Now < NextStartAt || !CanCapture()) { LogFailure(TEXT("start blocked by focus/menu/registration")); return; }
    NextStartAt = Now + .1;
    DeviceError.Reset();
    FVoiceModule& Module = FVoiceModule::Get(); // 只在明确按键后加载设备服务，非Editor云模块。
    if (!Module.IsVoiceEnabled()) { DeviceError = TEXT("语音未启用，请检查 Voice 配置"); LogFailure(TEXT("Voice module disabled")); return; }
    Capture = Module.CreateVoiceCapture(FString(), DemoVoice::SampleRate, 1);
    Encoder = Module.CreateVoiceEncoder(DemoVoice::SampleRate, 1, EAudioEncodeHint::VoiceEncode_Voice);
    if (!Capture.IsValid() || !Encoder.IsValid() || !Capture->Start())
    {
        DeviceError = TEXT("麦克风不可用：请检查默认输入设备及系统麦克风权限，再按 T 重试");
        Capture.Reset(); Encoder.Reset(); LogFailure(TEXT("microphone/encoder start failed")); return;
    }
    Encoder->SetBitrate(24000); Encoder->SetVBR(false); Encoder->SetComplexity(5); // 小且稳定的包体和中等编码复杂度，保留CPU/带宽给战斗。
    PendingPCM.Reset(); SendSequence = 0; ++LocalEpoch;
    if (LocalEpoch == 0) ++LocalEpoch; // 0为未开始状态，极端回绕跳过保留值。
    bCapturing = true; CaptureStartedAt = Now;
    ServerSetTransmission(true, LocalEpoch);
    UE_LOG(LogFPSDemo, Log, TEXT("VOICE_CAPTURE_STARTED epoch=%u"), LocalEpoch);
}

void UDemoVoiceComponent::StopTalking()
{
    DEMO_LOG_CALL();
    if (Capture.IsValid()) Capture->Stop();
    Capture.Reset(); Encoder.Reset(); CaptureBuffer.Reset(); PendingPCM.Reset();
    SpeechAmount = 0; SpeechUntil = 0;
    if (bCapturing)
    {
        bCapturing = false;
        ServerSetTransmission(false, LocalEpoch);
        UE_LOG(LogFPSDemo, Log, TEXT("VOICE_CAPTURE_STOPPED epoch=%u"), LocalEpoch);
    }
}

void UDemoVoiceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime, TickType, TickFunction);
    if (bCapturing)
    {
        const ADemoPlayerController* PC = GetVoiceController(); // 本帧重新获取；旅行/断线可失效。
        if (!CanCapture() || !PC || !PC->IsInputKeyDown(EKeys::T) || FPlatformTime::Seconds() - CaptureStartedAt > 60.)
        { StopTalking(); return; } // 失焦清键、输入取消或最长时限均必须重新按下，不自动恢复采集。
        CaptureFrames();
    }
    if (Playback && Playback->IsPlaying())
    {
        if (!UpdatePlaybackLocation() || FPlatformTime::Seconds() - LastReceivedAt > .25)
            ResetPlayback(); // 源Pawn丢失或250ms无包时清队列，避免旧声源/断网积压；恢复后只播放新收到的帧。
    }
}

void UDemoVoiceComponent::CaptureFrames()
{
    DEMO_LOG_TICK();
    uint32 Available = 0; // 此次设备缓冲字节数，读取前限制上界。
    const EVoiceCaptureState::Type State = Capture->GetCaptureState(Available); // 捕获句柄只在本线程Start/Stop操作。
    if (State == EVoiceCaptureState::Error || State == EVoiceCaptureState::UnInitialized || State == EVoiceCaptureState::NotCapturing)
    { DeviceError = TEXT("麦克风已断开，请检查设备后重新按 T"); LogFailure(TEXT("capture device lost")); StopTalking(); return; }
    if (State != EVoiceCaptureState::Ok || Available == 0) return; // Windows静音检测可能没有PCM；嘴型由真实时间自动关闭。
    if (Available > 16000 || (Available & 1) != 0)
    { DeviceError = TEXT("语音采集积压，请松开 T 后重试"); LogFailure(TEXT("capture buffer overflow")); StopTalking(); return; }
    CaptureBuffer.SetNumUninitialized(Available);
    uint32 Read = 0; // 实际读回字节数，不能把未初始化尾部送入编码器。
    if (Capture->GetVoiceData(CaptureBuffer.GetData(), Available, Read) != EVoiceCaptureState::Ok || Read > Available || (Read & 1) != 0)
    { LogFailure(TEXT("capture read failed")); return; }
    PendingPCM.Append(CaptureBuffer.GetData(), Read);
    const int32 Frames = PendingPCM.Num() / DemoVoice::FrameBytes; // 最多4帧突发，超过部分按完整帧丢掉，保留最新语音和余数。
    if (Frames > 4) PendingPCM.RemoveAt(0, (Frames - 4) * DemoVoice::FrameBytes, EAllowShrinking::No);
    int32 Consumed = 0; // 本Tick消费的PCM前缀，批量移除避免逐帧memmove。
    while (PendingPCM.Num() - Consumed >= DemoVoice::FrameBytes)
    {
        FDemoVoicePacket Packet; // 值类型固定容量，没有动态网络数组。
        Packet.Epoch = LocalEpoch; Packet.Sequence = ++SendSequence;
        uint32 Written = FDemoVoicePacket::Capacity; // 编码API输入容量/输出实际字节数。
        MeasureSpeech(reinterpret_cast<const int16*>(PendingPCM.GetData() + Consumed), DemoVoice::FrameSamples);
        Encoder->Encode(PendingPCM.GetData() + Consumed, DemoVoice::FrameBytes, Packet.Payload, Written);
        Packet.Size = static_cast<uint16>(FMath::Min<uint32>(Written, FDemoVoicePacket::Capacity));
        if (Written == 4 && Packet.Payload[0] == 1 && Packet.Payload[2] == 0 && Packet.Payload[3] == 0)
        { /* UE Opus的DTX空帧只有4字节头：正常静音，不传输，不把它当编码失败刷日志。 */ }
        else if (Written <= FDemoVoicePacket::Capacity && IsPacketValid(Packet)) ServerVoiceFrame(Packet);
        else LogFailure(TEXT("encoder produced invalid frame"));
        Consumed += DemoVoice::FrameBytes;
    }
    PendingPCM.RemoveAt(0, Consumed, EAllowShrinking::No);
}

bool UDemoVoiceComponent::IsPacketValid(const FDemoVoicePacket& Packet) const
{
    DEMO_LOG_TICK();
    if (Packet.Size <= 4 || Packet.Size > FDemoVoicePacket::Capacity || Packet.Payload[0] != 1) return false;
    uint16 CompressedBytes = 0; // UE5.4单帧封装：帧数、内部generation、uint16结束偏移；引擎默认ADD_ENTROPY_TO_PACKET=0。
    FMemory::Memcpy(&CompressedBytes, Packet.Payload + 2, sizeof(CompressedBytes));
    return CompressedBytes == Packet.Size - 4; // 任何多帧/错偏移/尾部垃圾都不进入客户端解码器。
}

void UDemoVoiceComponent::ServerSetTransmission_Implementation(bool bOpen, uint32 Epoch)
{
    DEMO_LOG_CALL();
    const ADemoPlayerController* PC = GetVoiceController(); // 只从PS.Owner解析，不能冒充其他讲话者。
    const ADemoGameState* State = GetWorld()->GetGameState<ADemoGameState>(); // 大厅未登记角色不允许转发语音。
    const double Now = FPlatformTime::Seconds(); // 服务端独立节流，客户端输入限制不能替代权限校验。
    if (!bOpen)
    {
        if (Epoch == ServerEpoch && bServerOpen) { bServerOpen = false; MulticastTransmission(false, Epoch); }
        return;
    }
    if (!PC || !PC->IsCoopRegistered() || !State || State->Phase == EDemoPhase::Lobby || Now < ServerNextStartAt
        || Epoch == 0 || static_cast<int32>(Epoch - ServerEpoch) <= 0)
    { LogFailure(TEXT("server rejected transmission start")); return; }
    ServerNextStartAt = Now + .1; ServerEpoch = Epoch; ServerSequence = 0; bServerOpen = true;
    FrameTokens = 6; ServerBudgetAt = Now;
    MulticastTransmission(true, Epoch);
}

void UDemoVoiceComponent::ServerVoiceFrame_Implementation(const FDemoVoicePacket& Packet)
{
    DEMO_LOG_TICK();
    const ADemoPlayerController* PC = GetVoiceController(); // 每帧校验登记仍有效，断线注销后不能继续转发。
    if (!bServerOpen || !PC || !PC->IsCoopRegistered() || Packet.Epoch != ServerEpoch
        || static_cast<int16>(Packet.Sequence - ServerSequence) <= 0 || !IsPacketValid(Packet))
    { LogFailure(TEXT("server rejected stale/unregistered/malformed frame")); return; }
    const double Now = FPlatformTime::Seconds(); // 55帧/秒+6帧突发，允许50Hz设备与网络轻微抖动。
    FrameTokens = FMath::Min(6.f, FrameTokens + static_cast<float>(Now - ServerBudgetAt) * 55.f); ServerBudgetAt = Now;
    if (FrameTokens < 1) { LogFailure(TEXT("server voice rate exceeded")); return; }
    FrameTokens -= 1; ServerSequence = Packet.Sequence;
    MulticastVoiceFrame(Packet);
}

void UDemoVoiceComponent::MulticastTransmission_Implementation(bool bOpen, uint32 Epoch)
{
    DEMO_LOG_CALL();
    const ADemoPlayerController* PC = GetVoiceController(); // 拥有者不回放自身音频，也不能被远端Begin清掉刚采到的嘴型。
    if ((PC && PC->IsLocalController()) || GetNetMode() == NM_DedicatedServer) return;
    if (bOpen)
    {
        ResetPlayback(); RemoteEpoch = Epoch; RemoteSequence = 0; bRemoteOpen = true;
        if (Decoder.IsValid()) Decoder->Reset();
    }
    else if (Epoch == RemoteEpoch) { bRemoteOpen = false; ResetPlayback(); }
}

FSoundAttenuationSettings UDemoVoiceComponent::MakePlaybackAttenuation() const
{
    DEMO_LOG_CALL();
    const float MaxDistance = FMath::Clamp(FMath::IsFinite(VoiceMaxDistance) ? VoiceMaxDistance : 3000.f, 1.f, 100000.f); // 配置单位cm，非有限最大距离回退30m，避免无限可听范围。
    const float InnerRadius = FMath::Clamp(FMath::IsFinite(VoiceInnerRadius) ? VoiceInnerRadius : 300.f, 0.f, MaxDistance - 1.f); // 保证正的衰减长度，错误配置不能除零。
    if (MaxDistance != VoiceMaxDistance || InnerRadius != VoiceInnerRadius)
        UE_LOG(LogFPSDemo, Warning, TEXT("VOICE_SPATIAL config corrected inner_cm=%.1f max_cm=%.1f"), InnerRadius, MaxDistance);
    FSoundAttenuationSettings Settings; // 本调用拥有的引擎值，AudioComponent复制后由音频线程安全使用。
    Settings.bAttenuate = true; Settings.bSpatialize = true;
    Settings.AttenuationShape = EAttenuationShape::Sphere;
    Settings.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
    Settings.AttenuationShapeExtents = FVector(InnerRadius, 0.f, 0.f);
    Settings.FalloffDistance = MaxDistance - InnerRadius; // UE此字段是满音量球之外的区间长度，不是最大可听半径。
    Settings.SpatializationAlgorithm = SPATIALIZATION_Default; // 引擎声道定位，无额外HRTF插件依赖；方向随听者当前镜头旋转。
    Settings.NonSpatializedRadiusStart = 0.f; Settings.NonSpatializedRadiusEnd = 0.f; // 满音量区仍保留方向，不混为近距离2D声音。
    Settings.bAttenuateWithLPF = false; Settings.bEnableOcclusion = false; Settings.bEnableReverbSend = false; // 本轮仅距离/方向，避免额外过滤或混响影响语音清晰度。
    return Settings;
}

bool UDemoVoiceComponent::UpdatePlaybackLocation()
{
    DEMO_LOG_TICK();
    const APlayerState* Speaker = Cast<APlayerState>(GetOwner()); // PS拥有语音身份；远端不依赖本机不可见的PC或其他玩家ViewTarget。
    const APawn* Pawn = Speaker ? Speaker->GetPawn() : nullptr; // 每次重新解析，死亡原Pawn仍有效，换Pawn后立即跟随新身体。
    if (!Playback || !IsValid(Pawn))
    {
        if (bHasPlaybackLocation) UE_LOG(LogFPSDemo, Log, TEXT("VOICE_SPATIAL source unavailable; discard playback until pawn returns"));
        bHasPlaybackLocation = false;
        return false; // 首次Pawn尚未复制也丢弃当前帧，不从PS默认原点或听众头部发声。
    }
    const FVector Location = Pawn->GetPawnViewLocation(); // 角色眼部高度近似发声位置；不是观战相机，阵亡发声保留在原身体位置。
    if (Location.ContainsNaN()) { bHasPlaybackLocation = false; LogFailure(TEXT("invalid spatial source position")); return false; }
    Playback->SetWorldLocation(Location); // 显式世界坐标更新，避免将音频挂到销毁后仍缓存的旧Pawn组件。
    if (!bHasPlaybackLocation) UE_LOG(LogFPSDemo, Log, TEXT("VOICE_SPATIAL source available"));
    bHasPlaybackLocation = true;
    return true;
}

bool UDemoVoiceComponent::PreparePlayback()
{
    DEMO_LOG_CALL();
    NextPlaybackRetryAt = FPlatformTime::Seconds() + 2.; // 失败由后续帧按此期限重试，不循环分配设备或打印初始化入口。
    if (!Decoder.IsValid()) Decoder = FVoiceModule::Get().CreateVoiceDecoder(DemoVoice::SampleRate, 1);
    if (!Decoder.IsValid()) { LogFailure(TEXT("decoder unavailable")); return false; }
    if (!VoiceWave)
    {
        VoiceWave = NewObject<USoundWaveProcedural>(this);
        VoiceWave->SetSampleRate(DemoVoice::SampleRate); VoiceWave->NumChannels = 1;
        VoiceWave->Duration = INDEFINITELY_LOOPING_DURATION;
        VoiceWave->SoundGroup = SOUNDGROUP_Voice;
        VoiceWave->VirtualizationMode = EVirtualizationMode::PlayWhenSilent; // 距离外继续消费实时FIFO；走近不会补播已过时的远处讲话，人数仍限2～4人。
        Playback = NewObject<UAudioComponent>(GetOwner()); // 身份/生命周期归PS，UpdatePlaybackLocation负责取当前Pawn位置。
        GetOwner()->AddInstanceComponent(Playback);
        Playback->bAutoActivate = false; Playback->bAutoDestroy = false;
        Playback->bAllowSpatialization = true;
        Playback->bIsUISound = true; // 仅保留菜单/暂停期间听队友的策略；UE此标记不关闭空间化或距离衰减。
        Playback->bOverrideAttenuation = true;
        Playback->AttenuationOverrides = MakePlaybackAttenuation();
        const float Volume = FMath::Clamp(FMath::IsFinite(PlaybackVolume) ? PlaybackVolume : 1.f, 0.f, 2.f); // 基础音量防非法INI；距离音量交UE混音器单独计算。
        if (Volume != PlaybackVolume) UE_LOG(LogFPSDemo, Warning, TEXT("VOICE_SPATIAL playback volume corrected to %.2f"), Volume);
        Playback->SetVolumeMultiplier(Volume);
        Playback->RegisterComponent(); Playback->SetSound(VoiceWave);
        UE_LOG(LogFPSDemo, Log, TEXT("VOICE_SPATIAL playback ready inner_cm=%.1f max_cm=%.1f"),
            Playback->AttenuationOverrides.AttenuationShapeExtents.X, Playback->AttenuationOverrides.AttenuationShapeExtents.X + Playback->AttenuationOverrides.FalloffDistance);
    }
    return Playback != nullptr;
}

void UDemoVoiceComponent::MulticastVoiceFrame_Implementation(const FDemoVoicePacket& Packet)
{
    DEMO_LOG_TICK();
    const ADemoPlayerController* PC = GetVoiceController(); // Listen房主本人的声也必须跳过本地回放。
    if ((PC && PC->IsLocalController()) || GetNetMode() == NM_DedicatedServer || !FApp::CanEverRender()) return;
    if (!bRemoteOpen || Packet.Epoch != RemoteEpoch || static_cast<int16>(Packet.Sequence - RemoteSequence) <= 0 || !IsPacketValid(Packet)) return;
    if (!Decoder.IsValid() || !Playback)
    {
        if (FPlatformTime::Seconds() < NextPlaybackRetryAt || !PreparePlayback()) return;
    }
    // UE5.4解码器要求6帧容量，即使输入仅1帧；严格要求解码结果20ms，恶意Opus时长不可扩张FIFO。
    int16 Decoded[DemoVoice::FrameSamples * 6] = {}; // 对齐的固定栈缓冲，最多120ms，不依赖网络内声明的长度分配。
    uint32 DecodedBytes = sizeof(Decoded); // Decode输入容量/输出实际PCM字节数。
    Decoder->Decode(Packet.Payload, Packet.Size, reinterpret_cast<uint8*>(Decoded), DecodedBytes);
    RemoteSequence = Packet.Sequence;
    if (DecodedBytes != DemoVoice::FrameBytes) { LogFailure(TEXT("decoder rejected duration/format")); return; }
    LastReceivedAt = FPlatformTime::Seconds();
    MeasureSpeech(Decoded, DemoVoice::FrameSamples); // 嘴型取原始PCM；距离只影响听众音量，远处看见的讲话动作仍正确。
    if (!UpdatePlaybackLocation())
    {
        if (Playback->IsPlaying()) ResetPlayback(); // 当前无合法声源时不排入FIFO，Pawn恢复不能补播丢失期间的语音。
        return;
    }
    if (VoiceWave->GetAvailableAudioByteCount() + DecodedBytes > DemoVoice::MaxQueuedBytes) VoiceWave->ResetAudio();
    VoiceWave->QueueAudio(reinterpret_cast<const uint8*>(Decoded), DecodedBytes);
    if (!Playback->IsPlaying()) Playback->Play();
}

void UDemoVoiceComponent::MeasureSpeech(const int16* Samples, int32 Count)
{
    DEMO_LOG_TICK();
    double Squares = 0; // double累计防int16乘方溢出；归一化幅值0..1。
    for (int32 Index = 0; Index < Count; ++Index) // 仅当前固定320样本同步读取。
    { const double Sample = Samples[Index] / 32768.; Squares += Sample * Sample; } // Sample只在本循环存在。
    const float RMS = Count > 0 ? static_cast<float>(FMath::Sqrt(Squares / Count)) : 0.f; // 均方根抗孤立尖峰，比峰值更适合嘴型。
    const float Threshold = FMath::Clamp(SpeechThreshold, .001f, .2f); // ini不可信范围钳制。
    if (RMS >= Threshold)
    {
        SpeechAmount = FMath::Clamp(RMS / FMath::Max(Threshold + .001f, FullOpenLevel), .2f, 1.f);
        SpeechUntil = FPlatformTime::Seconds() + .12; // 跨音节短间隙平滑，停止或静音后自动闭嘴。
    }
}

float UDemoVoiceComponent::GetMouthOpenAmount() const
{ DEMO_LOG_TICK(); return FPlatformTime::Seconds() < SpeechUntil ? SpeechAmount : 0.f; }

FString UDemoVoiceComponent::GetLocalStatus() const
{
    DEMO_LOG_TICK();
    if (!DeviceError.IsEmpty()) return DeviceError;
    if (bCapturing) return GetMouthOpenAmount() > 0 ? TEXT("T · 正在说话") : TEXT("T · 麦克风已开启");
    return TEXT("按住 T · 附近队伍语音"); // 明确距离限制，避免玩家把超出范围静音误当连接故障。
}
bool UDemoVoiceComponent::HasActiveLocalStatus() const
{ DEMO_LOG_TICK(); return bCapturing||!DeviceError.IsEmpty(); }

void UDemoVoiceComponent::ResetPlayback()
{
    DEMO_LOG_CALL();
    if (Playback) Playback->Stop();
    if (VoiceWave) VoiceWave->ResetAudio();
    SpeechUntil = 0; SpeechAmount = 0;
}

void UDemoVoiceComponent::LogFailure(const TCHAR* Reason)
{
    DEMO_LOG_TICK();
    const double Now = FPlatformTime::Seconds(); // 同一PS两秒只记录一次异常，不打印PCM、包体或身份。
    if (Now >= NextFailureLogAt) { NextFailureLogAt = Now + 2.; UE_LOG(LogFPSDemo, Warning, TEXT("VOICE_REJECT %s"), Reason); }
}

void UDemoVoiceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    StopTalking(); ResetPlayback();
    if (Playback) Playback->DestroyComponent();
    Playback = nullptr; VoiceWave = nullptr; Decoder.Reset(); bHasPlaybackLocation = false; // World卸载不保留旧源位置或Pawn引用。
    Super::EndPlay(EndPlayReason);
}
