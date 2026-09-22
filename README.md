# FPSDemo · 十关 GAS 第一人称闯关 Demo

基于UE5.4 C++第一人称模板，当前支持单人Standalone/单人PIE。十个逻辑关卡复用三个白模战斗区。

## 完整获取项目源码与资产（首次下载必读）

<!-- 首次克隆必须下载LFS实际对象；将获取与环境说明放在首页顶部，避免把资源指针当作完整工程。 -->
**本项目使用 Git LFS 存储 UE 资产、模型、贴图、音频和视频。请安装 Git 与 Git LFS，并使用以下命令下载；仅获取源码文件或 LFS 指针不能正常打开工程。**

在准备存放项目的目录打开 PowerShell，执行：

```powershell
git lfs install
git clone --branch main https://github.com/hachisamurai/FPSDemo.git
cd FPSDemo
git lfs pull origin main
git lfs fsck
git status --short --branch
```

等待所有命令成功结束，`git lfs fsck` 应显示 `Git LFS fsck OK`。`git lfs pull` 会补齐当前版本的实际大文件并写入工作目录；下载失败时先解决网络、仓库权限或 LFS 配额问题，再重新执行该命令。不要在资源未下载完成时启动 Unreal Editor。推荐使用上述克隆方式，不以 GitHub 的 **Download ZIP** 代替完整的 Git/LFS 获取流程。

如果已经克隆过项目，先保存并提交自己的修改，再更新：

```powershell
git pull --ff-only origin main
git lfs pull origin main
git lfs fsck
```

若拉取提示本地修改冲突或无法快进，请先处理本地工作与分支差异，不要通过强制重置丢弃自己的修改。以上步骤获取的是远端 `main` 已提交的版本，不包含其他开发者尚未提交或推送的本地改动。

### 首次编译与打开

1. 安装 **UE 5.4.4、Visual Studio 2022 的“使用 C++ 的游戏开发”工作负载及 Windows SDK**。引擎、编译器不包含在仓库内；当前 Shipping 配置使用源码引擎独立构建。
2. 项目保存的是开发机的源码引擎关联 GUID。在新电脑上右键 `FPSDemo.uproject` → **Switch Unreal Engine version**，选择本机对应的 UE 5.4 引擎，然后执行 **Generate Visual Studio project files**。
3. 打开生成的 `FPSDemo.sln`，选择 **Development Editor / Win64** 编译项目（目标为 `FPSDemoEditor`）；成功后打开 `FPSDemo.uproject`。
4. 打开 `Content/Whitebox/Maps/L_ThreeSector_Whitebox` 并以单人 PIE 运行。已提交的 UE 资产下载完整后可直接使用，无需重新执行全部资源导入脚本。

### 仓库包含与不包含的内容

- **已包含**：`Source`、`Config`、`Content`、`SourceAssets`、`Art`、`Backend` 源码、制作工具和说明文档；运行工程不需要开发机上的 `F:\Lyra\Lyra` 目录。
- **由本机生成**：`Binaries`、`Intermediate`、`DerivedDataCache`、`Saved`、IDE 工程文件等构建和运行产物。克隆后没有这些文件是正常的；仓库提供工程，不提供已编译的可执行包。
- **另行配置**：后端真实 `.env.local`、数据库密码、玩家云身份与存档不会上传。编辑器试玩只使用本地临时数据，不需要连接云服务；部署后端请参考 `Backend/.env.local.example` 和维护文档。
- **资源制作工具**：Blender、Python 库和 FFmpeg 需在重新制作相关资源时自行安装；使用已提交的 UE 资产运行游戏不需要这些离线制作工具。

## 运行

1. 使用UE5.4.4编译 `FPSDemoEditor / Win64 / Development`，新增原生类后保存资源并重启已打开的Editor。
2. 打开 `FPSDemo.uproject` 与 `Content/Whitebox/Maps/L_ThreeSector_Whitebox`，设置1名玩家后Play。
3. 大厅点击开始游戏，选择已有存档或确认创建空栏位。新档进入安全区，靠近下一关终端按E选择简单/普通/困难后出发。
4. 武器蓝图与输入资产已由Editor创建。如需要在新工作副本重建，编译后运行 `Tools/Unreal/create_weapon_assets.py`，详见武器文档。

| 操作 | 输入 |
|---|---|
| 移动 / 视角 / 跳跃 | WASD / 鼠标 / Space |
| 射击 | 左键；步枪按住连射，手枪/散弹/狙击每次按下一发 |
| 装填 | R；空弹后再次按左键也调用同一GAS装填接口 |
| 两个武器槽 | 1 主武器 / 2 默认手枪 |
| 更换主武器型号 | 安全区/关间升级终端→武器页→选择已解锁主武器；B不再切枪 |
| 狙击镜 | 右键循环一级/二级/退出，默认开枪退镜 |
| GAS冲刺 / 治疗 | Left Shift / Q |
| 终端交互 | E（250cm内） |
| 奖励、商店 | 鼠标点击或1/2/3；菜单期间不会切换武器 |
| 关闭商店 | Tab / E / 关闭按钮 |
| 暂停 / 恢复 | Esc；子页先返回上层，主暂停页再次Esc恢复 |
| 停止PIE（仅编辑器） | Shift+Esc / 编辑器停止按钮；项目配置需重启Editor后读取 |
| 结算后重开 | Enter / 重开按钮 |

## 游戏循环

大厅选择存档→初始安全区→入口终端选难度→第1关→前九关清关免费三选一→原地备战/银币升级/下一关终端→第10关胜利→保留金币、永久成长和解锁、清空银币及临时成长返回安全区→装备解锁武器/选择更高难度继续挑战。第5/10关有Boss，怪物属性受等级及难度倍率影响；击杀银币按等级配置，整轮通关金币按难度单独配置，不直接乘属性倍率。

- 玩家死亡结算后回安全区重开第一关，银币、临时成长和弹药重置，保留金币、永久成长、武器解锁和难度；胜利同样清空银币/临时成长并返回安全区，系统异常可回大厅。
- 三个独立存档栏显示首次创建的本地时间，安全区/关间检查点自动保存；未完成战斗读档后重新挑战。
- Esc提供返回安全区、设置、返回大厅、退出；未通关主动放弃且已有成长时需要确认清空当局数据，通关结算返回保留金币/永久成长/武器解锁并清空银币/临时成长。设置支持分辨率、画质、鼠标倍率和全屏/窗口。
- 编辑器试玩仅使用本次运行的本地临时数据，返回大厅可继续读档；停止PIE/独立试玩后清除存档、成长与解锁，再次Play从空档开始。正式非Editor游戏才跨进程保存，设备设置与诊断日志保留。
- 击杀获得银币；仅完整十关通关获得金币，难度表默认简单500/普通750/困难1000。安全区金币升级永久保留，关间银币升级本轮有效；两币种各三项独立20起价，只给所购项+10。死亡/通关保留金币价格，重置银币价格。
- 伤害与容量升级对所有武器生效；各枪独立保存弹药与开火间隔，切枪不补弹。
- 空弹再次射击只发起换弹。打完最后一发不会立即换弹，装填完成也不会自动补射。
- 小怪/Boss支持飞行物；Boss保留原范围攻击，并有2秒红光前摇的全图伤害/减速，冲刺窗口躲避成功恢复5HP。
- 现有武器外观/动画复用模板资源，可在各武器BP配置替换；当前没有四套独立成品枪械美术。

<!-- 维护入口默认折叠，简化仓库首页；文档及链接仍保留，点击标题可展开。 -->
<details>
<summary>维护文档</summary>

- [Git与Git LFS协作、克隆及忽略规则](Documentation/Git版本管理.md)
- [强制日志、注释与文档规则](AGENTS.md)
- [目录分类](Documentation/目录分类.md)
- [武器基类、散弹/狙击、Enhanced Input、空弹装填](Documentation/武器系统.md)
- [GAS架构与生命周期](Documentation/GAS架构.md)
- [战役配置与难度](Documentation/战役配置与难度.md)
- [关卡与经济](Documentation/关卡与经济.md)
- [暂停、设置与检查点存档](Documentation/暂停设置与检查点存档.md)
- [敌人攻击与冲刺躲避](Documentation/敌人攻击与冲刺躲避.md)
- [武器音效](Documentation/Audio/武器音效.md)
- [UI设计](Documentation/UI预览设计.md)
- [调试与验证](Documentation/调试与验证.md)

</details>

HUD使用原生Canvas，当前为单人键鼠原型；已有复制基础不等于完整联机支持。
