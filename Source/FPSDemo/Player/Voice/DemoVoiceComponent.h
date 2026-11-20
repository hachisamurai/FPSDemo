// 职责：PS拥有的按住说话、Opus房主转发与3D距离语音。使用：T采集，远端声源跟随PS当前Pawn；依赖UE Voice/Audio，游戏线程管理、音频线程消费FIFO，不录音/存档。
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DemoVoiceComponent.generated.h"

class IVoiceCapture;
class IVoiceEncoder;
class IVoiceDecoder;
class UAudioComponent;
class USoundWaveProcedural;
class ADemoPlayerController;
class UPackageMap; // 网络序列化签名依赖；本协议不传UObject引用。
struct FSoundAttenuationSettings; // 播放时构建引擎衰减值，无需创建或写入SoundAttenuation资产。

/** 固定容量避免RPC反序列化按客户端长度分配内存；格式锁定本项目UE5.4的单帧Opus封装。 */
USTRUCT()
struct FDemoVoicePacket
{
    GENERATED_BODY()
    // 每包最多256字节压缩数据；16kHz/单声道/20ms/24kbps通常约60字节，禁止分片大包。
    static constexpr uint16 Capacity = 256;
    uint32 Epoch = 0; // 本PS每次按下递增，迟到的上一段语音不可重新打开嘴巴或播放。
    uint16 Sequence = 0; // 当前按下周期内的帧序号，按模65536比较，丢包不重传。
    uint16 Size = 0; // 有效压缩字节数，网络先校验0..Capacity再读Payload。
    uint8 Payload[Capacity] = {}; // 值类型包体，无UObject引用，RPC结束即可销毁。
    /** Ar为当前网络档案，Map不用（无对象引用），bOutSuccess报告边界错误；高频序列化不输出常开日志。 */
    bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};
template<> struct TStructOpsTypeTraits<FDemoVoicePacket> : TStructOpsTypeTraitsBase2<FDemoVoicePacket>
{ enum { WithNetSerializer = true }; };

/** 语音随PS/连接存在，Pawn死亡或重生不改变讲话者身份；不把语音放入GAS或存档。 */
UCLASS(ClassGroup=(Demo), Config=Game, DefaultConfig)
class FPSDEMO_API UDemoVoiceComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    /** 创建可复制子对象；没有按T时不创建采集设备。 */
    UDemoVoiceComponent();
    /** Enhanced Input Started调用，仅本人、已登记且游戏视口聚焦时打开默认麦克风。 */
    void StartTalking();
    /** 松键/取消/菜单/失焦/旅行调用；清空缓存，可靠通知远端停止，必须重新按T才能恢复。 */
    void StopTalking();
    /** 每帧只读0..1实际音量驱动开口，超过短暂静音期限自动闭合；不是“按键已按下”状态。 */
    float GetMouthOpenAmount() const;
    /** HUD借用本机提示（包含无设备/权限失败），仅本地，不包含录音内容或账号ID。 */
    FString GetLocalStatus() const;
    /** 本机HUD查询是否确在采集或有设备错误；仅用于按需显示，不打开麦克风、不改变权限/复制。 */
    bool HasActiveLocalStatus() const;
    /** DeltaTime为秒、TickType/TickFunction为引擎调度；20ms轮询采集/超时并跟随讲话Pawn，暂停仍能关麦。 */
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    /** EndPlayReason为卸载原因；关闭物理麦克风、音频组件与所有内存缓存，不跨World保留。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend class FDemoVoiceSpatialTest; // 仅自动化可检查实际播放配置/位置；不开放蓝图、控制台或网络注入音频接口。
#endif
    /** bOpen为按键边沿，Epoch为本人单调周期；服务器只接受Owner连接并检查登记，不接受外部讲话者ID。 */
    UFUNCTION(Server, Reliable) void ServerSetTransmission(bool bOpen, uint32 Epoch);
    /** Packet为最多20ms语音；服务器验证封装/序号/限速后用现有LAN或EOS游戏连接转发。 */
    UFUNCTION(Server, Unreliable) void ServerVoiceFrame(const FDemoVoicePacket& Packet);
    /** 可靠周期边沿；bOpen=false立即丢弃尾部缓存，Epoch防止旧包在松键后复活。 */
    UFUNCTION(NetMulticast, Reliable) void MulticastTransmission(bool bOpen, uint32 Epoch);
    /** Packet在每个听众本机解码；拥有者跳过回放，专服不创建声音或麦克风。 */
    UFUNCTION(NetMulticast, Unreliable) void MulticastVoiceFrame(const FDemoVoicePacket& Packet);
    /** 返回Owner PS对应控制器的同步借用；远端客户端通常没有该PC，允许为空。 */
    ADemoPlayerController* GetVoiceController() const;
    /** 本机输入/身份/菜单/视口门禁；无需Pawn存活，观战者仍可与队友交流。 */
    bool CanCapture() const;
    /** 从设备取出PCM并切20ms帧；只在按住T期间调用，每Tick最多发送4帧，积压丢弃而非补发。 */
    void CaptureFrames();
    /** Packet必须是一帧UE5.4 Opus（4字节头），包含长短包/偏移检查，不调用解码器。 */
    bool IsPacketValid(const FDemoVoicePacket& Packet) const;
    /** Samples为本次同步借用16bit PCM，Count为样本数；RMS超过阈值时更新短暂开口状态。 */
    void MeasureSpeech(const int16* Samples, int32 Count);
    /** 首个远端包到达时创建独立解码器/3D音频；线性距离衰减，失败不影响游戏或启动采集。 */
    bool PreparePlayback();
    /** 创建播放源时把厘米配置规范化为球形线性衰减；越界/非有限值修正并记录，最大距离至少1cm。 */
    FSoundAttenuationSettings MakePlaybackAttenuation() const;
    /** 首帧播放前及播放期间50Hz调用；从PS当前Pawn取眼部世界位置，无Pawn/非法位置返回false，禁止原点或2D兜底。 */
    bool UpdatePlaybackLocation();
    /** 停止远端播放并清除形变源；只操作本PS自己的队列，不修改全局音频/OSS。 */
    void ResetPlayback();
    /** Reason为固定诊断代码；拒绝/错误最多每2秒记录一次，防网络异常造成日志洪泛。 */
    void LogFailure(const TCHAR* Reason);

    // 归本PS本地实例的采集/编码器；只在按T之后创建，松键销毁以释放设备，其他进程不可借用。
    TSharedPtr<IVoiceCapture> Capture;
    TSharedPtr<IVoiceEncoder> Encoder;
    TSharedPtr<IVoiceDecoder> Decoder; // 每远端讲话者一个解码器，线程只限游戏线程。
    UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> VoiceWave; // 强引用保活PCM FIFO，最大排队120ms。
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> Playback; // 同PS拥有的3D声源，位置跟随当前Pawn；不挂旧Pawn，重生无需重建连接。
    bool bHasPlaybackLocation = false; // 本听众是否曾解析当前有效声源；只用于源丢失/恢复日志去重，不复制或存档。
    TArray<uint8> CaptureBuffer; // 单次设备读取最多0.5秒，按键结束Reset；不保存磁盘。
    TArray<uint8> PendingPCM; // 不足20ms的余数；积压最多4帧+余数，处理后仅保留余数。
    bool bCapturing = false; // 本机实际设备已成功Start，不复制、不等于正在发声。
    uint32 LocalEpoch = 0; // 本地按键周期，随PS重建归零；不写入进度。
    uint16 SendSequence = 0; // 当前按键内已发送帧水位。
    double CaptureStartedAt = 0; // 单调秒；单次最长60秒自动关麦，避免丢失松键后常开。
    double NextStartAt = 0; // 单调秒；100ms按下节流，限制可靠边沿请求。
    FString DeviceError; // HUD持续提示最近采集失败，下次主动按T清除重试。
    bool bServerOpen = false; // 服务器本PS的发送许可；只有可靠Begin才能置位。
    uint32 ServerEpoch = 0; // 服务器当前周期；忽略旧周期数据与结束消息。
    uint16 ServerSequence = 0; // 服务端已接受最新帧，拒绝乱序/重复。
    double ServerBudgetAt = 0; // 帧令牌更新时间，单调秒不受暂停影响。
    double ServerNextStartAt = 0; // 服务器独立100ms边沿限速，不信任客户端节流。
    float FrameTokens = 6; // 容许6帧突发，持续最多55包/秒；正常50Hz。
    bool bRemoteOpen = false; // 本听众是否接受当前广播周期，Stop后任何迟到帧都丢弃。
    uint32 RemoteEpoch = 0; // 本听众当前可靠周期，不依赖属性复制与RPC到达的先后。
    uint16 RemoteSequence = 0; // 本听众已消费水位；丢包直接跳过，无重传/无限等待。
    double LastReceivedAt = 0; // 单调秒；超过250ms未收到语音清除残留队列，嘴巴更早自动关闭。
    double NextPlaybackRetryAt = 0; // 解码/播放初始化失败最多2秒重试一次，避免每个网络包触发模块初始化和调用日志。
    double SpeechUntil = 0; // 实际有声帧后120ms闭嘴，设备静音/断网也会自动归零。
    float SpeechAmount = 0; // 上一有声帧0..1开口程度，本机计算，不额外逐帧复制。
    double NextFailureLogAt = 0; // 每实例异常节流时钟，避免网络垃圾触发逐帧日志。
    // 正规化PCM RMS门槛0..1，约-42dB；仅控制嘴型，不剪断低音量语音，按设备噪声可调。
    UPROPERTY(Config, EditDefaultsOnly, Category="Voice", meta=(ClampMin="0.001", ClampMax="0.2")) float SpeechThreshold = .008f;
    // RMS达到此值时最大张嘴，需大于SpeechThreshold；不是麦克风增益。
    UPROPERTY(Config, EditDefaultsOnly, Category="Voice", meta=(ClampMin="0.01", ClampMax="1")) float FullOpenLevel = .08f;
    // 本机远端语音基础乘数0..2，再乘UE距离衰减；不复制，默认1保持近处录入音量。
    UPROPERTY(Config, EditDefaultsOnly, Category="Voice", meta=(ClampMin="0", ClampMax="2")) float PlaybackVolume = 1.f;
    // 满音量球半径，单位cm，默认300=3m；实际钳制到0..MaxDistance-1，不复制，所有端应使用同一发行配置。
    UPROPERTY(Config, EditDefaultsOnly, Category="Voice|Spatial", meta=(ClampMin="0", ClampMax="99999", Units="cm")) float VoiceInnerRadius = 300.f;
    // 声源至听者的最远距离（非衰减区长度），单位cm；默认3000=30m，合法1..100000，达到边界音量为0。
    UPROPERTY(Config, EditDefaultsOnly, Category="Voice|Spatial", meta=(ClampMin="1", ClampMax="100000", Units="cm")) float VoiceMaxDistance = 3000.f;
};
