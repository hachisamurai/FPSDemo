// 职责：Editor显式单进程的第一人称拖影/投影隔离；同一路径分别关闭模糊、投影、拥有者分区及时间AA，只写Saved，不改正式设置或Content。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoViewGhostAudit.generated.h"

class ADemoCharacter;
class ADemoPlayerController;
class UPrimitiveComponent;
class USkeletalMeshComponent;

UCLASS(Transient,NotBlueprintable)
class ADemoViewGhostAudit : public AActor
{
    GENERATED_BODY()
public:
    /** 创建最终动画之后的采样器；仅-DemoViewGhostAudit且WITH_EDITOR由Bootstrap生成。 */
    ADemoViewGhostAudit();
    /** 同一60Hz输入路径执行9种独立画面对照；DeltaSeconds为真实World步长，不注入骨姿。 */
    virtual void Tick(float DeltaSeconds) override;
    /** 卸载时解除本World输入委托并释放本专项按键；其他旧夹具不新增输入或设置覆盖。 */
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    /** 保存有限CSV、记录诊断范围并退出独立进程；Success仅表示采样完成，不等于根因或修复验收。 */
    void Finish(bool Success,const TCHAR* Reason);
    /** 在Case切换时设置本测试进程的渲染变量，暖机60帧，保留真正枪械/动画及相机眼位。 */
    void ApplyCase();
    /** 只在清晰的NoBlur低头姿态导出实际CPU线性蒙皮、三角面及dominant bone，辨别近裁剪与变形；不修改网格或骨矩阵。 */
    void DumpSkin(USkeletalMeshComponent* Mesh,const TCHAR* Label);
    /** 仅-DemoViewClipAudit分支：固定LOD0/90度相机，18个俯仰与头角各停1秒，以20Hz请求真实RHI图。 */
    void TickClipReview(float DeltaSeconds);
    /** 在稳定姿态用真实CPU蒙皮检查近裁剪平面切入；只记三角面/区域，不把数值当无穿模验收。 */
    void ProbeClipPose(int32 Pose);
    /** 正式LOD策略启用、无临时LOD/画质覆盖的8秒实际控制路径；含移动/跳跃/近战切换。 */
    void TickRuntimeReview(float DeltaSeconds);
    /** 独立-game稳定眼位专项初始化，保留正式渲染策略；建立PreActor输入及有界证据表。 */
    bool BeginStableViewClipReview();
    /** 仅匹配本World的PreActorTick发送真实控制/跳跃/冲刺按键；不在末帧读取尚未求值的相机或皮肤。 */
    void DriveStableViewClipInput(UWorld* World,ELevelTick TickType,float DeltaSeconds);
    /** PostUpdate只读最终POV与实际Owner皮肤；三案完成仅表示取证完成，不判艺术/整体无穿模通过。 */
    void TickStableViewClipReview(float DeltaSeconds);
    /** 每帧按实际LOD线性蒙皮检查近面和九条前向射线；只保存限量见证OBJ，不逐帧导出全皮肤。 */
    bool ProbeStableViewClipGeometry(USkeletalMeshComponent* Mesh);
    /** 保存本专项组件、资产散列、几何和运行结果；Success只限定输入/采样合同，Reason解释退出原因。 */
    bool SaveStableViewClipReview(bool Success,const TCHAR* Reason);
    /** 退出或销毁时精确撤销委托并释放本专项W/S/D/Shift/Space，不清除其他玩家输入。 */
    void StopStableViewClipInput();
    /** 首次遇到实际资产记录包路径、磁盘SHA1及大小；瞬态动态Montage明确记录无持久包。 */
    void RecordStableViewClipAsset(UObject* Asset);
    TWeakObjectPtr<ADemoCharacter> Player; // 当前本机Pawn，旅行或销毁后立即终止，不能跨World使用。
    TWeakObjectPtr<ADemoPlayerController> Controller; // 本隔离进程唯一拥有者，不复制测试输入。
    FString Directory; // 本次Saved时间戳目录，不能覆盖用户录像或旧诊断。
    TArray<FString> Rows; // 有界1080个画面/姿态条目；不保存逐帧完整网格，仅异常/隔离皮肤快照。
    TArray<FString> ClipProbeRows; // 18个稳定姿态的近裁剪探针；每个实际相交面另存有界证据。
    TMap<TWeakObjectPtr<UPrimitiveComponent>,bool> OriginalShadows; // 初始化冻结本Pawn组件投影值；每Case完整恢复，避免前一消融污染后一阶段。
    FVector StartLocation=FVector::ZeroVector; // 实际Hub落点；本测试只转头，不移动或改碰撞。
    int32 Case=-1,Frame=-60; // Case0..8；每Case先60帧暖机，后120帧同轨迹采样。
    int32 OriginalBlur=3,OriginalAA=4; // 启动时读取实际CVar；每Case从同一基准设置，退出独立进程不写设备配置。
    bool bInitialized=false,bFinished=false; // 保护一次初始化/保存与终止重入。
    bool bClipReview=false; // 仅Editor命令行启用全尺寸静态检查，不改变原阴影对照入口。
    bool bRuntimeReview=false; // 仅Editor正式策略复录：测试Actor不替代组件设置ForcedLOD，渲染设置使用用户原值。
    bool bStableClipReview=false,bStableClipMelee=false; // 显式Editor Standalone专项及独立近战分区选择，不进入普通PIE流程。
    FDelegateHandle StableClipInputHandle; // 本Actor拥有的弱UObject World输入订阅，Finish/EndPlay均精确解绑。
    TArray<FString> StableClipGeometryRows,StableClipRayRows,StableClipComponentRows,StableClipAssetRows,StableClipCaseRows; // 最多1800帧；九条射线/帧及有限组件、资产身份，不存逐帧完整皮肤。
    TSet<FString> StableClipSeenAssets; // 本进程已散列的资产路径，避免同一包逐帧读取磁盘。
    double StableClipCaseStart=-1.,StableClipDashRequestedAt=-1.,StableClipLastDashStart=-100.; // 实际World秒；不重置GAS冷却或伪造网络起点。
    int32 StableClipCase=0,StableClipWitnesses[3]={0,0,0}; // 静止/前冲/跳跃侧后冲三案；每案最多两份实际网格见证。
    uint64 StableClipInputFrame=0; // GFrameCounter输入提交帧，便于核对PostUpdate最终镜头与骨姿来自同帧。
    bool bStableClipDashRequested=false,bStableClipSawDash=false,bStableClipSawMontage=false,bStableClipSawFalling=false,bStableClipJumpPressed=false; // 当前案真实观察事实，只在案边沿复位。
    bool bStableClipAdvancePending=false,bStableClipCompleted=false; // PostUpdate只提出推进，下一PreActor才复位输入/位置；完成后等末帧截图落地再退出。
    int32 StableClipCompletedCases=0,StableClipCaseSamples=0; // 当前案有效采样数与已过输入合同案数，不能视为视觉验收案数。
};
