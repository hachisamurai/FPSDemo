# 敌人战斗动画源

Chaser为普通怪共用17骨，Warden为Boss的32骨；各自的Combat.blend在Action Editor内保留所有正式动作，FBX是24fps厘米制导出，root不移动。原Models目录仍保留模型与RigCheck绑定检查源，不用QA动作驱动战斗。

UE资源位于`/Game/Enemies/Breach/Animation/`：Sequences、ABP_Chaser/ABP_Warden、15个AM_*以及DA_Chaser/DA_Warden。Idle/Move由图混合，其余由C++表现组件通过GAS播放。combat_manifest.json记录model、kind、name、seconds、loop，供导入和验证；运行时只加载UE资产。

重建会更新专属生成资产；精修前复制资源并修改DA引用。完整流程、接口、边界和验证见项目`Documentation/敌人模型骨骼与动画同步设计.md`中的“战斗动画与运行时接入”。

`Previews/BreachCombatInUE.png`为真实UE白模竞技场渲染，非概念图。
