# 弹药类型与 GAS Debuff：实现说明

2026-09-21：按确认的预览接入真实终端、金币交易、武器弹道、GAS和检查点V4。图标直接复用已制作的 ElementIcons；本文件替代原待确认设计，以当前代码为准。当前范围为单人 Standalone / PIE，不声称完整多人预测与复制支持。

## 目的与界面流程

安全区升级终端提供「属性升级」「武器」「弹药类型」三个页签。弹药页沿用预览的列表/详情布局，每项名称左侧显示现有图标；顶部显示金币和已装配类型。列表查看不会改变装备，主副武器共用一种弹药类型，沿用各武器原有弹匣和装填机制。

### 终端页面尺寸与固定导航

弹药页与武器/属性页统一为960×480设计像素，锚点为`(ViewWidth/2-480, ViewHeight/2-240)`，随现有UIScale等比缩放。属性/武器/弹药页签左偏移分别为430/560/670、宽120/100/130；关闭按钮左偏移822、宽106，四个按钮均在面板顶部25处、高36。切换页面只改变高亮和内容，不改变这些按钮的屏幕位置或命中区域。

弹药内容由原540高排版收紧：四行从Y+137起，每行60高、间隔6；详情区与四行上下对齐；金币和当前装配位于标题下方说明行，消息/规则说明位于底部，避免覆盖固定导航。购买确认框仍在屏幕中心，报价期间底层按钮/列表不注册热区，交易权限及失败处理沿用原流程。

此样式无需新增配置，影响`UI/DemoAmmoHUD.cpp`及本文，入口保留`DEMO_LOG_TICK()`。后续更改导航坐标必须同步`DemoHUD.cpp`的武器/属性页，列表布局变更必须同步HitBox尺寸。验证使用现有`-DemoAmmoTest`真实渲染，检查页签位置、四行内容、操作按钮及底部说明无越界；交易回归不等于系统鼠标切页验收。

2026-09-21布局修正验证：`Saved/Logs/AmmoLayoutBuild.log`完整Editor Development构建通过；1280×720真实渲染`AmmoLayoutValidation.log`输出`DEMO_AMMO_TEST_SUCCESS`，进程退出0。已查看四行列表、三行详情、固定顶部导航和底部消息完整显示，截图归档`Art/UI/AmmoAligned/1280x720/AmmoTerminalGame.png`；代码核对页签与关闭按钮坐标/尺寸和武器页一致。本次未重新打包或重测其他分辨率。

1. 普通弹默认免费且已解锁。特殊弹药独立定价，默认火焰300、冰冻400、穿透500金币。
2. 未解锁项显示价格，金币不足显示差额；查看详情不扣币。确认弹窗显示购买前后余额。
3. 取消、切换查看项或关闭页面不购买。确认后重新检查报价、Hub阶段、当前存档、终端距离、玩家生命和模态权限。
4. 扣币与解锁一起提交本地检查点；保存失败回滚内存中的钱包与解锁。成功后仍需点击「装配此弹药」，不自动换弹。
5. 装配也先保存选择再更新装配GE；失败保留旧类型。关间及战斗不能购买/切换。价格不随其他购买上涨，银币不参与。
6. 解锁和选择属于当前存档槽，死亡、通关和主动放弃保留。Editor停止试玩仍清理该GI全部临时进度。

普通弹可撤销特殊效果。特殊参数非法时，弹道回退普通伤害并记录配置错误；普通弹装配不依赖特殊数值合法性。目录四项不能删除或重排；目录数量错误会显示配置错误而停止绘制操作。

## 配置与现有Icon

在内容浏览器打开 `/Game/Data/Ammo/DA_AmmoCatalog`，直接修改数据资产实例，不修改共享GE CDO。由 `Tools/Unreal/create_ammo_assets.py` 通过Editor创建并保存；重跑保留已有策划配置，没有手写二进制uasset。

| 配置 | 用途与范围 |
| --- | --- |
| Entries | 顺序固定为normal / fire / frost / piercing，稳定ID用于存档，Name为显示名 |
| Icon | UTexture2D强引用，四张小图随目录加载/Cook；缺图显示问号，不阻塞购买 |
| UnlockGoldCost | 每项独立的一次性金币整数价格，0..100000000；0也需确认领取 |
| bUnlockedByDefault | 跳过购买；普通弹始终可用，特殊弹默认false |
| BurnDuration / BurnPeriod | 默认4秒/1秒；新层刷新整组寿命但不重置周期；周期不能大于寿命 |
| BurnDamagePerStack / BurnThreshold / ExplosionDamage | 默认每周期每层3HP，第5层单体爆炸40HP并清空火焰栈 |
| ChillDuration / SlowPerStack / MaxSlow | 默认4秒、每层10%、总上限40%；减速比例范围0..0.9 |
| FreezeThreshold / FreezeDuration | 默认第5层定身2秒；层数范围1..32，时长为正 |
| PostThawImmunityDuration | 默认解冻后6秒；免疫总时长=冻结时长+此值，可为0 |
| PiercingDamageMultiplier / SecondaryDamageRatio | 默认首段×1.25，后段为首段理论伤害×0.5；后段比例0..1 |

所有浮点值必须有限且非负；时长、周期必须为正。阈值硬上限与GE的32层上限一致。穿透额外目标数固定1，不提供超过当前实现能力的配置。

复用资产为 `/Game/UI/Textures/ElementIcons/T_Ammo_Normal`、`T_Ammo_Fire`、`T_Ammo_Ice`、`T_Ammo_Piercing`，原始图片在 `SourceAssets/UI/ElementIcons/`。没有重复生成或替换美术图标。敌人头顶使用已有元素HUD，显示真实GE栈数及冻结提示，详见[元素图标](元素图标.md)。

## GAS职责与核心执行流程

玩家ASC继续归PlayerState所有；玩家Pawn上的 `UDemoAmmoComponent` 管理装配与交易。每次初始化/恢复为ASC授予一个Infinite装配GE，其DynamicGrantedTags为 `Ammo.Type.Normal / Fire / Frost / Piercing` 四选一。Avatar结束时只移除自己持有的装配句柄。

`Enhanced Input → Fire GA → 武器实例 PerformBallistics → 直接伤害GE → 敌人UDemoAmmoStatus → 叠层/阈值GE与GC`。

一次开火快照类型和配置，同步收集全部弹丸命中并按敌人聚合伤害，再通过原 `UDemoHealthEffect` 结算。仅实际受正伤害且存活的敌人接收火焰/冰霜状态；致死直伤不叠层。每次应用前检查Combat阶段，最后敌人死亡引起阶段切换后拒绝后续过期结算。一次函数调用内的ShotSequence作为射击序号，不额外维护跨帧ShotId集合。

### GE与Tag

| 原生GE/状态 | 行为 |
| --- | --- |
| UDemoBurnEffect / Debuff.Burn | Duration+Periodic，AggregateByTarget；GAS自动将周期伤害乘层数，不手动重复乘算；首次添加不立即DOT |
| UDemoChillEffect / Debuff.Chill | Duration叠层，添加/栈变化更新减速GE |
| UDemoChillMoveEffect | Infinite辅助GE，Override敌人现有MoveSpeedMultiplier为1-min(层数×每层减速,上限)，冰霜移除同步撤销 |
| UDemoFrozenEffect / State.CC.Frozen | Duration定身，由GE到期移除；移动停止但攻击逻辑继续 |
| UDemoFrostImmunityEffect / Immunity.Frost | Duration免疫覆盖冻结与解冻后时间；UDemoFrostRequirement在Chill/Frozen GE自身拒绝免疫或死亡目标 |
| UDemoAmmoLoadoutEffect | 玩家唯一装配GE；换弹移除旧句柄再应用新Spec |

Burn/Chill/Frozen复用 `DemoTags` 中的Native Tags。GE采用UE5.4的TargetTagsGameplayEffectComponent；SetByCaller复用现有 `DemoTags::Magnitude`，持续时间和周期通过每次独立Spec注入。效果分类依原生GE类与GrantedTags查询，当前没有额外Effect.Ammo.* Asset Tags，也不需要GE蓝图子类。

状态组件绑定ASC的添加/移除委托、每个栈的变化委托，以Active GE作为层数和寿命唯一真值。第N层立即消费，不依赖Overflow触发第N+1次。先记录来源ASC，再在重入保护下移除栈；移除后的Active GE指针不再访问。初次添加也检查阈值，支持配置为1。移除和EndPlay精确解绑；没有独立Debuff倒计时数组。

火焰爆炸是满层目标的单体额外伤害，GC显示局部脉冲，不造成范围伤害。DOT、爆炸及穿透后段不会递归施加新弹药效果。周期灼烧不触发原枪弹肉体命中音效，其他正常直接受伤继续原反馈。

冰冻为定身，保留小怪射击和Boss已经开始的2秒全图攻击。敌人导航组件在前摇或Frozen时Stop，否则以配置速度×MoveSpeedMultiplier追击和绕障。冻结先施加，再施加免疫；免疫施加失败撤销此次冻结。免疫内直接伤害正常，命中不会刷新免疫。火与冰可共存，不增加元素反应。当前辅助倍率复用现有属性；将来增加其他敌人减速来源时应明确组合方式，避免多个Override相互覆盖。

离开Combat、敌人死亡和销毁时清除弹药GE、辅助减速与光效；结束当前冻结并不绕过Boss前摇移动限制。当前未提供提前驱散冻结的玩法接口，未来若增加驱散需定义解冻后的免疫时间重算规则。

### GameplayCue表现

GE声明 `GameplayCue.Ammo.Burn / Chill / Frozen / FrostImmune`，爆炸和穿透执行 `GameplayCue.Ammo.Explosion / Piercing`。敌人通过原生 `IGameplayCueInterface::HandleGameplayCue` 转交状态组件：常驻效果驱动独立点光源，爆炸/穿透生成短寿命局部脉冲Actor。免疫Cue不额外显示独立特效。现有敌人HUD从Tag/Active GE读状态与层数，GC绝不扣血。

本版使用原生GC接口而非新增GC蓝图或Niagara包。脉冲复用ADemoAttackPulse，但局部半径分别180/60cm；原Boss全图脉冲仍默认9000cm。后续可替换表现而不修改GE伤害逻辑。

## 弹道与穿透边界

武器保持Hitscan。散弹每颗pellet独立检测，所有贡献对同一敌人合并成一次伤害GE和一次叠层，因此一枪不会瞬间叠满。多个敌人各自最多加一层。

穿透首段先计算距离衰减再乘增伤，沿实际枪口轨迹忽略第一敌人所有组件，继续到原射程终点；最近阻挡为墙则结束，最多再伤害一个敌人，不穿墙、不递归。保留相机到枪口之间的墙体保护。第二段基于第一段理论伤害而非实际扣血：基础100→首敌125→后敌62.5，首敌只剩10HP也不会把后段变成5HP。

散弹每颗pellet独立处理穿透，同一敌人可以接收不同pellet的首段或后段贡献，最终统一聚合。敌方飞行物不会自动获得玩家弹药效果。

## 检查点V4、失败处理与云端

V4在V3成长账本基础上增加 `UnlockedAmmoIds` 与 `SelectedAmmoId`。仅记录稳定ID，不保存Actor、GE句柄或资源路径。新档普通弹；V1/V2先沿原规则迁移成长账本，再与V3一起补普通弹默认值。未来版本保护原文件；V4未知ID、重复ID、缺普通弹或装配未解锁ID均拒绝，不静默授予权益。组件恢复另有普通弹防御回退。

金币、解锁、装配、关卡与成长共同进入同一个检查点，调用现有SaveCheckpoint/SaveActive和状态UI。购买成功即保存，无需等到死亡或通关。ResetActive保留已提交的解锁与选择。购买失败或保存失败不向UI发布成功；确认报价失效需重新发起确认。

云导出沿UStruct序列化包含V4字段；后端Contracts/ProfileService/health支持1..4。旧请求缺失字段使用nullable并在JSON中省略，保持历史幂等请求内容。云端继续按整个槽版本CAS，钱包与解锁不能各自做集合合并。当前仍遵循既有离线进度信任策略，字段校验不是服务器权威反作弊购买证明。

Editor不创建云/API子系统，不能加开关绕过；停止试玩删除本次临时解锁。本机API已升级，部署其他服务时先更新后端再发布V4客户端。详细退出、离线发件箱和冲突策略见[云存档运行与验证](云存档运行与验证.md)。

## 文件分类与维护入口

- `Weapons/Ammo/DemoAmmoCatalog.*`：可编辑数据资产、固定ID、配置检查。
- `Weapons/Ammo/DemoAmmoComponent.*`：购买、装配、存档捕获/恢复、装配GE生命周期。
- `GAS/Ammo/DemoAmmoEffects.*`、`DemoAmmoStatus.*`：GE模板、免疫条件、敌人栈回调与GC桥接。
- `Player/DemoAmmoMenu.cpp`、`UI/DemoAmmoHUD.cpp`：查看/报价/确认状态与真实终端绘制；PlayerController页签使用枚举。
- `Weapons/DemoWeaponBase.cpp`、`DemoWeaponComponent.cpp`、`Characters/DemoCharacter.cpp`：弹道、装配组件和恢复接入。
- `AI/DemoEnemy.*`、`DemoAttackPulse.*`、`UI/DemoEnemyHUD.cpp`、`Game/FPSDemoGameMode.cpp`：移动控制、表现、层数与阶段清理。
- `Save/DemoRunSave.*`、`Backend/FPSDemo.Api/{Contracts,ProfileService,Program}.cs`：V4格式、校验和兼容；Profile/CloudSync测试隔离名单同步新增DemoAmmoTest。
- `Tests/DemoAmmoTest.*`、`FPSDemoEditor/Tests/DemoEditorSaveTest.cpp`、`Backend/Tools/Test-Backend.ps1`：玩法/存档/HTTP回归。
- `Content/Data/Ammo/DA_AmmoCatalog.uasset`、`Tools/Unreal/create_ammo_assets.py`：Editor创建的目录与可重跑脚本。

新增函数/回调按DemoLog记录入口，拒绝分支记录原因；高频绘制/查询使用VeryVerbose。主要过滤词为AMMO_CONFIG、AMMO_PURCHASE、AMMO_EQUIP、AMMO_STACK、AMMO_FROST_REJECT、AMMO_PIERCE。数值单位和变量生命周期就地注释，日志不记录云正文或认证秘密。不要删除稳定ID、改目录顺序或修改运行中的共享GE CDO。

## 验证记录与复验方法

2026-09-21已完成：

- Editor Development编译通过，日志 `Saved/Logs/AmmoBuildFinal.log`。
- 真实地图 `-DemoAmmoTest` 输出 `DEMO_AMMO_TEST_SUCCESS`：取消/重复购买、独立价格、购买不自动装配、V4钱包与解锁、真实Fire GA、周期灼烧、第5层单次爆炸、冰冻减速/定身/免疫到期、穿透半伤、墙体阻挡、散弹一枪一层、死亡清理、重开保留。
- `FPSDemo.Editor` 三项通过：暂停输入、独立试玩临时存档、两次PIE启停与迁移/JSON往返；正式档案未改动，Editor未联网。日志 `AmmoEditorRegression.log`。
- .NET后端构建0警告/0错误，隔离账号实际HTTP/MongoDB共23项通过，包括V4上传、幂等重放、拒绝未解锁选择、金币/弹药原样读取。日志 `AmmoBackendIntegration.log`。
- 真实1280×720截图 `Saved/Screenshots/WindowsEditor/AmmoTerminalGame.png` 已检查，四张既有图标、选中项、详情和按钮完整。

复验启动真实Editor游戏时指定 `-DemoAmmoTest -unattended -nosound -LogCmds="LogFPSDemo Log"` 并打开 `/Game/Whitebox/Maps/L_ThreeSector_Whitebox`；测试自动隔离存档，正常约15秒退出。该夹具针对默认平衡数据；大幅调整时长/价格后需同步测试期望，不能为通过测试改回策划资产。

待扩展人工验收：所有异常配置组合、磁盘故障注入、配置实时改价、第三目标/最大射程/多目标散弹、Boss前摇与冻结叠加、全部分辨率交互和完整多人行为。不得把已通过的基础回归当作上述项目全部验证。

### 打包与协议补充验证

Development BuildCookRun（编译/Cook/Stage/Archive）通过，验证包位于`Saved/AmmoPackagedValidation/Windows/FPSDemo.exe`。打包版`-DemoAmmoTest`同样输出成功，真实1024×768截图已检查；原武器`-DemoWeaponTest`也通过，包括真实按键、四种武器、空弹匣再按射击自动装填、备用弹药、装填取消、散弹和狙击镜。日志为`AmmoPackage.log`、`AmmoPackagedRuntime.log`、`AmmoWeaponRegression.log`。

打包HTTP回归发现FName大小写差异：非Editor可能把normal表示为先注册的Normal，后端白名单区分大小写。ExportCloud现在显式输出小写unlockedAmmoIds/selectedAmmoId；Editor协议测试补充四类型JSON值校验及往返，`AmmoEditorV4Final.log`通过。此检查不能只依赖Editor的UStruct往返，因为FName比较本身不区分大小写。Editor测试模块为此增加Json私有依赖，不改变运行时网络边界。

最终打包云退出回归使用独立GUID和本机API，`AmmoPackagedCloudFinal.log`输出`DEMO_EXIT_CLOUD_SUCCESS`：旧请求在途时产生的新检查点也得到HTTP 200确认后，保存状态UI才进入成功并退出。该测试覆盖V4正常存档、真实发件箱及退出屏障；特殊解锁集合的服务端校验另由23项HTTP测试覆盖，不把此测试宣称为每种弹药的跨设备购买恢复验收。

最终`AmmoPackagedRuntimeFinal.log`再次通过整套弹药回归，并确认武器档案使用DemoProfileTest_GUID且退出清理。1920×1080实际截图也已检查，与1280×720、1024×768一致无截断；截图位于验证包Saved/Screenshots/Windows/AmmoTerminalGame.png，1024截图副本位于Saved/Tests/AmmoTerminal1024.png。截图验证不等于所有分辨率的人工鼠标交互验收。
