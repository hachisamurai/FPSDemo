# BREACH UI 设计与运行时接入 v1

2026-09-21 新增六张状态/弹药图标（火焰、冰块、普通子弹、火焰子弹、冰冻子弹、穿透子弹）及火焰/冰块世界空间材质。敌人旧血量文字已改为头顶屏幕空间进度条，旁边按GAS标签显示火/冰图标，摄像机转向时保持水平。透明源图、UE资源、显示契约和验证边界统一维护在[元素图标](元素图标.md)；显示接入不等于元素伤害、叠层或特殊弹装配已经实现。

## 目的与交付范围

初版以单人三关 GAS Demo 为基础，提供可切换场景、可操作按钮的设计预览。本轮已将视觉设计应用到 UE 的原生 `ADemoHUD`：中文运行时字体、圆角深色面板、青色操作按钮、金色金币、三选一卡片、属性前后对比、底部技能与弹药、上置 Boss 血条和胜败结算。不依赖浏览器，不需要 UMG 蓝图。

会话里的交互预览仍是独立审阅原型，不调用 UE、不写游戏存档。下面的“三关、5/8/6”等数值保留为初版设计快照；当前工作区的大厅、难度和十关配置由独立玩法改动引入，运行时 HUD 的关卡总数读取 `DemoCombatConfig::LevelCount`，不再硬编码三关。玩法数据以配置表和 GAS 为准。

最新运行时流程：前九关清关不再返回安全区，免费选择后留在本关 Intermission，中心的一对终端用于购买和进入下一关。HUD 显示“原地备战”和中心导航，免费奖励说明不再提示回安全区。实际死亡的结算按钮为“返回安全区 · 重新挑战”，重开清空金币/升级并保留难度；胜利和系统错误仍显示返回大厅。以下历史原型流程不是当前实现的验收依据，当前规则与接口见 [关卡与经济](关卡与经济.md)。

下一关终端现增加独立确认框：E 交互只打开“是否进入下一关？”，显示目标编号；点击“是”或按 Enter 才进入，点击“否”或按 Tab 只关闭窗口。安全区初次出发也使用同一界面。窗口显示时隐藏中央场景导航，保留周边状态信息，鼠标可用而移动/视角锁定；确认与升级菜单互斥。实现由 `DrawNextLevelConfirmation` 绘制、`NextLevelYes/NextLevelNo` 热区路由到控制器；关闭后热区在下一帧移除，旧点击仍由控制器拒绝。验证覆盖实际缩放坐标点击、取消恢复输入、重复确认、弹窗死亡清理；功能目的、完整调用流程、边界及维护约束统一见 [下一关出发确认](关卡与经济.md#下一关出发确认)，不使用下文历史原型的直接跳关规则。

可编辑预览位于当前会话的 `breach-ui-preview.html`；参考图保存到 `Art/UI/PreviewV1/`。背景来自 `Tools/Blender/render_ui_background.py` 对现有白模的干净第一人称渲染，输出 `Art/Whitebox/Previews/UI/`。没有将旧 HUD 烘焙到背景中。

## 设计理念

- 战斗 HUD 沿屏幕边缘排布：左上目标与剩余敌人，右上金币，下方生命、两项技能和弹药；中央保持射击空间。
- Boss 血条上置于中间区域，避免遮挡准星；血量与剩余敌人同时呈现，避免误以为击杀 Boss 即通关。
- 清关奖励使用三张等权卡片，“免费、三选一、限一次”明确可见，无关闭按钮。
- 金币商店使用纵向属性行，显示当前值 → 购买后值、实际价格与即时生效反馈；可重复购买，全部商品随购买次数统一涨价。
- 安全区交互提示显示 E 与行为名称，终端、出发入口分别提示；战斗技能在安全区显示锁定。
- 配色以青色强调可操作项，金币单独使用金色；浅色/深色随宿主外观调整，可在宿主设计选项切换青色/琥珀色、圆角和购买详情。
- 不虚构护盾、体力、备用弹药、地图雷达、存档、暂停或局外永久成长等当前没有的系统。

## 界面与状态对应

| 界面 | 游戏状态/条件 | 展示与动作 |
|---|---|---|
| 安全区 | Hub | 下一关目标；走近终端或出发点后 E；无敌人攻击 |
| 战斗 HUD | Combat | Health/MaxHealth、当前武器弹药/容量/备用与弹丸伤害、技能冷却、剩余敌人、击杀、金币 |
| 清关奖励 | Reward，第1/2关 | 攻击 +10、治疗恢复量 +20、冲刺速度 +350 cm/s，三选一，随后 Hub |
| 升级终端 | Hub，距终端≤250cm，已交互 | 伤害 +5；生命上限 +25 并恢复25；弹匣 +4 并补4发 |
| Boss 战 | Combat，第3关 | Boss 650 初始生命；6小怪+Boss，全部击杀才 Victory |
| 胜利 | Victory | 三关完成；累计击杀/剩余金币/购买次数；Enter 重开 |
| 失败 | Defeat | 失败关卡；同样提供统计和完整重开 |

预览顶部场景按钮与底部“模拟事件”属于设计审阅工具，不是游戏内 UI。点击场景按钮会载入独立示例状态，不能据此在游戏中跳关。默认展示商店示例：第一关已清、50金币、选择过伤害奖励所以攻击35。

## 核心运行流程与数值

1. 默认游戏开局 Hub：100生命、25伤害、12弹匣、0金币；所有主动技能在安全区禁用。
2. E 进入关卡，分别生成5、8、6个普通怪；第3关额外一只 Boss。
3. 武器HUD读实例名称、槽、弹药/容量/备用与每弹丸伤害。射速/消耗/装填由BP配置；当前默认步枪每发1、0.18秒间隔、1.4秒装填。狙击镜用物理像素正圆遮罩，开镜不绘制普通准星。伤害/容量升级显示全武器加成，避免误标单枪基础属性。
4. 冲刺4秒冷却、初速度1300cm/s；治疗12秒冷却、恢复35，满血不可用；清关强化只增加对应数值，不修改冷却。
5. 小怪10金币，Boss50；普通怪生命60/80/100。预览普通攻击事件伤害为首关9、第二关11，Boss范围攻击事件24。
6. 前两关清场，补满生命弹药并免费选能力；选择后返回安全区，可消费或主动进入下一关。
7. 商店成本 `20 + Purchases * 10`，所有类别共用涨价次数，校验余额/位置/状态后立即扣费加属性。余额不足、距离过远按钮禁用，键盘入口同样校验。
8. 胜敗 Enter 重置整局。预览用局部状态重置对应 UE 的 OpenLevel。

预览冷却由本地计时器表现；不是 UE GAS 的实际运行。射击/受伤/击杀/清场由下方模拟操作触发，不模拟敌人AI、场景碰撞和真实战斗。Boss场景“射击命中”默认指向Boss，小怪通过独立事件模拟。

## UE 接入接口建议

| UI 数据/事件 | 已有接口 |
|---|---|
| 生命/弹药/攻击 | UDemoAttributeSet 对应属性；ASC 属性变化委托 |
| 冷却 | UDemoAbilitySystemComponent::GetCooldownRemaining + DemoTags |
| 装填 | Demo.State.Reloading |
| 阶段/金币/击杀/剩余敌人 | ADemoGameState |
| 交互提示 | ADemoCharacter::FindInteractable / ADemoInteractable::GetPrompt |
| 商店与奖励选择 | ADemoPlayerController::SelectUpgrade → GameMode 验证 |
| 关闭商店 | CloseUpgradeMenu；Reward阶段禁止关闭 |
| 重开 | RestartPressed → RestartDemo |

运行时现已拆为 `DrawStatus`、`DrawUpgradeMenu` 和 `DrawEndScreen`；控制器负责输入模式与光标，Canvas HitBox 只发送意图，不直接改 GAS 属性或金币。`GameMode::GetPurchaseBlockReason` 为界面和交易提供共用的阶段/角色/终端/250cm距离/余额验证；空串表示可购买，失败原因以中文显示。实际购买仍重新检查，避免沿用旧帧按钮状态。

## 使用、边界与验证

- 预览直接点击场景按钮、购买按钮或奖励卡片。焦点在预览内时，1/2/3选择、E交互/关闭、R装填、Q治疗、Shift冲刺、Enter终局重开；不会拦截其他应用输入。
- 高低宽度自适应，窄屏奖励卡片纵向排列；窄屏是审阅兼容布局，不表示游戏新增触屏操作支持。
- 所有功能函数与回调使用 `[BreachPreview]` console.debug 调用日志。计时回调只保留当前片段局部状态，卸载时清除；宿主状态恢复不会再次触发保存，防止循环。
- 宿主状态保存可选，失败不影响按钮行为；恢复时校验版本与基本字段。
- 已用无头浏览器验证商店20/30扣费、余额不足、离开范围禁用、免费选择不可重复、两次奖励及三关流程、Boss最终金币、重开清零、真实时间装填与治疗冷却，以及窄屏不横向溢出。
- 初版已查看商店/奖励/战斗截图；该阶段仅验证原型交互与布局，原生 HUD 的实装验证见下文。

## 影响文件及维护事项

初版新增 `Tools/Blender/render_ui_background.py`、`Art/Whitebox/Previews/UI/*.jpg`、`Art/UI/PreviewV1/` 参考图和本文；可交互预览保留在会话专属可视化目录。

运行时改动：`UI/DemoHUD.*`、`Player/DemoPlayerController.cpp`、`Game/FPSDemoGameMode.*`、`Characters/DemoCharacter.*`、`Interaction/DemoInteractable.cpp`、`FPSDemo.Build.cs`，以及独立界面回归 `Tests/DemoUIValidation.*`。购买价格、奖励增量、GAS结算和技能冷却规则未由 UI 改动；角色仅新增只读冲刺速度接口。

## 运行时绘制、输入与生命周期

1. HUD BeginPlay 创建强引用的 transient `UFont`，复制引擎复合字体（Roboto + DroidSansFallback），让中英文在 Canvas 运行时缓存中渲染。UE5.4 的 `FCanvasTextItem` 即使传入 SlateFontInfo 也要求非空 UFont，不可删去该对象。
2. 每帧借用当前 World 的控制器、角色、GameState、AttributeSet/ASC，重开或换 Pawn 后不使用旧引用。数据未就绪显示初始化提示；HUD 回收时字体跟随 GC。
3. 设计坐标基准 1280×720，取视口宽高最小比例等比缩放；局内菜单居中、状态栏沿边缘定位，大厅单独锚定左侧偏上。HitBox 使用完全相同的像素变换。字体按当前比例重新栅格化。面向键鼠桌面，竖屏不新增触屏操作。
4. 配色常量集中在 `DemoUI` 命名空间，sRGB 设计色先转线性颜色，避免输出二次 gamma 使面板泛灰。圆角使用单个三角扇，半透明像素不重复叠加。
5. 可用按钮和整张奖励卡片支持鼠标悬停变亮；按钮关闭后不注册热区。数字键仍进入同一控制器校验。商店可 E/Tab 关闭，奖励必须选择一次。打开菜单会停火并锁移动/视角，关闭恢复游戏输入。
6. 技能状态来自真实 GameplayTag/冷却读取：战斗外锁定、冷却、满血不可治疗、就绪分别展示；装填中和命中/受伤提示继续由真实 GAS 与角色事件驱动。

### 准星命中反馈

准星保持原灰白色，击中敌人不再变青色。`DemoWeaponBase::PerformBallistics`结算命中时更新角色`LastHitTime`，`DemoHUD::DrawStatus`在Combat读取该World时间，命中后0.15秒绘制四段红色短线。每段位于中心四个45°斜方向，X/Y偏移7至14设计像素，线宽2设计像素，统一乘UIScale，中心留空；连续命中刷新显示时长。开镜时同样显示该标记，镜内十字颜色保持原样。

只修改`UI/DemoHUD.cpp`的展示，不新增计时器或伤害判定；未命中不更新时间，负时间差不显示，非Combat/暂停覆盖页沿用原HUD路由。入口保留`DEMO_LOG_TICK()`。调整显示时间、线长/宽或颜色时维护本节；验证普通射击命中、未命中、连续命中及开镜，确认标记短暂出现后消失、准星颜色不变。

`Tests/DemoAmmoTest.cpp`的现有真实射击回归可追加`-DemoHitMarkerCapture`，保存`HitMarker-On.png`与`HitMarker-Off.png`，分别观察直接命中当帧及灼烧周期后的状态；不通过手写LastHitTime伪造命中。截图开关仅影响记录，不改变测试/玩法数据。

2026-09-21验证：`HitMarkerBuild.log`完整Editor Development构建通过；`-DemoAmmoTest -DemoHitMarkerCapture`真实渲染回归输出`DEMO_AMMO_TEST_SUCCESS`（`Saved/Logs/HitMarkerValidation.log`）。已查看1280×720命中/消失截图，四段红线和灰白准星同时可见，稍后只剩灰白准星；截图归档`Art/UI/HitMarker/1280x720/`。计算HitAge时先将World时间转换为与LastHitTime相同的float精度，避免同帧double/float差值出现微小负数而漏掉首帧。测试原先写死V4版本的交易断言已改为核对当前SaveGame默认版本，保留金币/解锁/选择的全部断言；不修改存档迁移规则。本次未单独截图验收狙击镜、其他分辨率或Shipping包。

所有新增功能入口使用 `DEMO_LOG_CALL`，绘制、辅助绘制和只读高频函数使用 `DEMO_LOG_TICK`。日志仍受项目统一级别控制；不会因刷新频繁而删除埋点。字体与圆角新增 `SlateCore` / `RenderCore` 私有模块依赖，未引入 Lyra 或第三方 UI 插件。

## 实装验证方式

编译：使用本机 UE5.4.4 `Build.bat FPSDemoEditor Win64 Development -Project=... -WaitMutex -NoHotReloadFromIDE`。界面专用回归在真实 Standalone World 运行，必须启用渲染，不能使用 `-nullrhi`：

```powershell
& 'F:/UE5.4.4/UnrealEngine-5.4.4-release/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'F:/UE5.4Project/FPSDemo/FPSDemo/FPSDemo.uproject' /Game/Whitebox/Maps/L_ThreeSector_Whitebox -game -RenderOffscreen -ResX=1280 -ResY=720 -unattended -nosound -DemoUIValidation '-LogCmds=LogFPSDemo Log' '-abslog=F:/UE5.4Project/FPSDemo/FPSDemo/Saved/Logs/UIValidation.log'
```

独立界面测试不与 `-DemoSmokeTest` 同时启用。检查真实 HitBox 是否存在、交易被禁用时快捷键仍被拒绝、免费奖励不可关闭、范围变化、20/30扣费与下一次涨价、通关/失败结算与重开；截图写到 `Saved/Screenshots/DemoUI/`。清场使用测试 GE，并非玩家完整操作或平衡性验收。`DEMO_UI_SUCCESS` 表示通过，失败日志为 `UI CHECK FAIL`，每世界90秒超时。Shipping 不生成测试 Actor。

2026-09-20 UE5.4.4 Editor Development 编译已通过；1280×720 真实渲染回归输出 `DEMO_UI_SUCCESS`（`Saved/Logs/UIValidation.log`）。已检查大厅、商店、奖励、Boss 与失败界面截图。1280×720 图归档于 `Art/UI/AppliedV1/1280x720/`，会话设计图仍保留在 `Art/UI/PreviewV1/`，二者不要混用。

1024×768 回归同样通过（`Saved/Logs/UIValidation4x3.log`），并检查开始按钮、奖励卡片、购买按钮在等比缩放后的实际视口命中点；截图 PNG 尺寸已确认为1024×768，归档于 `Art/UI/AppliedV1/1024x768/`。本机离屏运行只传 `ResX/ResY` 会被1280×720默认值覆盖，复现其他比例需同时传 `-windowed -ForceRes '-ExecCmds=r.SetRes 1024x768w'`，不能仅根据命令行参数宣称验证了目标尺寸。

尚需人工验收窗口焦点、多显示器 DPI、系统级鼠标点击与各自偏好的实际分辨率；测试会计算视口坐标并查询引擎真实 HitBox，再派发点击回调，不操作系统鼠标。打包版字体资源尚未通过 Shipping 验证。

玩法数值或阶段规则变动，需同步本设计、预览状态机以及 [GAS架构](GAS架构.md)、[关卡与经济](关卡与经济.md)。基础设计与运行时权限分离，最终实现必须继续由 GameMode/GAS 做条件检查，不依赖界面的禁用状态。

## 大厅图片背景与左侧半透明面板

当前流程更新（2026-09-20）：下述布局历史记录中的大厅难度按钮和设置占位页已移除。主大厅仍保留背景与左侧面板，开始打开三个存档栏；设置、暂停和确认页使用居中720×520设计布局，安全区下一关终端显示三档难度。实际接口、存档规则、截图及验证以[暂停设置与检查点存档](暂停设置与检查点存档.md)为准。历史截图只说明当时美术方案，不代表当前按钮。

### 目的与设计

使用用户提供的 `UE_Lobby_Background_Sharp_Gear_No_Eye_Glow_2K.png` 作为大厅全屏背景，替换初版人物图；原图不修改、不裁成新文件。把主菜单和设置占位页收拢到左侧窄面板，保留图片中心人物。大厅功能和难度/开始/设置/退出路由保持一致。

面板宽404、高472设计像素，左边距默认44；面板垂直中心位于视口高度43%，即“左边中间偏上一些”。面板底色为中性深灰 `#181818`，默认不透明度45%（透明度55%），文字保持不透明以便阅读；大厅及设置页不再使用偏蓝的通用 Surface 底色。大厅按钮统一采用中性灰色半透明底，避免青色选中底与背景冲突：普通状态为 `#4E4E4E / 42%`，悬停为 `#787878 / 60%`，选中难度为 `#888888 / 62%`，选中后悬停为 `#A0A0A0 / 68%`。选中难度附带灰白细下划线，区分持久选择和临时悬停。有启动错误时面板向下扩展，保留安全边距，不将长诊断文本铺满背景。

### 资源与运行流程

1. 原图逐字节复制到 `SourceAssets/UI/Lobby/T_LobbyBackground.png`，与下载文件哈希一致。项目不在运行时访问 D 盘下载路径。
2. `Tools/Unreal/import_lobby_background.py` 用 AssetImportTask 导入 `/Game/UI/Textures/T_LobbyBackground`，设置为 UI 纹理、sRGB、无 Mip、NeverStream 和 RGBA UI 压缩类型，避免流送时先显示低清版本。该脚本只更新这个指定资产，源 PNG 可用于重复导入。
3. `ADemoHUD` 构造函数硬引用该 Texture2D，Cooker 可追踪资源依赖；Lobby 阶段先 `DrawLobbyBackground`，再绘制半透明面板及按钮。设置页共用背景和锚点，开始游戏后切回常规 HUD。
4. 背景以 cover 方式居中裁切 UV 铺满视口，不改变源图宽高比；16:9 几乎展示整图，4:3 裁掉左右，超宽屏裁掉上下。图片资源缺失或尚无渲染资源时回退为深色纯色并记录日志，不暴露大厅下面的白模场景。
5. `LobbyButton` 每帧读取真实鼠标热区与当前难度，绘制灰阶底色、清晰文字与选中下划线，然后用相同坐标注册 HitBox；入口使用 `DEMO_LOG_TICK()`。开始、难度、设置、退出及设置页返回按钮共用该样式。局内的 `Button` 仍使用原有外观。点击区域和文字不受底色透明度影响。

### 配置、依赖与维护

`DemoHUD` 类默认值 `Demo|Lobby` 中可配置 `LobbyBackground`、`LobbyPanelOpacity`（0.15..0.95）、`LobbyAnchorY`（建议0.43）与 `LobbyLeftMargin`（设计像素）。生产布局会限制面板位置以预留错误提示空间。改变锚点默认值后，需同步 `DemoUIValidation::ClickMenuPoint` 对大厅开始按钮的预期坐标。

导入命令：

替换图片时先覆盖项目内固定源 PNG，再运行导入；若正在运行的编辑器锁定旧资产，保存关闭后重试。遇到 Error 32 或保存失败时不能把源文件已复制等同于运行时资产已更新，也不能强制关闭可能含未保存内容的编辑器。

```powershell
& 'F:/UE5.4.4/UnrealEngine-5.4.4-release/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'F:/UE5.4Project/FPSDemo/FPSDemo/FPSDemo.uproject' -run=pythonscript '-script=F:/UE5.4Project/FPSDemo/FPSDemo/Tools/Unreal/import_lobby_background.py' -unattended -nop4 -nosound
```

影响文件：`UI/DemoHUD.*`、`Tests/DemoUIValidation.*`、导入脚本、源 PNG、`Content/UI/Textures/T_LobbyBackground.uasset` 与本文。资源依赖为现有 PythonScriptPlugin（仅导入时）、Engine Texture2D 和 Canvas；无需额外运行时插件。

验证重点：导入输出 `LOBBY_BACKGROUND_IMPORT_SUCCESS`；检查大厅/设置页背景可见、人物无拉伸、按钮无遮挡；检查左侧开始按钮在16:9/4:3真实热区命中、设置页返回，以及进入战役后不残留大厅背景。图像可在 `Saved/Screenshots/DemoUI/00-Lobby.png` 和 `00-LobbySettings.png` 查看。

2026-09-20 初版背景接入验证：Editor Development 编译通过（`Saved/Logs/LobbyBuild.log`），1280×720 和1024×768真实渲染回归均输出 `DEMO_UI_SUCCESS`（`LobbyUIValidation.log`、`LobbyUIValidation4x3.log`）。初版大厅/设置页实机截图已查看并归档至 `Art/UI/LobbyBackground/1280x720/` 和 `1024x768/`，PNG尺寸已核实。这些历史截图使用旧背景和旧按钮配色，不作为本次换图的验证结果。未执行 Shipping 打包。

本次更换背景与灰阶按钮：源图副本 SHA256 为 `B10B5DACA50999ACDBF5063F24BD449049A762122A96CBEB79A1C4F8C9876167`，与用户提供文件相同。保存关闭编辑器后重新导入成功（`Saved/Logs/LobbyGrayImport.log`），Editor Development 编译通过（`Saved/Logs/LobbyGrayBuild.log`）；1280×720 真实渲染回归输出 `DEMO_UI_SUCCESS`（`Saved/Logs/LobbyGrayUIValidation.log`）。已检查大厅和设置页截图，新背景、灰阶按钮、当前难度下划线及左侧半透明面板正确显示；截图归档至 `Art/UI/LobbyGray/1280x720/`。本次未重测4:3、系统鼠标悬停操作或 Shipping 打包；此前4:3结果只覆盖相同布局的初版配色。

后续面板修正：`DrawLobby` 的容器背景独立改为中性灰 `#181818`，`LobbyPanelOpacity` 默认值由0.58降为0.45；主菜单、设置占位页及启动错误扩展面板共用此设置。绘制仍沿用原有 `DEMO_LOG_TICK()` 入口，布局与热区不变，文字和按钮各自绘制，不对整个 UI 统一降透明度。若派生蓝图保存了旧不透明度覆盖值，应在类默认值中重置该字段或手动设为0.45。

面板修正历史验证：`Saved/Logs/LobbyNeutralPanelBuild.log` 曾因同期武器类实现尚未补齐而链接失败。后续武器库接入时已完成Editor Development构建（`Saved/Logs/ArmoryBuild.log`），1280×720界面回归输出 `DEMO_UI_SUCCESS`（`ArmoryUIRegression.log`），该构建已包含中性灰45%不透明面板。上文 `LobbyGray` 仍为按钮灰阶版本历史截图。

## 终端武器页

升级终端右上新增“属性升级 / 武器”页签。武器页按固定目录显示手枪/步枪/散弹枪/狙击枪四张卡，已解锁使用正常前景色，待解锁整体灰色；均支持点击查看条件。Card优先显示武器蓝图Config中的Weapon Icon软引用纹理，打开页面时异步加载并缓存，未配置或加载失败时使用原生轮廓；纹理等比居中，锁定时保留Alpha并转灰。卡片还显示名称、蓝图基础伤害×弹丸数、RPM、弹匣、装填秒数。说明框为模态层，下层热区停止注册；未解锁只有返回按钮，已解锁主武器可明确点击装备，手枪说明固定副槽。

武器页与属性商店共用960×480设计面板与居中缩放；不改变大厅布局。数字快捷键在武器页不购买属性；Tab先关说明再关终端。玩法权限、持久化结构、失败处理和未来上传接口统一维护于[武器库与玩家存档](武器库与玩家存档.md)，不在UI另存解锁列表。
