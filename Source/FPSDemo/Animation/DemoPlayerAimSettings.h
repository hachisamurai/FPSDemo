// 职责：共享视角/身体转向与上身瞄准的项目配置；在DefaultGame.ini或配置CDO编辑，各端同构建读取，不写局内存档。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoPlayerAimSettings.generated.h"

/** 项目级瞄准表现配置；所有端使用同一Cook配置，既不属于局内成长，也不写玩家存档。 */
UCLASS(Config=Game, DefaultConfig)
class FPSDEMO_API UDemoPlayerAimSettings : public UObject
{
    GENERATED_BODY()
public:
    /** 配置加载后验证有限值与模型安全范围；错误回退默认值并记录，不将非法角度传给动画线程。 */
    virtual void PostInitProperties() override;
    // 向下最大角度，正数度数[1,75]；相机实际下界取负值，默认60避免过度低头。
    UPROPERTY(Config, EditAnywhere, Category="Aim", meta=(ClampMin="1",ClampMax="75")) float LookDownDegrees=60.f;
    // 向上最大角度，正数度数[1,75]；默认55，不能通过360度绕过俯仰界限。
    UPROPERTY(Config, EditAnywhere, Category="Aim", meta=(ClampMin="1",ClampMax="75")) float LookUpDegrees=55.f;
    // 头与身体的水平自由角度，左右各[1,60]度；45度内身体不转，超过后进入完整对齐过程，不再永久保留45度枪口偏差。
    UPROPERTY(Config, EditAnywhere, Category="Aim", meta=(ClampMin="1",ClampMax="60")) float HeadYawLimit=45.f;
    // 静止越界后身体追赶相机的指数速率1/秒[1,60]；14约0.22秒消除95%误差，到0.1度内收敛后恢复自由转头。移动/挥击仍立即对齐。
    UPROPERTY(Config, EditAnywhere, Category="Aim", meta=(ClampMin="1",ClampMax="60")) float BodyTurnFollowRate=14.f;
    // 总俯仰分配给腰上躯干的比例[0,0.35]；剩余角度由肩臂层完成，胶囊/骨盆/双腿保持移动动画。
    UPROPERTY(Config, EditAnywhere, Category="Aim", meta=(ClampMin="0",ClampMax="0.35")) float TorsoFraction=.18f;
    // 远端角度指数平滑速率1/秒[1,60]；24约0.125秒到95%，只平滑显示，不拖慢拥有者镜头/弹道。
    UPROPERTY(Config, EditAnywhere, Category="Aim", meta=(ClampMin="1",ClampMax="60")) float RemoteFollowRate=24.f;
    // 装填源混出后的瞄准恢复指数[1,4]，无单位；1与身体/机械已有SmoothStep同权，4仅供旧曲线同DLL对照。
    // 只影响非霰弹枪表现权重，不延长原0.12秒退出截止、不改变Reload/Shot/Bolt/弹药。各端同Game配置，非玩家存档。
    UPROPERTY(Config, EditAnywhere, Category="Aim|Reload", meta=(ClampMin="1",ClampMax="4")) float ReloadAimRestoreExponent=1.f;
    // 手枪低头极限时朝角色前方的握枪避让距离cm[0,8]；从低头15度开始平滑增加，防止枪柄碰衣身。
    UPROPERTY(Config, EditAnywhere, Category="Aim", meta=(ClampMin="0",ClampMax="8")) float PistolLowAimOffset=4.f;
    // 启用枪与双腕共同指向准星焦点的微调；仅表现，关闭可回退原来的平行方向对齐，不改变弹道授权。
    UPROPERTY(Config, EditAnywhere, Category="MuzzleFocus") bool bEnableMuzzleFocus=true;
    // 焦点距眼位至少200cm，合法50..1000cm；贴墙时仍由实体弹安全扫掠处理，禁止枪向后翻或强行追随不可达的近点。
    UPROPERTY(Config, EditAnywhere, Category="MuzzleFocus", meta=(ClampMin="50",ClampMax="1000")) float MuzzleFocusMinDistance=200.f;
    // 在已验收作者瞄准姿态上允许的额外完整枪框架角度，度[1,20]；限制贴墙/跨目标时的肩肘修正幅度。
    UPROPERTY(Config, EditAnywhere, Category="MuzzleFocus", meta=(ClampMin="1",ClampMax="20")) float MuzzleFocusMaxDegrees=15.f;
    // 仅平滑焦点距离的指数速率1/秒[1,60]；视角方向仍即时跟随，20约0.15秒到95%，不增加鼠标输入延迟。
    UPROPERTY(Config, EditAnywhere, Category="MuzzleFocus", meta=(ClampMin="1",ClampMax="60")) float MuzzleFocusDistanceFollowRate=20.f;
    // 已标定枪械的大幅原生IdleBreak及其手部差量总开关；false同时禁止原生状态进入并退出当前轮，普通Hold呼吸/跑跳保留。
    // 仅表现，不改变GAS/弹药或新增骨骼复制；旧版本false仅关手部差量的隔离结果不能代表本版本false行为。
    UPROPERTY(Config, EditAnywhere, Category="IdleBreak") bool bEnableIdleBreakMotion=true;
    // 待机动作目标混入/退出秒数[0.04,0.5]；右手、枪与左手共用，避免分别平滑导致脱握。
    UPROPERTY(Config, EditAnywhere, Category="IdleBreak", meta=(ClampMin="0.04",ClampMax="0.5")) float IdleBreakBlendSeconds=.15f;
    // 真实操作后再次允许新一轮随机待机的最短秒数[0.2,5]；被打断的当前轮不能中途恢复。
    UPROPERTY(Config, EditAnywhere, Category="IdleBreak", meta=(ClampMin="0.2",ClampMax="5")) float IdleBreakRearmSeconds=1.5f;
    // 大幅原生闲置动作仅允许近水平视线，绝对俯仰度数[1,45]；普通Hold呼吸/跑跳不受此门限制，各端读同一配置。
    UPROPERTY(Config, EditAnywhere, Category="IdleBreak", meta=(ClampMin="1",ClampMax="45")) float IdleBreakMaxViewPitchDegrees=20.f;
    // 大幅原生闲置动作相对身体最大左右视线偏角[1,45]度；使用真实Pawn视线，不读取被动画影响的头骨/相机。
    UPROPERTY(Config, EditAnywhere, Category="IdleBreak", meta=(ClampMin="1",ClampMax="45")) float IdleBreakMaxRelativeYawDegrees=20.f;
};
