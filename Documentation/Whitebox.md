# Blender 三个战斗区白模场景

玩法现为十个逻辑关卡，复用此文的三个战斗区及一个安全区；不用增加十张物理地图。`DT_Levels.ArenaIndex` 决定每关使用哪个区域，第 5/10 关默认在区域 2 迎战 Boss。历史地图/Blender 文件名保留 ThreeSector，避免破坏已有资产引用。配置与新回归见 [战役配置与难度](战役配置与难度.md)。

## 目的与设计

为 UE5.4 单人 GAS Demo 提供可编辑、可导入、可直接游玩的关卡白模，先验证尺寸、视线、掩体和流程，不制作精细美术。建筑使用白/灰材质；游戏 UI 与交互提示保留反馈颜色。

- Blender 源文件：`Art/Whitebox/FPSDemo_ThreeSector_Whitebox.blend`。
- UE 地图：`/Game/Whitebox/Maps/L_ThreeSector_Whitebox`，已设为编辑器和游戏默认启动地图。
- 四区处于**同一张持久关卡**，通过既有 GameMode 传送推进，不是四张独立加载地图。每区约 34×34m，中心间隔 60m，UE 地面 Z=10000cm。

| 区域 | 白模布局 | 玩法用途 |
|---|---|---|
| 00 SafeHub | 有顶服务棚、工作台、柜子、升级台标记、入口门框 | 开局及死亡重开后备战；正常清关不回此区域 |
| 01 CargoYard | 两侧货箱、低掩体、开放中央通道 | 第一关 5 小怪，建立射击与绕行节奏 |
| 02 RelayHall | 设备柜、交错掩体、头顶横梁 | 第二关 8 小怪，提高战斗密度 |
| 03 BossArena | 双环地面标记、角落立柱、背墙地标 | 6 小怪 + Boss，预留范围攻击躲避空间 |

掩体高约 0.9–1.15m，入口净高至少 4m；近侧围墙 2.4m，其他约 5m。敌人现在沿NavMesh路线保持平面悬浮高度绕障，必经路线仍无楼梯/跳台要求；工作棚和货箱顶面不属于必经区域。三个战斗区通过`configure_enemy_navigation.py`生成并保存导航体积，安全区无需敌人网格。白模重导入/移动障碍后按[敌人导航与绕障](敌人导航与绕障.md)重新构建验证。

## 核心流程与接口

1. `Tools/Blender/build_whitebox.py` 在独立后台 Blender 中从空场景生成四区，保留 Collection、可编辑网格、文字和修改器。
2. 导出时复制几何、转换文字并应用修改器，每区合为一个 StaticMesh；删除导出副本，原始场景保持可编辑。
3. 每区 FBX 以地面中心为局部原点。Blender 使用米，UE 使用厘米，导入缩放 1。导出副本镜像 Y 并反转面绕序，抵消 UE FBX 转换，保持玩法坐标方向。
4. `export_axis_probe.py` 生成非对称探针，UE 检查包围盒 min=(200,500,0)、max=(400,900,600)cm，误差小于 0.1cm，以验证单位和轴方向。
5. `Tools/Unreal/import_whitebox.py` 导入四个网格和材质，创建或加载专用地图，摆放区域、参考点、PlayerStart、太阳光、天空光、大气。
6. `GameMode::BuildAreas` 找到四个同时含 `DemoAuthoredBlockout` 与 `DemoArea-1/0/1/2` 的区域后，跳过旧程序地板/墙体。终端、怪物继续由原 C++ 生成。
7. 大厅 → 初始安全区 → 战斗 → 前九关原地奖励 → 本关中心商店/下一关终端 → 第十关胜利；玩家死亡重开才返回安全区。中心两终端默认 XY 偏移 `(0,±300)` cm，向下探测实际地面再加 81cm 根碰撞高度，避免穿入安全区台面；玩家占位时只微调终端。切换战斗前销毁，见 [关卡与经济](关卡与经济.md)。

## 使用与配置

打开 UE 项目，在 `Content/Whitebox/Maps/L_ThreeSector_Whitebox` 单人 PIE/Standalone 点击 Play。WASD 移动、鼠标视角、左键射击（空弹再次按下自动装填）、R 装填、1 主武器、2 手枪、安全/关间备战 B 更换主武器、狙击右键循环两档镜/退镜、Shift 冲刺、Q 治疗、Space 跳跃、E 交互；菜单点击或 1/2/3 选择，Tab/E 关闭商店，Enter 终局重开。

Blender Outliner 的 00–03 Collection 对应四区，90 为展示相机；Empty 仅为出生/交互参考。`Art/Whitebox/FBX/` 是四区模型和探针，`Previews/` 是透视图和总览。

重新生成会覆盖生成的 `.blend`，手改源文件应先另存。UE 导入会更新同名生成网格，并以相同 Label+类型更新生成 Actor，保留其他用户 Actor。改名/删除的旧生成 Actor 需手工清理，脚本不会删除用户对象。白模地图使用原生 GameMode，敌人数量为 C++ 默认 5/8/6；调整默认值需同步测试。

在项目目录用 PowerShell 重建：

```powershell
& 'F:/Blender/blender.exe' --background --python-exit-code 1 --python Tools/Blender/build_whitebox.py
& 'F:/Blender/blender.exe' --background --python-exit-code 1 --python Tools/Blender/export_axis_probe.py
& 'F:/UE5.4.4/UnrealEngine-5.4.4-release/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' 'F:/UE5.4Project/FPSDemo/FPSDemo/FPSDemo.uproject' -run=pythonscript '-script=F:/UE5.4Project/FPSDemo/FPSDemo/Tools/Unreal/import_whitebox.py' -unattended -nullrhi -nosound
```

## 依赖、碰撞与清单字段

- 实际工具：Blender 5.2.0 LTS、UE5.4.4；Blender 版本不改变 UE 项目版本。
- 制作依赖 bpy/FBX 导出器、UE Editor Python 插件和原生 FPSDemo 模块；游戏运行不需要 Blender/Python。
- 使用 **Use Complex Collision As Simple** 静态三角面碰撞，不开启物理模拟。不可用单个凸包包围整个区域，否则封闭房间内部会被当作实体。
- `whitebox_manifest.json` 无法放注释，字段在此说明：`version` 是版本；`blender_unit/ue_unit` 是单位；`ue_height_offset_cm` 是高度偏移；`areas[].name/asset/index/origin_ue_cm/label` 是集合名、资产名、区域编号、UE 原点、展示名；`markers[].name/kind/position_m/radius_m` 是参考点名、用途、Blender 世界位置、参考半径。
- 参考点导入为 editor-only TargetPoint，运行时仍由 GameMode 公式生成敌人。修改出生位置必须同步 C++，不能仅移动参考点。
- 工具函数打印 `[Whitebox]` 调用日志；C++ `BuildAreas` 记录 authored/fallback 分支。

## 边界、失败与维护

缺少 FBX、资产类型错误、探针失败、地图创建/加载/保存失败均抛出异常；同时检查退出码与 `WHITEBOX_IMPORT_SUCCESS`。UE5.4 的 `new_level` 不覆盖已有地图，因此重跑显式 `load_level`。

只存在部分区号标签时 GameMode 拒绝启动，进入可重试失败；完全没有标签的旧模板地图使用程序几何回退。整区合并网格适合小型白模，正式美术阶段应拆分模块、优化碰撞。

新增 `Art/Whitebox/`、`Content/Whitebox/`、`Tools/Blender/`、`Tools/Unreal/import_whitebox.py`；修改 `FPSDemoGameMode.cpp/.h` 场景选择分支、`Config/DefaultEngine.ini` 默认地图，并更新关卡文档。

区域坐标或尺寸变更需同步 Blender 生成器、清单、GetAreaCenter/出生点与回归。标签、局部原点、厘米比例是联动接口。原模板地图保留。

## 验证记录（2026-09-20）

- `.blend`、四区 FBX、探针和五张预览生成成功，已检查 Blender 渲染。
- UE5.4.4 Editor Development 编译通过；初次/重复导入、地图保存、1:100 单位和轴向校准通过。
- `Saved/Logs/WhiteboxSmoke.log` 无渲染回归及 `WhiteboxVisualSmoke.log` 有渲染回归均输出 `DEMO_SMOKE_SUCCESS`，且确认使用 Blender authored geometry。
- 回归覆盖安全区禁战、射线/弹药、冷却、装填完成/取消、两次奖励、金币购买与距离限制、三关敌人数、Boss 胜利、死亡与重开。实际游戏截图在 `Saved/Screenshots/Demo/`；已查看安全区、普通战斗、Boss 场景。
- 自动测试通过 GE 清场验证状态机，不代替完整人工试玩每条绕行路线和掩体卡点；Shipping 打包、多人、独立流送未验证。调试入口见 [调试与验证](调试与验证.md)。

API 参考：[Blender FBX 导出](https://docs.blender.org/api/main/bpy.ops.export_scene.html)、[UE5.4 FBX 导入配置](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/FbxStaticMeshImportData?application_version=5.4)。
