# Boss升空俯冲

## 目的与设计

为第5/10关Boss增加升空、悬停无敌3秒、锁点斜向俯冲、落地50伤害与击退、硬直的完整技能。蓝色扩散光表示悬停无敌，地面蓝盘表示实际伤害落点；玩家出圈或在落地时处于冲刺窗口可免伤/免击退。本技能不增加躲避回血，原红色全图技能的躲避+5HP保留。

`UDemoBossDiveAbility`是InstancedPerActor、ServerOnly的原生GAS技能，归敌人自身ASC。Enemy Tick只驱动唯一活动技能，施法时暂停平面导航；空中通过115cm球体Sweep，落地后交回NavMesh。无需行为树/蓝图接线。无敌使用GE而非扣血后回填；特效不参与伤害、碰撞和导航。

## 执行流程

1. `DemoCombatConfig::Resolve→Enemy::Configure`冻结DiveSlam，生产Boss授予GA；小怪不授予。默认出生8秒后首次可用，冷却到期的俯冲优先尝试，但不抢断已开始的地面/全图预警。GAS入口独立检查Combat、存活目标、冷却、Frozen、其他施法，不能绕过AI直接发动。
2. 激活记录原始中心与逻辑关卡，先验证升空通道。0.6秒升高600cm，顶棚阻挡取消。升空可以受伤，被冻结或死亡会取消。
3. 到达顶点后开始固定3秒悬停。无敌GE授予`Demo.State.Invulnerable`，显示三层错峰向外扩散的蓝色壳与点光。GA全过程持有`Demo.State.BossDiveCasting`；旧飞行物/全图攻击入口和Enemy Tick共同阻止其他攻击。
4. 3秒结束快照玩家当前脚下，不预测、不再跟踪后续移动。NavMesh优先投影玩家位置，再尝试250/450cm的8方向候选环，要求投影成功且顶点到候选有完整飞行通道。落点中心为地面+130cm，保留原悬浮高度；不可达/通道堵塞则取消，不穿墙硬落。预警盘和真实落点一致。
5. 进入Diving立即移除无敌、关闭空中壳、显示地面范围。沿锁定斜线Sweep，时长`max(DiveSeconds,距离/MaxDiveSpeed)`。只在Boss移动Sweep中忽略当前玩家，玩家枪械仍能命中Boss；地形和其他阻挡仍有效。锁点后出现动态障碍会取消，途中碰撞不伤人。
6. 到达落点先转Recovery、消费一次性Impact标记，再判断范围、玩家脚下高度差≤180cm、落点上方30cm到玩家中心的Visibility与`IsDashEvading()`。出圈、遮挡或冲刺窗口均不伤害、不击退、不回血。
7. 命中通过Health GE施加固定-50，不读取AttackPower、不乘难度/等级。健康实际减少且玩家仍存活才LaunchCharacter：从落点向外900cm/s、向上220cm/s；中心重合时使用俯冲水平朝向，再回退世界前向。标记先于GE消费，玩家死亡触发的阶段取消不能重入伤害。
8. 默认落地硬直1秒，可以受伤但不能攻击。结束后再计16秒冷却；二阶段可乘既有0.75倍率，但3秒悬停/50伤害不变。其他攻击结束后至少再留1秒，避免接续全图瞬发。

## 无敌、Debuff与取消

`UDemoAttributeSet::PreGameplayEffectExecute`在扣血前拒绝无敌目标的负向加法Health和更低值Override，覆盖当前枪伤、穿透、爆炸和已有灼烧周期。治疗仍允许。后续新增乘法伤害或持续型Health修改器需同步扩展边界，不能直接SetHealth绕过GE。

新灼烧/冰霜同时在AmmoStatus入口和GE ApplicationRequirement拒绝，外部直接施加GE也不能叠层。已有DOT/冰霜继续计时，不被清除/延期；周期伤害在无敌期间被拦截，解除后恢复。Frozen阻止起飞，在升空/俯冲时新增冻结则取消本技能；原其他技能的定身语义不变。

无敌GE为Infinite，但由活动GA精确保存Handle，只在Hovering持有；同一状态时钟应用/移除，避免独立GE时长与低帧率切换错位。正常结束、Cancel、死亡、阶段/关卡变化、EndPlay都走EndAbility，移除无敌、销毁VFX、清移动忽略、设结束冷却。暂停冻结World时钟与Tick，没有异步Lambda或后台移动任务。

空中取消且Boss存活时沿经过的顶点/原点逆向Sweep回收；Rising直接回原点，已落地留在落点。若动态物体同时堵住回收通道，优先不穿墙，记录`DIVE_RETURN_BLOCKED`并留在碰撞安全点；关卡制作应避免在Boss原起飞通道生成封闭障碍。无合法落点/低顶棚取消不造成伤害，也不发躲避奖励。当前只支持原单人Standalone/PIE，不等同完整联机技能/VFX同步。

## 配置与制作

源配置为`Config/Combat/Enemies.json`中DiveSlam，运行表`/Game/Data/DT_Enemies`；Drone关闭、Warden开启。JSON无注释，由本文记录意图和范围。修改后用Editor运行`Tools/Unreal/import_combat_tables.py`；表内手改会被后续JSON导入覆盖。直接C++生成需显式`bUseDive=true`且bBoss=true；旧调用默认禁用，非法直接配置不授予GA，错误生产表则拒绝开局。

| 字段 | 默认 | 单位/合法范围 |
|---|---:|---|
| bEnabled | Boss=true | 小怪忽略 |
| Height | 600 | 升高cm，300..1000 |
| RiseSeconds | 0.6 | 秒，0.3..2 |
| DiveSeconds | 0.6 | 目标秒，0.3..2，限速可能延长 |
| MaxDiveSpeed | 4000 | cm/s，1000..6000 |
| Radius | 300 | 水平伤害半径cm，150..500 |
| Knockback | 900 | 水平击退cm/s，0..1600 |
| KnockUp | 220 | 向上击退cm/s，0..500 |
| RecoverySeconds | 1 | 秒，0.5..3 |
| Cooldown | 16 | 结束后秒数，8..60；二阶段后至少6秒 |
| InitialDelay | 8 | 首发等待秒，3..60 |

3秒悬停和50伤害固定，不受二阶段/难度缩放。全部浮点字段拒绝NaN/无穷/越界。

`Tools/Unreal/create_boss_dive_vfx.py`由Editor创建`/Game/VFX/EnemyAttacks/Dive/M_BossDiveBlue`（Unlit/Additive/Fresnel）。`ADemoBossDiveVFX`硬引用材质与引擎球体，Cook可收集：三层球壳每秒错峰扩散，展平球体为落点范围盘，落地扩散0.4秒后隐藏。GA结束销毁表现Actor，不写伪造uasset，不修改原红光材质。

## 验证与调试

独立游戏专项参数`-DemoEnemyAttackTest -DemoBossDiveTest`，复用EnemyAttack隔离档/禁云入口，不单独裸用DiveTest。地图`/Game/Whitebox/Maps/L_ThreeSector_Whitebox`。NullRHI可测功能；加`-DemoBossDiveCapture -RenderOffscreen -windowed -ForceRes -ResX=1280 -ResY=720`可采集真实RHI画面，保存在`Saved/Screenshots/BossDive/Hover.png`和`LandingTarget.png`。

八组真实World场景覆盖生产配置启用、非法高度、首发冷却、升空可受伤、3秒无敌、旧攻击互斥、普通GE/旧DOT免伤、新Debuff拒绝、锁点与解除无敌、固定50/击退、真实Dash GA窗口圈内免伤、出圈不追踪、主动/阶段取消、Boss升空死亡、动态障碍和致命命中清理。成功标记`DEMO_BOSS_DIVE_SUCCESS`，失败`ATTACK_TEST FAIL`；必须结合日志与退出码判定。兼容回归`-DemoEnemyAttackTest`及`-DemoAmmoTest`覆盖原全图/躲避回血和元素弹药。

重点日志：`BOSS_DIVE_PHASE`、`DIVE_LOCK`、`DAMAGE_BLOCKED`、`DEBUFF_REJECT`、`DIVE_HIT`、`DIVE_EVADED`、`DIVE_CANCEL`、`DIVE_END`。高频Advance/GetPhase使用DEMO_LOG_TICK，阶段/失败使用普通日志；接口和变量均有用途/生命周期注释。

## 影响文件与维护

新增`AI/DemoBossDiveSettings.*`、`AI/DemoBossDiveVFX.*`、`AI/DemoEnemyDive.cpp`、`GAS/Abilities/DemoBossDiveAbility.*`、`Tests/DemoBossDiveTest.cpp`及VFX制作脚本。修改`AI/DemoEnemy.*`、`Game/DemoCombatConfig.*`、`GAS/DemoAttributeSet.*`、`GAS/Ammo/DemoAmmoEffects.*`、`DemoAmmoStatus.cpp`、`Tests/DemoEnemyAttackTest.*`与Enemies.json。资产为DT_Enemies和独立Dive材质；现有导入工具还保存另两张配置表，其源配置未更改。

修改Boss半径/悬浮高度须同步飞行Sweep、落点中心、VFX地面偏移和导航代理；新增技能须接入Casting/旧前摇互斥。飞行阶段/无敌不写永久存档。自动化不等同所有动态布局、手感平衡和完整联机验收。

## 本次验证记录（2026-09-21）

- Editor Development编译/链接通过，`Saved/Logs/BossDiveBuild.log`；独立VFX与怪物表由Editor实际创建/保存，分别记录`BOSS_DIVE_VFX_SUCCESS`与`COMBAT_IMPORT_SUCCESS`。
- 初轮NullRHI七场景`BossDiveTest.log`通过；最终真实RHI八场景`BossDiveRender.log`输出`DEMO_BOSS_DIVE_SUCCESS`、退出0，包含致命落地GE触发玩家死亡后的技能清理。
- 已检查1280×720实拍`Saved/Screenshots/BossDive/Hover.png`及`LandingTarget.png`：三层蓝色外扩壳、空中Boss和地面范围预警可见。
- `DiveAttackRegression.log`输出`DEMO_ENEMY_ATTACK_TEST_SUCCESS`、退出0；`DiveAmmoRegression.log`输出`DEMO_AMMO_TEST_SUCCESS`、退出0。原全图/冲刺回血和元素弹药回归通过；原攻击测试中的玩家死亡日志是预期用例。
- Windows Development完整Build/Cook/Stage/Pak/Archive成功，见`BossDivePackage.log`；产物`Saved/BossDivePackagedValidation/Windows/FPSDemo.exe`。独立打包八场景`BossDivePackagedTest.log`输出`DEMO_BOSS_DIVE_SUCCESS`、退出0，日志确认随机测试档已清理。
- 最终专项与回归无`ATTACK_TEST FAIL`、`AMMO_TEST FAIL`或非预期`Error:`。未验证Shipping、联机、所有动态封闭布局及最终玩家手感平衡；暂停依赖引擎World Tick/时间，未在本次专项单独模拟Esc暂停操作。
