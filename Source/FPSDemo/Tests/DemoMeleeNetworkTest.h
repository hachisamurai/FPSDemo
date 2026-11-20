// 职责：近战双进程LAN验收。先由两端正常UI选核心/准备/出发，再执行原高空隔离动作观测；不代选核心或绕过正式流送。依赖Editor LAN/临时档及NetworkProbeSetup只读门。
// 使用：房主-DemoMeleeNetworkTest、客户端-DemoMeleeNetworkClient；两端加-DemoMetalImpactNetwork验证回执，SkinCapture可截图；本探针不验房间导航/实际地面。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoMeleeNetworkTest.generated.h"
UCLASS(Transient,NotBlueprintable)
class ADemoMeleeNetworkTest : public AActor
{
    GENERATED_BODY()
public:
    /** 显式启动后复制测试协调Actor，RPC只绑定已登记的远端PC。 */
    ADemoMeleeNetworkTest();
    /** DeltaSeconds为游戏秒；正式UI准备和动作观测分别计墙钟，只有真实首关KillAll已开战才启动原动作计划。 */
    virtual void Tick(float DeltaSeconds) override;
private:
    /** Next为0..5场景；四次正常输入后测客户端立即取消及服务器GAS拒绝，只有夹具临时设置门禁标签。 */
    void BeginStage(int32 Next);
    /** Next/Host/Remote为隔离测试计划；首阶段在两端各自重置动作总时钟，准备等待不占用90秒观测预算。 */
    void ResetStage(int32 Next,class ADemoCharacter* Host,class ADemoCharacter* Remote);
    UFUNCTION(Client,Reliable) void ClientBegin(int32 Next,class ADemoCharacter* Host,class ADemoCharacter* Remote); // 真实拥有者执行测试计划，必须显式客户端参数。
    UFUNCTION(Server,Reliable) void ServerResult(int32 Completed,bool Passed); // Completed拒绝旧阶段回执，Passed为该端实测结果。
    UFUNCTION(Client,Reliable) void ClientFinish(bool Passed); // 服务器通知另一进程退出，预留发包时间。
    /** Passed记录最终状态；客户端先退出、房主多留一秒，防房主断线销毁客户端测试Actor而遗留进程。 */
    void Finish(bool Passed);
    /** 返回本阶段被测Pawn：0/1及4/5为远端，2/3为房主。 */
    class ADemoCharacter* Subject() const;
    /** Elapsed为本阶段秒数；显式SkinCapture时调整观察端镜头并记录枪械/近战待机和挥击，不控制生产动画。 */
    void CaptureSkinViews(double Elapsed);
    TWeakObjectPtr<class ADemoCharacter> HostPawn,RemotePawn; // World拥有，跨关旅行后由新测试Actor重新解析。
    TWeakObjectPtr<class ADemoEnemy> Target; // 每阶段真实敌人骨骼碰撞/ASC靶，仅服务器使用。
    int32 Stage=-1; // -1等队伍，0..3正常攻击，4取消在途预测，5权威拒绝。
    bool bImmediateResponse=false,bNoRewind=true,bRollbackObserved=false,bLateRevival=false; // 本机输入同帧响应/确认不倒放/拒绝撤回/取消后不复活的独立证据。
    double LocalInputTime=0.; // 本端按键游戏秒，只在同一端计算响应或回退时间。
    float LastOwnedProgress=-1.f; // 本端上一次活动姿态相位，确认时不得回退；每阶段重置。
    uint32 LastOwnedSerial=0; // 合法新一击允许相位从0重来，不能误判成确认倒放。
    int32 OwnedAttackStarts=0; // 本机实际观察到的不同攻击序号，远端缓冲连击场景必须为2。
    bool bBufferedPress=false,bResetTarget=false; // 阶段1只点一次尾部缓冲并复位一次靶，避免自动连打或测试重复搬靶。
    double StageTime=0.,StartedAt=0.,ExitAt=0.; // 本端游戏秒/墙钟分别负责动画采样与进程上限。
    double SetupWaitSeconds=180.; // Editor独立准备时限秒，显式参数限30..600；动作开始后仍使用原90秒预算。
    bool bInitialStagePending=false; // 首阶段RPC先于本机Combat/菜单关闭到达时短等原计划，不添加业务或测试RPC。
    bool bHostRequested=false,bEquipped=false,bPressed=false,bSawAttack=false,bSawPose=false; // 每端真实输入及观察锁存。
    bool bSawFirstPersonPose=false; // 本地拥有者必须实际求值独立FP动画；远端无需创建另一人的相机网格。
    bool bLocalDone=false,bRemoteDone=false,bFinished=false,bPassed=false; // 双端都通过才前进；退出不重复发RPC。
    TWeakObjectPtr<class ACameraActor> SkinCamera; // 当前World拥有的观察端镜头，不复制给其他端。
    bool bSkinCameraReady=false,bSkinGun=false,bSkinIdle=false,bSkinAttack=false; // 每阶段本地截图锁存，ResetStage统一重置。
    uint64 MetalBefore=0, AudioBefore=0; // 可选金属专项的阶段前本机池计数；两端独立读，不接受服务器转抄成功结果。
};
