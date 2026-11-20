// 职责：扩展兵种与双Boss的冻结参数；由DT_Enemies读取，依赖GAS执行器，预警时间不随难度缩短。
#pragma once
#include "CoreMinimal.h"
#include "DemoEnemyRosterSettings.generated.h"

/** None保留旧敌人行为；Anchor是Boss拥有的可击毁装置，不参与随机小怪池。
    Beacon为2026-11-17追加的“封锁眼”巡逻无人机（D03剧本），只追加在末尾以保持旧序列化/表数值稳定。 */
UENUM(BlueprintType)
enum class EDemoRosterKind : uint8 { None, Guard, Bombardier, Mender, Exploder, Warden, Weaver, Anchor, Beacon };

/** 特殊小怪替换槽规则的唯一来源：表校验、生成计划与并发容量共用，避免四处白名单漂移。 */
namespace DemoRosterRules
{
    /** Kind为模板身份；返回是否可被关卡SpecialRows引用（护盾/迫击/修复/爆裂/封锁眼），Boss与锚点为false。 */
    FPSDEMO_API bool IsSpecialMinion(EDemoRosterKind Kind);
    /** Kind为模板身份；返回单关替换槽及同场并发上限，修复者1、其他特殊小怪2，非特殊返回0。 */
    FPSDEMO_API int32 SpecialCap(EDemoRosterKind Kind);
}
/** 技能执行数据，与关卡倍率解耦；默认值对应普通难度设计，实例只读。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoEnemyRosterSettings
{
    GENERATED_BODY()
    // 执行器选择；None让原有测试、蓝图和存量兵种继续使用原仲裁。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EDemoRosterKind Kind=EDemoRosterKind::None;
    // 护盾基础耐久点数，Resolve与HP使用同一关卡/难度倍率；不自动恢复。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Shield=180.f;
    // 正面总夹角120度；范围(0,180]，侧后方与核心沿用原受击倍率。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ShieldArc=120.f;
    // 被盾吸收的伤害比例[0,1]；不足部分溢出到Health，避免最后一击免伤。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float ShieldAbsorb=.65f;
    // 盾破或双锚销毁后的硬直秒；硬直期间取消正在准备的攻击。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Stagger=2.f;
    // 新技能结束后到下一次准备的World秒，难度只调整此值；迫击默认4、修复5。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Cooldown=4.f;
    // 技能准备秒；迫击/落雷/封锁眼光束>=1.2，修复1.5，爆裂1，扇射.7，冲击环另固定1.5。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Windup=1.2f;
    // 单个地面攻击半径cm；爆裂350、迫击/落雷220；封锁眼表示锁定光束半宽（默认90）。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Radius=220.f;
    // 修复者寻找目标的三维距离cm，不能隔墙治疗。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float HealRange=1200.f;
    // 单次修复目标最大生命的比例；每个被治疗敌人生命周期最多3次。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float HealFraction=.08f;
    // 施法期间累计实际掉血达到自身最大生命此比例即打断，护盾吸收不计掉血。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float InterruptFraction=.15f;
    // Boss大招结束后的输出窗口秒，所有后续攻击均等待，核心倍率沿用HitProfile。
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Recovery=1.5f;
    /** 运行时也检查有限值与可读前摇下限，错误配置不能靠编辑器Clamp掩盖。 */
    bool IsValid() const;
};
