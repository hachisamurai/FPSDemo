# Breach 四枪换弹机械资源

2026-09-22左手接触重制同步将Pistol/Rifle/Shotgun/Sniper完全拔出阶段下移改为12/6/8/7cm（交接段再增加最多3cm），保留原片长和声音节点。手腕、掌心朝向、15根指骨及外侧绕行由`Art/Player/Animations/left_hand_contacts.json`和接触作者生成，不能继续使用旧`magazine_grip_cm`推算新版左手。旧字段保留源清单兼容，当前导入读取新接触轨迹。真实UE全枪接触验证、维护流程与最新结果见`Documentation/武器系统.md`的2026-09-22记录及`Art/Player/Animations/README.md`；本文件下方2026-09-21数值属于历史对照。

本目录是 `Tools/Blender/build_breach_weapon_animations.py` 的独立输出。源模型读取自 `Art/Weapons/Models/Breach_Weapons.blend`，不会保存或覆盖原静态模型。运行框架、GAS和主/子AnimBP说明统一维护在 `Documentation/武器系统.md`；本文件只记录离线骨骼和动作资源契约。

## 文件与使用

- `Breach_{Pistol,Rifle,Shotgun,Sniper}_Reload.blend`：每枪可编辑的独立零件、刚性蒙皮、普通/空仓两个Action和预览相机。
- `FBX/SK_Breach_{Type}.fbx`：四个轻量骨骼枪，所有顶点权重恰为一个骨的1.0。
- `FBX/A_Breach_{Type}_Reload_{Tactical,Empty}.fbx`：八个机械Sequence，仅根以下的弹匣、套筒/枪栓活动。手臂动作由独立的共享手臂资产生成流程负责。
- `FBX/SM_Breach_{Type}_Magazine.fbx`：四个弹匣静态表现件，原点为整体包围盒中心。首版直接用实际magazine骨同步手部，不同时显示此副本。
- `Previews/{Type}_{Neutral,MagazineOut,Mechanism}.png`：真实骨骼几何渲染。需要再结合手臂和第一人称相机在UE内验证动作观感。
- `weapon_animation_manifest.json`：单位、骨、挂点、动作、手部目标、声音时刻和美术变更清单。
- `fbx_validation.json`：重新导入实际FBX后的离线检查结果，不代表UE或联机运行已经验证。

后台重建命令（在项目根目录运行）：

```powershell
& 'F:/Blender/blender.exe' --background --python-exit-code 1 --python Tools/Blender/build_breach_weapon_animations.py
```

只复查现有FBX、不重写Blend/FBX：

```powershell
& 'F:/Blender/blender.exe' --background --python-exit-code 1 --python Tools/Blender/build_breach_weapon_animations.py -- --validate-only
```

生成器是确定性离线工具，会覆盖本目录同名产物。手工编辑前请另存；需要保留的形状/动作变更应同步回脚本。函数和逐帧采样入口均打印 `[WeaponReload] call`，建议重定向日志到 `Saved/Logs/BlenderWeaponAnimationsBuild.log`。退出码和成功标记应一起检查。

## 骨与坐标

所有JSON位置为 **UE模型空间厘米，+X枪口、+Y右、+Z上**。Blender场景本身用厘米单位并镜像Y，配合已验证FBX轴导出管线。导入UE使用比例1，不另外乘100。

- 共同骨：`weapon_root → body → magazine`。
- 手枪：`body → slide`，合计4骨。
- 步枪/散弹：`body → bolt → charging_handle`，合计5骨。
- 狙击：`body → bolt`，合计4骨。

`weapon_root/body`全程恒定；所有骨Scale恒为1。枪体整体举起/倾斜由手臂的稳定武器锚点完成，不能再次叠加到本目录枪根。

手枪原静态源没有完整内部弹匣，仅有握把底板。本次新增隐藏在握把内的弹匣盒体，底板与盒体共用magazine骨。步枪/散弹在既有枪机上增加可抓取的拉机柄。狙击枪栓柄只在新骨骼源中移至+Y侧，配合已确认右手拉栓；旧静态源不变，排壳口饰板固定在机匣避免随栓柄旋转穿模。

材质角色沿用 `M_Breach_Body/Armor/Steel/Rubber/Cyan/Glass/Bore`，UE目录 `/Game/Weapons/Materials/Breach`。不能依据材质槽序号硬编码，必须按导入槽名匹配。

## 动作阶段与声音

固定60fps，手枪1.2秒、步枪1.4秒、散弹2.2秒、狙击2.5秒。普通/空仓总时长相同；运行时按武器ReloadSeconds统一缩放手臂、枪械和事件时间。

|阶段|Tactical|Empty|
|---|---:|---:|
|手靠近|0.10|0.08|
|MagOut / detach|0.20|0.16|
|完全拔出|0.32|0.28|
|新匣接近|0.52|0.43|
|MagIn / attach|0.60|0.50|
|MagSeat|0.68|0.57|
|恢复准备姿势|0.92|0.96|

额外空仓机械声音时刻：

- 手枪：`SlideRelease=0.78`；首帧套筒后锁，释放时前移撞止动。
- 步枪：`ChargingHandle=0.68`、`BoltOpen=0.74`、`BoltClose=0.82`。
- 散弹：`ChargingHandle=0.68`、`BoltOpen=0.76`、`BoltClose=0.84`。
- 狙击：`BoltOpen=0.68`、后拉到位0.74、前推到位0.83、`BoltClose=0.88`。

SoundEvents仅产生表现，不能结算弹药。首尾已回到机械中立（手枪空仓首帧例外为后锁，末帧中立），中断可以直接复位。弹匣全程可见，轨迹向视野下方退出再回插，用于手臂按实际骨采样握持；首版不依赖某个Notify隐藏骨，也不生成世界掉落物。

拔匣幅度按“弹匣顶部完全退出机匣”计算，不按整个弹匣长度向下移。2026-09-21根据真实55.022cm模板手臂链和最终镜内锚点校正：步枪在完全拔出阶段下移6cm、交接最深9cm；散弹下移11cm、最深14cm。保持所有阶段时间与声音时点不变。实际逐顶点检查完全拔出时，步枪弹匣顶部比机匣开口低2.71cm，散弹低7.05cm；长枪旧27/22cm下移让手腕目标超过链长约11.42/4.19cm，因此已移除该旧路径，不能通过拉伸手臂或放宽验证阈值掩盖问题。

## JSON字段契约

JSON本身不支持注释，字段用途在此维护：

- 顶层 `version=1` 是结构版本；`units/coordinates/fps/root_motion` 定义坐标、采样率和禁止根运动；`materials_directory`为UE共享材质目录。
- `weapons[].model/mesh/skeleton/blend`：型号与稳定资源命名。Skeleton按枪型独立，不强迫不同尺寸枪共享参考骨位置。
- `bones[].name/parent/head_cm`：骨名、父级和UE空间骨枢轴。导入后Socket须基于真实参考矩阵转换，不能直接把模型坐标写成骨局部坐标。
- `bounds_cm/triangles`：整枪参考包围盒、LOD0三角数；用于检查导入轴/单位错误。
- `sockets.{Muzzle,Grip_L,Sight,Grip_R}.bone/world_cm`：Socket父骨与UE模型空间位置。
- `magazine_center_cm`：弹匣骨枢轴和手持静态表现件原点。
- `magazine_withdraw_cm/magazine_max_drop_cm`：完全拔出阶段和交接最深阶段的下移幅度，厘米；用于防止未来改轨迹重新超出手臂可达范围。
- `magazine_grip_cm`：参考姿势下左手腕目标；`magazine_grip_offset_cm`是相对于弹匣枢轴的模型空间偏移，默认[-2,-5,-1]厘米。手指贴合仍由实际手臂骨架校正，不能当作任意骨架的通用握持姿势。
- `bolt_grip_cm`：机械操作手腕目标。狙击为[-2.7,10.8,9]厘米，手枪为套筒后部[-4,-3.5,10.8]，其他枪依据实际拉机柄位置取样。`mechanism_grip_bone`指明目标跟随的slide或bolt骨。
- `model_changes/magazine_visibility`：本次新增机械件、右侧狙击栓柄、以及首版不启用重复弹匣显示的说明。
- `magazine_presentation.mesh/origin_cm/bounds_local_cm/size_cm`：可选静态弹匣资源和原点/局部边界/尺寸，厘米。
- `material_roles`：本枪使用的真实材质名称；`parts[].name/rigid_bone/vertices`保留硬表面部件归属，便于未来改模时检查蒙皮。
- `clips[].name/variant/seconds/frames/fps/loop`：动作资产、普通/空仓版本、秒数、帧间隔数（不是含端点样本数）、采样率与非循环标识。
- `clips[].stages`：归一化阶段0..1；`sound_events[].name/phase`为音效语义和归一化触发时间。
- `clips[].hand_targets[].phase/magazine_center_cm`：选定时刻实际弹匣中心位置，厘米，便于离线制作交叉检查。运行手臂离线烘焙应优先采样实际导入Sequence，而非只在这些稀疏记录间插值。
- `clips[].magazine_opening_z_cm/magazine_top_at_withdraw_cm/withdrawn_clearance_cm`：机匣开口高度、实际刚性弹匣最高顶点高度、二者差值，厘米。生成时必须至少有0.3cm完全脱离净空。

## 边界、验证与维护

任何源文件/材质/骨缺失、非刚性权重、根运动、骨缩放、FBX轴/单位或时长不符都抛出异常；失败前可能已写部分专属产物，修正后幂等重跑。导入器不得将缺失资源静默视为制作完成。

2026-09-21实际后台生成4骨骼枪、8机械Sequence、4可选静态弹匣成功；4个网格FBX与8个动作FBX重新导入验证通过，所有顶点单骨1.0权重，包围盒误差小于0.00001cm，八个动作逐帧检查根恒定、Scale=1、末帧复位。已查看手枪拔弹匣和狙击拉栓真实几何预览。此记录仅包含Blender与FBX离线验证，UE导入、手臂贴合、画面和音效播放由整体模块的后续验收记录负责。

改时长、关键阶段、机械骨或抓握位置后，必须同步手臂烘焙、声音事件配置、导入校验和 `Documentation/武器系统.md`，避免两套素材只有总长相等却阶段不一致。旧模型源和旧静态FBX保持可追溯。

## 换弹声音时序试听

八套合成试听位于 `Art/Audio/Reload/Previews/{Type}_{Tactical,Empty}.wav`，可直接打开 [四枪换弹试听页面](../../Audio/Reload/Previews/index.html)。页面使用真实枪械中立几何图、浏览器原生音频控件和各事件秒数，不将静帧称作手臂或UE运行视频。

`Tools/Audio/build_reload_audio_previews.py`只读取本目录动作manifest和`Art/Audio/Reload/SW_Reload_*.wav`，按`phase * seconds`将机械声音混合到同一音轨。输出为48kHz单声道PCM16，试听总时长为武器ReloadSeconds加0.3秒尾音；不修改21个原始机械声音、不修改运行时声音配置。重建命令：

```powershell
python Tools/Audio/build_reload_audio_previews.py
```

每个输入必须存在且格式一致；动画声音事件必须按时间递增。源尾音允许自然重叠，不能为了“避免重叠”挪动关键事件。混合峰值超过0.95时才统一衰减防止削波，其余保留原始响度。每个输出WAV重新读取校验采样数、声音非空和削波，生成`Art/Audio/Reload/Previews/audio_preview_manifest.json`；该文件记录事件秒数/采样点、相邻起音间隔、尾音重叠长度、源文件SHA256、试听峰值/RMS与混合增益，方便重新制作时追溯。

2026-09-21八套试听已实际生成并通过PCM校验，长度分别为1.5/1.7/2.5/2.8秒，均无削波，成功标记`RELOAD_AUDIO_PREVIEWS_SUCCESS tracks=8 clipping=0`。日志为`Saved/Logs/ReloadAudioPreviews.log`。这项验收确认离线波形和事件时间，不代表已经听取UE内空间声音、混音或与手臂同屏播放效果。

## 手臂可达性与镜头安全域逐帧验证

`Tools/Unreal/validate_fp_hand_clearance.py`在全部资源导入保存完成后，通过独立Editor Python只读采样8套实际手臂Sequence及其配对机械Sequence。读取当前子AnimBP的`support_hand_grip`与manifest的`magazine_grip_cm/bolt_grip_cm`，不维护另一份握点值；稳定锚点`ik_hand_gun`直接从手臂片段采样，机械`magazine/bolt`直接从已导入枪械片段采样。相机空间使用原生DemoCharacter CDO的CharacterMesh1P完整相对Transform，验证它直接挂在Camera下且Scale=1。脚本不生成Actor、不启动玩法、不写Content或存档，只写Saved日志和JSON。

```powershell
& 'F:/UE5.4.4/UnrealEngine-5.4.4-release/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'F:/UE5.4Project/FPSDemo/FPSDemo/FPSDemo.uproject' -run=pythonscript '-script=F:/UE5.4Project/FPSDemo/FPSDemo/Tools/Unreal/validate_fp_hand_clearance.py' -unattended -nosound -nullrhi
```

检查方法与阈值：

- 采样所有60Hz烘焙帧，并额外采样归一化动作阶段和声音关键时刻，首尾都包含。每帧记录左右锁骨/肩（upperarm）/肘（lowerarm）/腕（hand）相机空间位置。
- 上臂、前臂实际长度与模板Idle真值比较，任一偏差大于0.25cm失败；这一余量用于压缩和浮点误差，不允许用拉伸骨解决握点错误。
- 用模板约55.022cm总链长检查每帧设计腕目标。目标超过链长0.5cm失败，同时记录实际腕距与IK钳制残差。
- 左手稳定抓匣区（抓匣权重至少0.995，且未进入拉机柄混合）比较实际手腕与真实机械弹匣抓点，误差超过1cm失败。伸手/放手过渡不误当成持续接触。
- 对上臂和前臂中心线解析计算其在相机前方中央圆柱的最近点。核心域深度X=4..22cm、YZ半径5.5cm，进入即失败；较宽深度4..28cm、半径9cm的范围只记警告。该域用于发现靠近镜头且明显遮挡画面中央的骨轴，允许正常肩根留在相机后/下方。
- 安全域是骨链中心线代理，不包含肩帽体积、皮肤变形、指骨接触或枪身三角面，不能当作完整无穿模证明。仍须结合8套真实RHI换弹关键帧检查肩帽/袖口、双手、弹匣与枪机。

`Saved/Logs/FPHandClearance.json`字段：顶层`status/error_count/warning_count`表示全套结果，`limits`保存本次阈值，`camera_basis`记录CDO类/组件/父相机/相对变换，`baseline`记录模板两侧位置和骨长。`clips[]`保存实际资源路径、时长、读取的握点、最大接触误差/目标超距/骨长偏差及失败列表。`clips[].frames[]`保存phase/seconds、锚点相机位置、接触混合权重；`sides.l/r.camera_cm`含锁骨/肩/肘/腕，另有实际上下骨长、设计腕目标、距肩长度、超距量、腕误差与可达余量。`segments.upperarm/forearm`记录安全域最近点、半径与线段参数。`left_magazine_contact_error_cm`仅在稳定抓匣阶段存在。失败列表保留类型、侧别、阶段和厘米误差，便于准确复现；资源缺失或异常也写`status=failed/exception`，防止误读前次成功报告。

2026-09-21最终右腕重标定及镜内锚点调整后：8套共932采样，核心错误0、骨长稳定、左右腕目标超距均0；最大抓匣误差手枪0.251cm、步枪0.064cm、散弹0.018cm、狙击0.010cm。外围9cm域保留35个左上臂近镜提示，结合真实RHI关键帧复核；没有放宽阈值清除警告。日志`Saved/Logs/FPHandClearanceRun.log`，报告状态为passed_with_warnings。原27/22cm弹匣轨迹造成的145条失败已通过缩短实际拔匣距离修正。

用户指出右臂穿托后另行使用实际蒙皮/真实枪托三角表面检查，不能以本节骨轴通过代替它。最终UE动作导出及联合Blender工作台、真实蒙皮1560姿态复核结果见[手臂联合制作说明](../../Player/Animations/WORKBENCH_README.md)。八段带同步机械声的UE实际画面见[换弹视频预览](VideoPreviews/index.html)，视频音轨按游戏相同事件时间表合成，非声卡实录。

后续若改变动作接触阶段、Mesh1P挂接、角色蓝图覆盖、骨架或运行AnimGraph中的额外IK，需要同步检查入口和握点契约。此离线脚本读取原生角色CDO及保存的Sequence，不执行主AnimBP额外修正；运行主图的Pole/IK应与离线作者一致，并单独进行真实画面回归。
