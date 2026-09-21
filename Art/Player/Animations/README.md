# 第一人称手臂制作与枪械联合标定

`Reference/`保存从UE原模板导出的161骨手臂网格、原待机动作与骨变换。`CurrentUE/`保存只读导出的当前四枪待机/换弹成品FBX及组件空间变换，用于Blender内将真实手臂、蒙皮和实际枪械放在同一空间重新设计。导出不会修改UE模板包。`Breach_FP_Reload_Workbench.blend`为联合工作台，构建过程及版本见其清单。

此前直接沿用模板GripPoint与手腕的相对位置，仅保证骨长/腕点可达，未检查Breach实际握把、枪托与有厚度的前臂，因此不能把旧骨轴安全域通过当成无穿模。重新标定以实际握把包握、前臂避开真实枪托体积为约束，保留原骨名、骨长、蒙皮与共享Skeleton。

`fp_pose_profiles.json`是不支持注释的离线制作配置，由`Tools/Unreal/import_fp_weapon_animations.py`读取并交给原生SequenceAuthoring，运行游戏只读取烘焙后的动画，不读JSON。字段含义：

- `version/units`：当前为1、厘米；修改单位必须同步导入器与Blender场景，禁止通过缩放骨骼补偿。
- `right_wrist_weapon_cm`：共同握把坐标系内右手腕骨位置，四枪共享同族握把；原模板腕点相对Breach偏前，后移约7cm并下移，使掌指包在握把而非机匣前部。
- `right_elbow_from_shoulder_cm`：手臂组件空间里相对右肩骨的TwoBoneIK肘极点方向与距离；只决定弯肘平面，不平移肩根或伸长骨，向右下外展避让枪托。
- `sniper_bolt_reach_clearance_cm`：右手接近/离开枪栓时向枪右侧额外绕行的厘米幅度，使用sin(pi*抓栓权重)平滑弧线；在握把和抓栓端点为0，防止直线过渡扫过机匣。
- `support_wrist_weapon_cm`：历史工具兼容镜像，保持与`left_hand_contacts.json`的Support一致；正式新导入器从接触配置读取，避免待机IK拉回旧握点。

修改此配置后必须重新编译变化过的Editor工具、统一导入/烘焙，再运行手臂可达性及枪托相交检查、真实RHI回归。手腕目标到位不等于手指/护甲已经无交叉；验证应包含有厚度的前臂与实际枪托零件，并复查换匣、狙击右手离握把/回握把、中断复位。运行框架、配置子动画蓝图和GAS唯一补弹见`Documentation/武器系统.md`。

## 左手接触制作（2026-09-22）

`left_hand_contacts.json`按四枪保存Support/Magazine/Mechanism三种接触。`wrist_cm`为枪参考空间中的腕骨位置（不是掌心）；`delta_wxyz`为相对于原模板手腕的枪空间旋转增量，Blender顺序wxyz；`fingers`按index/middle/ring/pinky/thumb分别记录01/02/03关节的额外屈曲角度（度），正方向由原模板各指的屈曲平面确定，负数张开。手枪从弹匣底盖接近，其余枪从弹匣侧面握持；Sniper的左手Mechanism等于Support，枪栓仍由右手操作。

`author_fp_left_hand_contacts.py`在原始UE蒙皮上制作并校验转腕、张手、合指和外侧绕行，输出`left_hand_contact_tracks.json`与`Breach_FP_LeftHand_Contacts.blend`。轨迹含每帧枪局部`wrist_cm`、`rotation_xyzw`、15根指骨的局部`finger_delta_xyzw`，后两者采用UE xyzw顺序。文件是离线作者产物，不进入游戏运行逻辑；导入器核对接触配置及机械清单SHA256，过期必须重新制作。

普通换弹简化为外侧接近→转腕合指→抽匣→插匣→张手回握。空仓在插匣后加套筒释放/拉机柄/右手拉栓。手指不参与弹药结算；GAS仍在原结束时点唯一补弹。声音事件时间未改，减少的只是没有必要的下探距离。不能靠缩小手、拉长骨骼、移动相机或隐藏接触部分绕过穿插。

正确重建顺序：修改机械源（需要时）→Blender构建机械→Blender `author_fp_left_hand_contacts.py`→编译Editor工具（C++变化时）→UE `import_fp_weapon_animations.py`→UE `export_fp_left_contact_workbench.py`→Blender `validate_fp_left_contact_ue.py`→UE真实运行录帧/中断/声音检查。`--static`只验证12个接触姿势，不生成新轨迹，不能替代完整重建。

`LeftContactUE/`保存新实际UE导出，原`FinalUE/`保留右臂修正快照。`left_hand_timeline_diagnostic.json`是Blender作者候选；`left_hand_final_ue_diagnostic.json`才是UE成品全枪检查。两者都检查左臂/掌/指实际变形顶点与每个枪件闭合三角表面；数值接触容差0.02cm，失败记录不删除。成品额外复核双臂对枪托，仍不宣称连续三角面CCD或所有运行混合姿态均无相交。

依赖原Template网格/骨架与Reference原始姿势、FinalUE历史姿势、独立机械Blend和工作台坐标映射。缺失骨、原始蒙皮/权重不一致、腕点不可达、轨迹帧数不匹配都会明确失败；不要在同一Content上并行重导入和导出。当前功能目的、验证记录及运行接口统一维护于`Documentation/武器系统.md`，可编辑接触工作台和分阶段预览在本目录。
