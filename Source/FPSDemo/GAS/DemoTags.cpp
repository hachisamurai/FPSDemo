#include "GAS/DemoTags.h"

namespace DemoTags
{
	// 与弹药Debuff设计中的Granted Tags一致；注册标签本身不施加伤害、减速或冻结。
	UE_DEFINE_GAMEPLAY_TAG(Burn, "Debuff.Burn");
	UE_DEFINE_GAMEPLAY_TAG(Chill, "Debuff.Chill");
	UE_DEFINE_GAMEPLAY_TAG(Frozen, "State.CC.Frozen");
	// 瞄准状态属于Ability生命周期，与单武器射击时间戳互不混用。
	UE_DEFINE_GAMEPLAY_TAG(Aiming, "Demo.State.Aiming");
	// 有时限 GE 授予的状态标签，和 DashCooldown 分离以避免整个4秒冷却均能躲避。
	UE_DEFINE_GAMEPLAY_TAG(DashEvading, "Demo.State.DashEvading");
	UE_DEFINE_GAMEPLAY_TAG(Slowed, "Demo.State.Slowed");
	UE_DEFINE_GAMEPLAY_TAG(Magnitude, "Demo.Data.Magnitude");
	UE_DEFINE_GAMEPLAY_TAG(Dead, "Demo.State.Dead");
	UE_DEFINE_GAMEPLAY_TAG(Reloading, "Demo.State.Reloading");
	UE_DEFINE_GAMEPLAY_TAG(FireCooldown, "Demo.Cooldown.Fire");
	UE_DEFINE_GAMEPLAY_TAG(DashCooldown, "Demo.Cooldown.Dash");
	UE_DEFINE_GAMEPLAY_TAG(HealCooldown, "Demo.Cooldown.Heal");
}
