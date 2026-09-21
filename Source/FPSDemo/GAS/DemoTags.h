#pragma once
#include "NativeGameplayTags.h"

namespace DemoTags
{
	// 敌人状态显示契约：由后续灼烧/冰霜/冻结GE授予与移除，HUD只读存在性，不用Tag计数冒充GE叠层。
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Burn);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Chill);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Frozen);
	// 狙击Aim GA激活期间持有，装填/切枪/非战斗取消时自动移除。
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Aiming);
	// 冲刺成功提交后持续0.45秒，仅用于Boss全图攻击判定，不是冷却或通用无敌。
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(DashEvading);
	// Boss全图命中的有时限减速；离开战斗统一移除。
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Slowed);
	// 瞬时属性 GE 的有符号 SetByCaller 数值；负值表示消耗/伤害。
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Magnitude);
	// 死亡状态阻止所有主动技能，重开关卡时随 ASC 销毁。
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Dead);
	// 装填技能激活期间持有，阻止射击并供 HUD 展示。
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reloading);
	// 保留旧标签名以兼容已有资源引用；Fire现在用各武器实例时间戳，不授予此标签。
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(FireCooldown);
	// 冲刺和治疗仍使用各自有时限GE授予的冷却标记。
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(DashCooldown);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(HealCooldown);
}
