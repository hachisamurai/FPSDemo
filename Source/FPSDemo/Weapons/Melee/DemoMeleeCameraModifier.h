// 职责：近战本人最终Roll反馈。使用：Melee组件在当前本人装备期间注册到CameraManager，换枪/死亡/菜单/EndPlay精确移除。
// 依赖：武器只读配置、当前Avatar和真实起刀/HP回执；无Tick/Timer、资产加载、ControlRotation或玩法权限。
#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "Weapons/Melee/DemoMeleeDefinition.h"
#include "DemoMeleeCameraModifier.generated.h"
class ADemoCharacter;

UCLASS()
class FPSDEMO_API UDemoMeleeCameraModifier : public UCameraModifier
{
    GENERATED_BODY()
public:
    /** CameraManager构造实例；优先级在既有受击220之后，合成有限最终Roll，不阻断其它Modifier。 */
    UDemoMeleeCameraModifier();
    /** Pawn为当前本人Avatar，Config为武器值快照；安装时调用一次，非法值降级到安全范围，不持有DA引用。 */
    void Initialize(ADemoCharacter* Pawn,const FDemoMeleeCameraFeedbackSettings& Config);
    /** 实际挥声相位触发；Serial为本端单刀唯一号，Direction按招式侧给正负，不从每次动画求值重触发。 */
    void PlaySwing(uint32 Serial,float Direction);
    /** ShotId为已确认实际扣血的旧网络身份；只做本人表现，每刀多个目标限一次，不解析Actor或授伤害。 */
    void PlayHit(const FGuid& ShotId);
    /** 最终相机回调，游戏秒包络；仅Roll，不回写CameraComponent，不改变Pitch/Yaw/FOV/Location或瞄准。 */
    virtual bool ModifyCamera(float DeltaTime,FMinimalViewInfo& InOutPOV) override;
#if WITH_EDITOR
    /** Editor真实HP专项只读已接受的去重ShotId数（最多32）；不触发反馈、不开放蓝图/非Editor接口。 */
    uint32 GetHitFeedbackCountForAudit() const;
#endif
private:
    /** 当前本人/实际ViewTarget/存活/菜单资格核对；失败返回false且最终回调清掉包络。 */
    bool CanPresent() const;
    /** 纯数学正弦平方包络，开始/结束斜率为0；Now/Start游戏秒，Duration为已校验秒数。 */
    static float Envelope(double Now,double Start,float Duration);
    TWeakObjectPtr<ADemoCharacter> Avatar; // 不跨Pawn延命，当前PC实际ViewTarget还需每帧核对。
    FDemoMeleeCameraFeedbackSettings Settings; // 初始化冻结的纯配置，后续不访问DA/骨骼。
    uint32 LastSwingSerial=0; // 单刀挥击去重，不复制/存档。
    double SwingStarted=-1.,HitStarted=-1.; // 本World游戏秒，取消由组件移除本实例，不留异步任务。
    float SwingDirection=1.f; // 当前挥击左右符号，命中沿本刀方向。
    TArray<FGuid> HeardHits; // 至多32条ShotId，实例销毁自动清理，多目标不叠加确认。
};
