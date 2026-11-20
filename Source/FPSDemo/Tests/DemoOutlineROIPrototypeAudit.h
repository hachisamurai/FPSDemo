// 职责：T03显式Editor独立Game的full-view参考夹具；默认真实斜枪，可选原E租约/IA_Fire按钮按压，均消费Root桥。
// 使用：默认斜枪1次RT/显式时序16+12脉冲；可选Keycap六组press_on16RT，加fixed_on1或显式16（总17/32），最多2PNG。
// 边界：不改A08/正式资源/地图/输入/经济/云，不造Mask；同帧CPU提交与Alpha写入不等GPU帧身份或整体验收。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoOutlineROIPrototypeAudit.generated.h"

class ADemoCharacter;
class ADemoPlayerController;
class ADemoGunDisplay;
class ADemoCasinoDevice;
class UDemoCasinoDeviceProfile;
class UInputAction;
class UPrimitiveComponent;
class UMaterialInstanceDynamic;
class UDemoOutlineROIPrototype;
class FJsonObject;
class FJsonValue;

/** 普通Editor/PIE/非Editor不创建；源与Mask由正常生产Focus创建，Keycap仅原E/金额键手势，无购买/下注。 */
UCLASS()
class FPSDEMO_API UDemoOutlineROIPrototypeAudit : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 仅Editor显式-game/Standalone/真实持久白模地图，旧A08夹具参数同时存在则拒绝创建。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** 仅独立Game World支持，排除临时启动World/PIE。 */
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    /** 只建立Root桥/原Hub注册依赖，不加载/编辑候选资产。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** 建立独占证据目录及弱World委托；实际渲染必须由专用Viewport产生。 */
    virtual void OnWorldBeginPlay(UWorld& World) override;
    /** 精确清自己的委托/请求并让桥还生产MID，不取消其它截图或清其它PP。 */
    virtual void Deinitialize() override;
private:
#if WITH_EDITOR
    /** 本World正常末帧推进有限状态，90秒/磁盘门及所有失败锁存。 */
    void OnPostTick(UWorld* World,ELevelTick Type,float Delta);
    /** 世界意外清理立即以未验证收尾，不允许拿旧绑定继续采样。 */
    void OnWorldCleanup(UWorld* World,bool SessionEnded,bool CleanupResources);
    /** Message为有限诊断；首错锁存，截图广播只锁存、下一Tick才结束。 */
    void Fail(const FString& Message);
    /** 实测10GiB余量及证据含abslog<=64MiB，不删除旧文件。 */
    bool Budget() const;
    /** 真Hub/当前目标唯一生产253mask；斜枪核模型材料LeaderPose/Visibility，Keycap核原Operate与动态键源。 */
    bool ReadTarget(FString& Error) const;
    /** Keycap复用实体Input的Health/Eliminated/Hub/UI正向Operate门及真实租约/LocalView/digit0；技能许可与Operate互斥，不能用于此处。 */
    bool ReadKeyTarget(FString& Error) const;
    /** 每GT末帧向原EnhancedPlayerInput注入现IA_Fire bool，走原Started/Completed；完整手势后归零。 */
    bool InjectKeyPrimary();
    /** 每GT读取实际Key/MASK峰/回Rest及15个原Query不动；不取动画求值目标或改源姿态。 */
    void ReadKeyMotion();
    /** Keycap独立E租约/Focus准备状态11..14，结束后并入原Root桥绑定状态2。 */
    void AdvanceKeyPreparation();
    /** 原A07正常瞄准收敛：仅deadline内失败时以本帧真实SharedEye重新朝原ControlPoint转向，不改眼位/Query/容差。 */
    void ReaimKeyControl(FName ControlId,const FString& Reason);
    /** ReadTarget外追加同Pawn/源/mask、真实眼位/FOV与计划yaw；不调用CalcCamera或手动更新生产视图。 */
    bool ReadView(FString& Error) const;
    /** 正常绘制LDR截图回调；逐次核Root桥同帧/代/Owner/实际捕获AA及提交证据，只存标量。 */
    void OnScreenshot(int32 Width,int32 Height,const TArray<FColor>& Bitmap);
    /** fixed_on默认1/显式16；枪另pulse12，Keycap另press_on16；Frame为已核同帧证据，无原始RGBA输出。 */
    bool ReadRTWitness(const TArray<FColor>& MainBitmap,const TSharedPtr<FJsonObject>& Frame,bool Pulse=false);
    /** 仅显式fixed_on16帧累积二值R的one-runs/CRC、相邻XOR及频次；不使用主图混白/颜色CRC判稳定。 */
    bool RecordTemporalR(const TArray<uint8>& Binary,const TSharedPtr<FJsonObject>& Witness);
    /** press_on16个真实移动组RT读回只记RLE/XOR与实际源姿态，运动XOR不作为稳定失败。 */
    bool RecordPressR(const TArray<uint8>& Binary,const TSharedPtr<FJsonObject>& Witness);
    /** 完成16帧后输出完整count-runs/flip-runs和总和不变量，变化只记录不调shader/门限。 */
    bool FinalizeTemporalR();
    /** 四组后固定视向自然settle<=2秒，再Composite常开、Mask0/2/0各4回调；第一响应不跳帧/暖机。 */
    void AdvancePulse();
    /** 只在上次截图广播已Reset后请求下一张；每组>=1真实秒且8..256有效暖机，再16有效测量。 */
    void AdvanceSamples();
    /** Name为两张固定证据名；解除内存截图委托后才正常落PNG，不扩图预算。 */
    bool RequestPNG(const FString& Name);
    /** 精确撤本截图句柄/匹配Filename的自有请求，不取消其它截图。 */
    void StopMemoryCapture();
    /** Success仅表示列明参考合同有观测；报告仍声明白2px/完整边/Owner/遮挡/成本等独立未验。 */
    void Finish(bool Success);

    TWeakObjectPtr<ADemoCharacter> Player; // 当前真实独立游戏Pawn，不创建替身。
    TWeakObjectPtr<ADemoPlayerController> Controller; // 本机原控制器，仅显式Keycap正常E及现IA_Fire事件，不直接路由交易。
    TWeakObjectPtr<ADemoGunDisplay> Display; // 固定gunshop_rifle_01，须DA WeaponId=rifle。
    TWeakObjectPtr<ADemoCasinoDevice> KeyDevice; // Keycap固定原Mode1实体老虎机，E取得原租约。
    TWeakObjectPtr<UDemoCasinoDeviceProfile> KeyProfile; // 原Controls持有的可信Profile，只读键PartId/rest及原Query。
    TWeakObjectPtr<UInputAction> KeyFireAction; // 仅Keycap加载既有IA_Fire，不创建新Action/Mapping。
    FName KeyPart; // amount.digit.0的实际动态PartId。
    TArray<FName> KeyQueryIds; TArray<FVector> KeyQueryRest; // 原15个Query点进入前快照，动画不得移动根查询。
    TArray<TSharedPtr<FJsonValue>> PressRFrames; TArray<uint8> PreviousPressR; // press_on16个实际RT见证与上一二值R，不混入固定16频次。
    FString KeyAmountBefore,KeyAmountAfter; // 金额草稿由数字0正常输入改变，单列于钱包/Serial无交易事实。
    double KeyPressStarted=0.,KeyPressPeak=0.,KeyPressTravel=0.,KeyMaxTravel=0.,KeyQueryMaxShift=0.; // 单调按压时刻/实际Key位移cm与固定Query最大变化cm。
    int32 KeyGestures=0,KeyMovingRTSamples=0,KeyGTMovingSamples=0,KeyRequestBefore=0; // 两press组各一次手势，移动GT/RT计数及原金额键不得发经济请求。
    int32 KeyAimCorrections=0; TSharedPtr<FJsonObject> KeyAimFailureFacts; // pull/digit0各保首次实际拒绝射线/首命中纯值与总校正次数，不逐帧输出。
    double KeyRayStableSinceWorld=-1.,KeyRayStableNeededWorld=0.,KeyRayStableElapsedWorld=0.; // Stage12连续真实ValidateHit的World秒；失效清零，等待DA稳定秒+2次扫描+1个60Hz帧裕量，不延2real秒门。
    uint64 KeyETriggerFrame=0; // 唯一正常E触发的实际GFrameCounter，配合稳定dwell供复核，不刷E。
    bool bKeycap=false,bKeyDown=false,bKeyGestureStarted=false,bKeyOwnedGesture=false; // 显式Target Keycap，按/释状态与真实Device手势所有权。
    TWeakObjectPtr<UPrimitiveComponent> Source,Mask; // 原展示或实际动态键源与生产253副本，锁定对象身份。
    TWeakObjectPtr<UMaterialInstanceDynamic> ProductionMID; // Root桥借用的原A04相机MID，清理后核原width有效值恢复。
    TWeakObjectPtr<UDemoOutlineROIPrototype> Bridge; // Root World服务，夹具只调用其公开接口。
    FDelegateHandle TickHandle,CleanupHandle,ScreenshotHandle; // 本夹具独占句柄，结束只Remove自己。
    TArray<TSharedPtr<FJsonValue>> Samples,Warmups,Groups; // 斜枪四组/键帽六组各16测量及最多256暖机的有限证据。
    TArray<FString> PNGs; // 正常PNG至多2张，整个目录仍4图硬预算。
    TSharedPtr<FJsonObject> RTWitness; // 唯一RT读回的通道/主图纯白统计，不存原始像素。
    TSharedPtr<FJsonObject> LastRTWitness; // 显式时序/脉冲失败也保最后一次真实RT通道与旧边沿probe事实，不把失败样本塞进16个有效频次。
    TArray<TSharedPtr<FJsonValue>> TemporalRFrames,PulseSamples,PulseGroups; // 显式模式16个R见证与3×4脉冲，同原四组分开登记。
    TSharedPtr<FJsonObject> TemporalRSummary; // 921600格频次/翻转的有界RLE汇总，不保存原始RGBA。
    TArray<uint8> PreviousTemporalR,TemporalRCounts,TemporalRFlipCounts; // 仅显式模式分配，各921600字节；二值前图/16帧频次0..16/翻转0..15。
    TArray<FColor> FixedOffBaseline; // 显式模式fixed_off首测主图约3.7MB内存，不输出原图，用于识别旧白沿的真实暗背景点。
    TArray<int32> PulseProbeIndices; // 从fixed_on真实R255且baseline.maxRGB<=200均匀选<=64点，脉冲0必须不近白，2必须精确255。
    TSharedPtr<FJsonObject> PulseProbeEvidence; // probe索引RLE、数量/来源帧及选择条件，不能把天然白背景当阴性。
    uint64 FixedOffBaselineFrame=0; // 实际fixed_off首测绘制帧，非GPU帧号。
    FString Directory,Failure,OwnedRequest,PendingPNG; // 独占输出/首错/自有截图关联名与待写入正常PNG。
    int32 Stage=0,Group=0,ValidCallbacks=0,RejectedCallbacks=0,InitialCoins=0,InitialSerial=0; // 有限状态/四组索引/观测及经济基线。
    int32 PulseIndex=0,RTReadbacks=0; // 枪默认1/时序28；Keycap默认17/时序32，无pulse；全部有限正常回调读回。
    uint32 Generation=0; // Root桥首次有效绑定代，所有采样须相同。
    uint64 RequestSerial=0,CompletedSerial=0,RequestFrame=0,LastCallbackFrame=MAX_uint64,LastTickFrame=MAX_uint64,PNGFrame=0; // 请求与实际绘制回调顺序，不能冒认外来图。
    double RequestPhaseSeconds=0.; int32 RequestGroup=0; bool bRequestMeasured=false; // 真正RequestScreenshot前冻结phase elapsed/组/测量资格；回调处理耗时不能把warm升级为测量，pulse绕过warm。
    double Started=0.,WaitStarted=0.,BindStarted=0.,GroupStarted=0.,RequestedAt=0.,NextSample=0.,YawChangedAt=0.,PNGRequestedAt=0.; // 全部真实单调秒：15秒shader等候/3秒截图/2秒POV/90秒总门。
    double YawOffset=0.,ObliqueAngle=0.,FocusDistance=0.; // 16个±.15deg计划yaw及实际斜枪角度/距离cm。
    float OriginalWidth=0.f,ReferenceFOV=0.f; // 原生产MID有效width与实际最终Camera FOV，退出不修改材质默认值。
    FVector AimPoint,PawnReference; // 实际注册Focus中心/当前地面Pawn位置cm。
    FTransform SourceReference,MaskReference,CameraReference,POVReference; // 目标真实Rest源/mask及固定视图基准；仅Keycap press放开同源运动，不锁生产相机。
    FRotator OriginalRotation; // 慢yaw完成后恢复原ControlRotation。
    bool bFailed=false,bFinished=false,bPending=false,bCallbackReady=false,bBound=false,bRTAttempted=false,bRTValid=false,bCleanupVerified=false; // 幂等退出/在途图/Root绑定/一次RT及有限清理证据。
    bool bTemporalWitness=false,bPulseCompleted=false,bPulseMaskBorrowed=false,bPulseMaskRestored=false; // 仅显式参数启用；最后Mask2必须恢复并由实际桥帧再次观察。
    bool bWorldTeardown=false; // 清理/卸载只关闭仍属本Pawn+KeyDevice的本地租约，不发送旧World ServerRelease或触碰新设备。
#endif
};
