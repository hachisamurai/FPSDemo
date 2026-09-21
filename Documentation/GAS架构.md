# GAS 架构与技能模块

## 目的与设计理念

将“能否执行技能”和“数值如何改变”放到 GAS；生命与全局升级由属性集管理，当前弹药唯一保存在武器实例，输入/UI只调用受控接口。参考本机 Lyra 的所有权和生命周期划分，但不复制其 Experience/GameFeature/装备插件体系，保持 UE5.4 模板独立编译。

参考源码：

- `F:/Lyra/Lyra/Source/LyraGame/Player/LyraPlayerState.cpp`：PlayerState 拥有 ASC、AttributeSet，Mixed 复制。
- `F:/Lyra/Lyra/Source/LyraGame/Character/LyraPawnExtensionComponent.cpp`：Owner/Avatar 初始化与解绑。
- `F:/Lyra/Lyra/Source/LyraGame/AbilitySystem/Attributes/LyraHealthSet.cpp`：属性边界与 GE 后处理。
- `F:/Lyra/Lyra/Source/LyraGame/AbilitySystem/LyraAbilitySet.cpp`：只在权威端授予技能、避免生命周期泄漏。
- [Epic GAS 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-ability-system-for-unreal-engine?application_version=5.4)：概念参考；API 兼容性以本机 5.4.4 源码和实际编译为准。

## 所有权与运行流程

1. GameMode 指定 `ADemoPlayerState` 与 `ADemoCharacter`。
2. PS 构造默认 ASC/属性子对象；ASC 在初始化时发现并注册属性集。
3. Pawn `PossessedBy` 调用 `InitAbilityActorInfo(PS, Pawn)`。`OnRep_PlayerState` 为复制到达时补做 Avatar 绑定，但客户端不授予技能。
4. `GrantStartupAbilities` 只在服务器执行一次，授予 Fire、Reload、Dash、Heal、Aim，并在Avatar初始化后创建武器库存。
5. 输入 → ASC `TryActivateAbilityByClass` → GAS 成本/冷却/标签检查 + 角色/阶段检查 → `CommitAbility` → 动作或异步任务 → `EndAbility`。
6. Pawn EndPlay 精确解绑健康委托、清理计时器；仅当自己仍是当前 Avatar 时取消技能、清空 ActorInfo。
7. 敌人 ASC 归敌人 Actor 所有，Owner/Avatar 都是自身；死亡后延迟 0.1 秒销毁，避免在属性委托栈中释放 ASC。

## 属性与效果

| 属性 | 默认 | 范围 | 用途 |
|---|---:|---|---|
| Health | 100 | 0..MaxHealth | GE 有符号加法，伤害为负、治疗为正 |
| MaxHealth | 100 | >=1 | 购买增加上限 |
| AttackPower | 25 | >=0 | 敌人攻击基值，玩家枪伤改读武器定义 |
| WeaponDamageBonus | 0 | 0..10000 | 全武器每次触发的伤害加成，霰弹按弹丸分摊 |
| MagazineBonus | 0 | 0..10000 | 全武器弹匣容量加成，取非负整数 |
| MoveSpeedMultiplier | 1 | 0.1..1 | GAS减速倍率，与狙击开镜步速倍率相乘 |

`PreAttributeChange` 和 `PreAttributeBaseChange` 都执行钳制；`PostGameplayEffectExecute` 在上限降低时修正当前值并记录目标、属性、变化量。属性复制使用 `REPNOTIFY_Always` 并通知 GAS 聚合器。

`DemoEffects::Apply` 为每次操作创建独立 Spec，统一通过 `Demo.Data.Magnitude` SetByCaller 注入数值，不修改共享 GE CDO。拒绝空 ASC、无权威、空类、非有限数值。返回 true 表示 Spec 已执行，不保证钳制后的属性一定发生变化。

Fire覆盖GAS的CheckCost/ApplyCost与CheckCooldown/ApplyCooldown，读取当前武器的弹药和RPM时间戳；Commit成功只扣一次，武器消费一次执行许可后命中。移除旧的Ammo/MagazineSize属性及ShotCost/Ammo/FireCooldown GE，避免重复数据源。`UDemoPowerEffect`修改WeaponDamageBonus，`UDemoMagazineEffect`修改MagazineBonus；补弹经过武器实例接口。生命、伤害、升级仍使用GE。

## 技能执行

| 技能 | 执行 | 约束 |
|---|---|---|
| Fire | 当前武器执行相机瞄准+枪口阻挡射线，散弹多弹丸聚合GE | 消耗/射程/RPM按BP配置；死亡/装填/非Combat阻止 |
| Reload | R与空弹再次射击共用RequestReload；WaitDelay等待当前武器配置秒数 | 满弹/无备用/重复请求拒绝；弱引用锁定原武器，取消不补弹 |
| Aim | 持续激活持有Aiming标签，右键切两档真实FOV | 狙击支持；切枪/装填/菜单/非战斗取消并恢复 |
| Dash | 水平输入方向，没有输入则朝向；默认1300 cm/s初速度 | 战斗/清关备战/安全区可用；4秒冷却，0.45秒Boss全图躲避窗口；菜单/暂停拒绝 |
| Heal | 默认恢复35HP，可被清关医疗奖励加强 | 战斗/清关备战/安全区可用；12秒冷却，满生命拒绝且不消耗冷却；菜单/暂停拒绝 |

技能使用 `InstancedPerActor + ServerOnly`，本版单人不实施客户端预测/RPC。冲刺与治疗的强化参数属于当前 Pawn，本局保留、重开重置；射击数值强化通过 GE 修改属性基础值。

Dash/Heal冷却由有时限GE授予标签；Fire冷却独立存于武器实例，切枪不会刷新。UE5.4 使用 `UTargetTagsGameplayEffectComponent`，**构造函数必须 `CreateDefaultSubobject` + 加入 `GEComponents`**；构造中调用 `FindOrAddComponent` 会因为其内部匿名 `NewObject` 触发断言。本版已启动验证此边界。

## 边界与失败处理

- 武器技能仍验证Combat；Dash/Heal共用`CanUsePlayerSkills`，允许Combat/Intermission/Hub且存活、无菜单/暂停。Reward奖励页、Victory结算页、Defeat死亡页、大厅均拒绝；终端/武器详情、出发确认/难度选择、Esc覆盖页也拒绝，不能直接调用GA绕过UI。通关选择返回安全区后立即可用。清关补满生命，所以治疗显示“生命已满”是正常状态，受伤才可施放。
- 同Pawn阶段切换不刷新技能冷却；死亡重开会重建PS/ASC清除死亡标签与短期状态，永久金币成长按V3检查点恢复，银币及免费临时成长重置。
- 死亡先加 Dead 标签，失败状态立即阻断敌人后续伤害和新技能。
- 装填回调重新检查 Avatar、生命和阶段；取消任务、切图或阶段变化均不会赠送延迟弹药。
- 最后击杀通过 NextTick 推进清关，防止当前 Fire 激活栈内切图/取消。
- 不声称提供完整多人逻辑：关卡、经济、输入、敌人目标选择均面向单人。

## 使用、验证与影响文件

依赖 `GameplayAbilities / GameplayTags / GameplayTasks / EnhancedInput`；插件在 `.uproject` 显式启用，JSON 不支持注释，字段变更原因记录于本文。

调整Dash/Heal冷却：`GAS/DemoEffects.cpp`；武器成本/RPM/装填时长：派生BP的Config；激活策略：`GAS/DemoGameplayAbility.cpp`；初始属性与上下限：`GAS/DemoAttributeSet.*`。更改时同步 UI 描述和自动回归预期。

主要文件：`GAS/DemoTags.*`、`DemoEffects.*`、`DemoAttributeSet.*`、`DemoAbilitySystemComponent.*`、`DemoGameplayAbility.*`、`DemoPlayerState.*`、`DemoCharacter.*`、`FPSDemo.Build.cs`、`FPSDemo.uproject`。

验证入口见 [调试与验证](调试与验证.md)。回归覆盖真实射线伤害、成本去重、技能冷却、WaitDelay 装填完成/取消、安全区阻止武器但允许冲刺/治疗，以及跨关卡重开的ASC重建。

本次武器拆分的接口、配置、键位与回归结果见 [武器系统](武器系统.md)。旧Ammo属性/GE引用必须迁移，不能通过重加旧属性兼容，否则会产生两份弹药状态。


## 清关与通关后冲刺/治疗（2026-09-21）

目的：清关后玩家仍能冲刺前往终端、受伤后治疗，通关回Hub也可使用。角色提供独立`CanUsePlayerSkills`，GA的CanActivateAbility按Action分流，Dash动作入口再次检查同一许可；武器继续调用CanUseCombatAbilities，避免扩大射击/装填/开镜权限。HUD技能状态也调用同一接口，备战区/安全区显示就绪、真实冷却或生命已满，菜单内显示当前不可用。

输入仍为LeftShift冲刺、Q治疗，经角色RequestAbility到ASC/GAS；不新增资产或配置。冷却继续在DemoEffects.cpp配置（4/12秒）。满血不消费治疗冷却，死亡标签与ServerOnly权威规则不变。存档不保存短期冷却，普通阶段切换不专门重置冷却。

依赖与影响：Characters/DemoCharacter.*、GAS/DemoGameplayAbility.*、UI/DemoHUD.cpp、Tests/DemoSessionTest.*。新增函数入口使用DEMO_LOG_TICK，GA拒绝阶段/菜单时记录Action，Dash动作拒绝记录许可原因。

验证：扩展`-DemoSessionTest -DemoVictoryContinuationTest`，在真实初始Hub、领取奖励后的Intermission、通关Hub直接调用实际GAS激活，检查治疗GE、冲刺躲避标签、两种冷却及重复拒绝、满血不耗冷却；终端、暂停、出发菜单和Victory模态页面验证无效果/无扣冷却，并验证非战斗武器仍拒绝。测试恢复生命、清理夹具冷却与排队冲量以保持后续经济/读档断言。测试不是实际键盘输入或手感验收，运行结果见[调试与验证](调试与验证.md)。
