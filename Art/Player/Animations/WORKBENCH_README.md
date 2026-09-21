# 第一人称四枪联合姿态工作台

**版本说明（2026-09-22）：** 本文下方的`FinalUE`与1560姿态报告是9月21日的枪托专项历史验证，只覆盖枪托表面，没有覆盖左手与弹匣/机匣/护木。当前左手转腕、指骨张合与全枪接触使用`Breach_FP_LeftHand_Contacts.blend`、`LeftContactUE/`、`LeftHandPreviews/index.html`及`left_hand_final_ue_diagnostic.json`。制作/配置和失败处理见同目录主`README.md`的“左手接触制作”，实际验证结论见`Documentation/武器系统.md`对应日期记录。不要将旧枪托通过当成左手已通过。

本目录的 `Breach_FP_Reload_Workbench.blend` 用于把真实 UE 手臂蒙皮与四把 Breach 骨骼枪放在同一厘米场景内检查、编辑握持和换弹动作。它不替换 UE Template，不改变原手臂骨架、权重或枪械源文件。UE 中最终动作由工程的 C++ 作者流程生成；工作台的候选动作是几何对照资源，不能直接当作最终运行资产验证结果。

## 内容与使用

- `FP_Pistol`、`FP_Rifle`、`FP_Shotgun`、`FP_Sniper` 四个集合，每次只显示一个。
- 四个 `Arms_*` 共享原导出 `UE_Original_Arms_Skeleton` 的 160 根骨骼，网格保留原 21570 个顶点及其权重。不同对象可以分别播放动作。
- `CurrentUE_{枪型}_{Idle|Tactical|Empty}` 是重设计前导出的 12 套实际 UE 动作；`Candidate_*` 是根据 `fp_pose_profiles.json` 生成的 12 套右臂调整动作。二者都有 Fake User，切换不会丢失。
- `FinalUE_*` 是重烘焙后再次由 UE 导出的 12 套实际动作，未在 Blender 重算 IK。工作台打开默认显示 `FinalUE_Rifle_Idle`，原快照和候选仍保留供比较。
- `Gun_*` 保留 `A_Breach_{枪型}_Reload_Tactical/Empty` 机械动作；待机清除机械 Action 并重置 Pose。比较动作时，给手臂和枪选择相同类型、在相同归一化时刻观看。
- `WeaponAnchor_*` 跟随原 `ik_hand_gun`，保持挂枪与操作右手分离。没有把枪改挂在右手腕，狙击拉栓时枪不会被手带走。
- `WorkbenchPreviews/index.html` 汇总 24 张 `FinalGrip/FinalMagazine/FinalMechanism` 最终 UE 动作的侧视和第一人称几何预览；带 `Current` 的三组图提供旧姿态对照。图片由真实蒙皮和几何渲染，并非示意拼图。

工作台使用导出 FBX 的真实网格，但 FBX 没有携带完整 UE 材质，因此赋予了诊断颜色。第一人称预览参考 Mesh1P 相对相机位置，并非 UE 最终光照/材质截图；判断相交应同时使用侧视、实体测试和 UE 实际录制，不能只看二维遮挡。

## 坐标、姿态与修改理由

UE 数据以厘米、+X 前/+Y 右/+Z 上记录。工作台用六个实际关节对原 FBX 做刚性坐标拟合，保留必要的 Y 反射；四枪最大骨位置校准误差均小于 0.0001 cm，实际 `ik_hand_gun` 变换用于挂枪。矩阵、源 FBX、集合名和动作清单写在 `workbench_manifest.json`。

候选腕目标为枪局部 `(-10.5, 1.5, -2) cm`，右肘 Pole 为肩部加 `(12, 50, -45) cm`，保留模板手腕姿势。旧腕 `(-3.522, 1.674, 1.466) cm` 使掌心偏向机匣前方；新腕把手掌移回握把，同时使右肘下移并向外展开。狙击空仓伸手/回握期间加入最大 6 cm 的向右过渡弧，最终拉栓仍贴合原机械抓握点。两段手臂长度不伸缩，目标不可达会明确终止构建。

这些配置由 `fp_pose_profiles.json` 统一提供，JSON 字段注释与运行使用说明在本目录主 README，由运行实现维护。不要单独改工作台脚本常量造成 UE 与 Blender 两套方案分叉。

## 实体诊断

`stock_collision_proxy.json` 逐枪记录真实枪托零件的 UE 武器局部 OBB 与三角网格。长枪各 6 件，手枪没有枪托。每件保留源对象名、`body` 骨、中心、正交轴、旋转四元数、半尺寸、顶点与三角面；OBB 来自真实顶点的 XZ 主轴，Y 保持挤出方向，不能用整枪总包围盒替代空心枪托。

`forearm_skin_radius.json` 从原网格主要蒙皮权重提取实际径向样本。根级 `sample_detail` 是右前臂/腕，`upperarm.sample_detail` 是右上臂及原 twist/corrective 区。每项包含源顶点号、主骨名、权重、沿关节轴的 `alpha` 和 `radius_cm`。上臂轴为 `upperarm_r` 肩→`lowerarm_r` 肘，前臂轴为肘→`hand_r` 腕。用于厚度胶囊的分段半径应由完整样本取最大值，不用小半径掩盖穿插。

`workbench_skin_diagnostic.json` 是实际变形后全部双臂网格顶点与实际闭合枪托三角表面的检查：包围盒只筛选候选点，BVH 最近表面距离和射线奇偶规则共同确认穿入。0.02 cm 是数值接触噪声，不是视觉豁免。报告保留枪型、动作、阶段、零件、穿入顶点数、主骨名及最深点的武器局部坐标。该检查覆盖全部 60 Hz 帧及额外机械关键时刻，但不宣称连续时间的三角面 CCD；还需结合 UE 最终动作与运行镜头复核。

已定位重设计前狙击空仓右上臂 `upperarm_twist_02_r` 区进入托底衬垫：完整检查旧/候选共 1594 姿态，旧动作 16 姿态失败，区间约 0.649–0.695 及 0.808–0.848，最大侵入 0.586 cm。此前只检测前臂/腕的胶囊没有覆盖这部分，所以“前臂零碰撞”不能代表整个右臂通过。右手回握把、肘下外展的候选 797 姿态无实际蒙皮点侵入。

最终不是仅验收 Blender 候选：`workbench_final_ue_skin_diagnostic.json` 检查重新从 UE 导出的 `FinalUE` 动作，12 段 FBX 的参考骨矩阵、顶点坐标、全部蒙皮权重与原始共享网格逐项误差均为 0；三长枪共 1560 姿态（导出帧和 UE 采样并集）没有顶点侵入实际枪托表面。手枪无枪托，不纳入枪托检测。报告覆盖全部左右臂及手部、辅助蒙皮，不仅骨轴；它仍不是连续三角面 CCD 或运行时混合图的证明。

UE 上臂厚度胶囊 × OBB 保守代理仍保留狙击 22 帧阳性、最大包络交叠约 1.554 cm：胶囊将实际非圆截面填满、OBB 将零件边缘外部填满，两者比真实网格更保守。没有缩小半径或提高阈值把它改成通过；最终真实蒙皮 × 真实枪托的阴性结果用于区分这些代理阳性，与 UE 运行录制结合验收。

## 重建与维护

在项目根目录运行：

```powershell
& F:/Blender/blender.exe -b --python Tools/Blender/build_fp_reload_workbench.py
& F:/Blender/blender.exe -b --python Tools/Blender/validate_fp_reload_skin.py
& F:/Blender/blender.exe -b --python Tools/Blender/import_final_fp_workbench.py
```

第一条先提取真实枪托/蒙皮尺寸，再读取 `CurrentUE/current_ue_workbench_manifest.json` 及其 12 个 FBX，创建工作台与预览。仅更新几何代理可在末尾添加 `-- --extract-only`。第二条只读打开工作台，输出实际蒙皮报告和旧姿态对照图，不覆盖 Blend 或 UE 内容。第三条要求先由 UE 只读导出完整 `FinalUE` 目录，再加载真实最新动作、检查原始网格一致性、产生最终蒙皮报告及 24 张最终图，并保存工作台默认姿态。只更新打开视图和索引可加 `-- --view-only`。

缺失 FBX/动作、骨架名称不一致、坐标校准超 0.1 cm、不可达手腕或实际枪托列表为空均明确失败。不要以替代静帧、拉长骨骼、缩小碰撞半径或移动相机来掩盖问题。模型或权重修改后应重建代理、重新导出实际 UE 动作，再复核实体和录制画面。

维护文件：`Tools/Blender/build_fp_reload_workbench.py`、`Tools/Blender/validate_fp_reload_skin.py`、`Tools/Blender/import_final_fp_workbench.py`、本说明、工作台 Blend/manifest/诊断 JSON 和 `WorkbenchPreviews/`；原 Template、`CurrentUE`/`FinalUE` 快照、旧静态枪源均为只读输入。
