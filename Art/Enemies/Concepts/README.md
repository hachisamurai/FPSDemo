# Breach 敌人概念板 v1

2026-09-21，按用户确认的“工业悬浮机械敌人”方向制作。图片 `Breach_Enemies_Chaser_Boss_v1.png` 使用内置 image_gen 工具生成；输入参考为 `Art/Weapons/Models/Previews/Breach_Weapons_Models.png`，仅参考已有武器的材质和工业造型。

左侧：追击型撞击者，主体约 1m，楔形灰白机身、短机械臂、橙红传感器和双推进器。右侧：镇压核心 Boss，主体约 2.4m，厚重肩甲、关节机械臂、四片核心护板与背部能量环。右上小图表现护板开启和全图技能蓄能。

这是一张设计概念图，尚未制作或导入敌人 SkeletalMesh、Skeleton、Animation Sequence、AnimBP 或 Montage；不是 UE 实拍。下方视图仅供建模参考，不应当作像素精确、严格一致的正交工程图。最终尺寸、关节间隙和可命中轮廓需在 Blender/UE 中校准。第 10 关使用同骨架装甲变体，不复制第二套 Boss 动画。

设计与动画/联机职责见 `Documentation/敌人模型骨骼与动画同步设计.md`。既有 GAS/导航/攻击代码未因这张图发生改变；本次交付为概念与架构设计。

## 实际生成提示词

```text
Use case: stylized-concept.
Asset type: one cohesive enemy concept development board for the UE5.4 FPS demo BREACH. This is a concept image, NOT a finished model or game screenshot.
Primary request: design the first two approved enemy archetypes, a compact CHASER drone and a large WARDEN CORE boss, from the same industrial manufacturer as the supplied weapon board. The input image is a style/material reference ONLY; do not reproduce firearms or its four-panel layout.
Style: believable buildable medium-low-poly hard-surface 3D game asset concept, broad beveled armor panels, clean exposed hinge joints and pistons, restrained details that can be modeled in Blender with rigid bone weights. Matte off-white/light-gray armor, dark blue-gray metal skeleton, charcoal rubber/seals, sparse amber-orange/red enemy sensor lights. Keep much of the armor visibly gray-white, no cyan enemy lights, no organic flesh, no military human, no legs, no wings resembling a living creature, no giant dense greebles or glossy illustration-only details.
Composition: polished wide landscape studio concept board, left 40% CHASER and right 60% BOSS; ample margins, every appendage fully visible. Upper area shows each floating robot at a matching front three-quarter angle with a subtle ground shadow. Lower strip shows small front/side neutral-pose construction silhouettes for each design, consistent with its main hero design. Avoid clutter and explanatory paragraphs. Simple exact headings: 'BREACH / ENEMY DESIGN', '01 CHASER', '02 WARDEN CORE', small captions '1.0 m BODY' and '2.4 m BODY'. Do not imply panel render sizes are equal real-world scale.
CHASER: approximately 1 metre compact wide wedge-shaped hovering armored body, slightly pointed armored battering front, one small deeply inset horizontal amber-red sensor, two short folding mechanical forearms with blunt heavy impact plates, a central energy core partially visible beneath split chest armor, two compact rear/downward hover thrusters. Sturdy aggressive silhouette, not a sphere with pasted-on details. Neutral arms clearly leave room to fold back for windup and swing forward for a contact attack.
WARDEN CORE: approximately 2.4 metre main armored body, imposing floating reactor machine with broad heavy shoulder armor, articulated two-link left/right mechanical arms ending in blunt three-segment clamp/impact assemblies, a large recessed orange core with four thick hinged shutter panels, a segmented annular energy assembly mounted on the BACK and visible above/around the shoulders, visible mounting supports (no mysteriously floating armor). Two lower thrust pods, no legs. Boss ring and core shutters can open during a telegraphed pulse; show one small inset of the same boss with shutters open and orange ring illuminated, keep neutral main pose. Maintain obvious family resemblance to the chaser while giving the boss a distinct broad crown/back-ring silhouette.
Lighting: clear soft studio illumination and gentle rim light against desaturated charcoal-blue backdrop, readable metal surfaces, limited restrained emissive bloom. No explosions, no battlefield, no weapons held in human hands, no watermark, no Unreal logos, no conceptual bone X-ray overlays.
```
