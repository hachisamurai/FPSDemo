// 职责：核心检查点严格值校验。使用：恢复和选择事务前调用；不访问World、经济或随机目录。
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Progression/Builds/DemoRunBuildSettings.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h"
#include "Debug/DemoLog.h"

namespace
{
    const TCHAR* const CoreIds[] = {TEXT("B01"), TEXT("B02"), TEXT("B03"), TEXT("B04"), TEXT("B05"), TEXT("B06"), TEXT("B07"), TEXT("B08"), TEXT("B09")}; // 可信顺序只追加；UI索引和服务器选择共用，禁止用任意资源路径扩目录。
}
FName DemoRunBuild::CoreAt(int32 Index)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] DemoRunBuild::CoreAt"));
    return Index >= 0 && Index < UE_ARRAY_COUNT(CoreIds) ? FName(CoreIds[Index]) : NAME_None;
}
int32 DemoRunBuild::CoreCount()
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] DemoRunBuild::CoreCount"));
    return UE_ARRAY_COUNT(CoreIds);
}
bool DemoRunBuild::IsCore(FName Id)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] DemoRunBuild::IsCore"));
    for (const TCHAR* Candidate : CoreIds) if (Id == FName(Candidate)) return true;
    return false;
}
bool FDemoRunBuildSnapshot::Validate() const
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] FDemoRunBuildSnapshot::Validate"));
    if(Revision<0 || (PendingBranchStage!=0&&PendingBranchStage!=3&&PendingBranchStage!=7)) return false;
    if(CoreId.IsNone()) return Branch3.IsNone()&&Branch7.IsNone()&&PendingBranchStage==0&&!bLocked;
    if(!DemoRunBuild::IsCore(CoreId)) return false;
    const FString Prefix=CoreId.ToString()+TEXT("-"); // 只由上方可信核心ID构成，不解析用户资产路径。
    if(!Branch3.IsNone()&&Branch3!=FName(Prefix+TEXT("A"))&&Branch3!=FName(Prefix+TEXT("B"))) return false;
    if(!Branch7.IsNone()&&Branch7!=FName(Prefix+TEXT("C"))&&Branch7!=FName(Prefix+TEXT("D"))) return false;
    if(!Branch7.IsNone()&&Branch3.IsNone()) return false;
    if((!Branch3.IsNone()||!Branch7.IsNone()||PendingBranchStage!=0)&&!bLocked) return false;
    return !(PendingBranchStage==3&&!Branch3.IsNone()) && !(PendingBranchStage==7&&(Branch3.IsNone()||!Branch7.IsNone()));
}
FText DemoRunBuild::Title(FName Id)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoRunBuild::Title"));
    const TCHAR* Ids[]={TEXT("B01"),TEXT("B01-A"),TEXT("B01-B"),TEXT("B01-C"),TEXT("B01-D"),TEXT("B02"),TEXT("B02-A"),TEXT("B02-B"),TEXT("B02-C"),TEXT("B02-D"),TEXT("B03"),TEXT("B03-A"),TEXT("B03-B"),TEXT("B03-C"),TEXT("B03-D"),TEXT("B04"),TEXT("B04-A"),TEXT("B04-B"),TEXT("B04-C"),TEXT("B04-D"),TEXT("B05"),TEXT("B05-A"),TEXT("B05-B"),TEXT("B05-C"),TEXT("B05-D"),TEXT("B06"),TEXT("B06-A"),TEXT("B06-B"),TEXT("B06-C"),TEXT("B06-D"),TEXT("B07"),TEXT("B07-A"),TEXT("B07-B"),TEXT("B07-C"),TEXT("B07-D"),TEXT("B08"),TEXT("B08-A"),TEXT("B08-B"),TEXT("B08-C"),TEXT("B08-D"),TEXT("B09"),TEXT("B09-A"),TEXT("B09-B"),TEXT("B09-C"),TEXT("B09-D")}; // 固定可信稳定ID，不能解析外部资源路径。
    const TCHAR* Names[]={TEXT("冰裂突围"),TEXT("推进冰核"),TEXT("致密冰核"),TEXT("冷雾扩散"),TEXT("回温装置"),TEXT("冲刺突击"),TEXT("集中突击"),TEXT("连续突击"),TEXT("回撤推进"),TEXT("稳定射击"),TEXT("精准猎杀"),TEXT("冷静锁定"),TEXT("极限锁定"),TEXT("回收组件"),TEXT("暴露标记"),TEXT("节律装填"),TEXT("连拍供弹"),TEXT("短连序列"),TEXT("移动节拍"),TEXT("稳定装填"),TEXT("交叉火力"),TEXT("回身突击"),TEXT("压制追击"),TEXT("独立闭环"),TEXT("协同破势"),TEXT("战术锚点"),TEXT("撤离爆破"),TEXT("固守信标"),TEXT("短程接力"),TEXT("队伍校准"),TEXT("反击回路"),TEXT("贴身夺回"),TEXT("远距止损"),TEXT("沉着撤离"),TEXT("应急共享"),TEXT("抵近爆膛"),TEXT("双膛续压"),TEXT("阔口清阵"),TEXT("贴身护幕"),TEXT("震颤破甲"),TEXT("蓄势贯杀"),TEXT("溢伤连爆"),TEXT("贯心裂痕"),TEXT("幽灵步态"),TEXT("猎杀标定")}; // 与ID同序；既有稳定ID语义保持，新核心不进入旧随机天赋奖池。
    static_assert(UE_ARRAY_COUNT(Ids) == UE_ARRAY_COUNT(Names), "Build title IDs must match display names");
    for(int32 Index=0;Index<UE_ARRAY_COUNT(Ids);++Index) if(Id==FName(Ids[Index])) return FText::FromString(Names[Index]);
    return FText::GetEmpty();
}
FText DemoRunBuild::Describe(FName Id)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoRunBuild::Describe"));
    const UDemoRunBuildSettings* C=GetDefault<UDemoRunBuildSettings>(); // 集中配置，文案与服务器行为数值同源。
    const UDemoExpandedBuildSettings* E=GetDefault<UDemoExpandedBuildSettings>(); // 新核心独立设置，不改变旧B01～B03规则或存档解释。
    FString Text; // 只作为本次返回值构造，不存本地化状态或改变选择。
    if(Id==TEXT("B01")) Text=FString::Printf(TEXT("同敌%g秒内积满%d层短冻%g秒（单发+1，散弹单枪%d/%d丸+2/+3，慢射速弱点+2）；近战击碎推出%gcm爆裂造成%.0f%%近战伤害。Boss免控改为下一击+%.0f%%。"),C->IceChainSeconds,C->IceHits,C->FreezeSeconds,C->IcePelletDoubleThreshold,C->IcePelletFullThreshold,C->PushDistance,C->BurstRatio*100,C->BossBonus*100);
    else if(Id==TEXT("B01-A")) Text=FString::Printf(TEXT("冰裂推出%gcm，触发冷却%g秒；与致密冰核互斥。"),C->PushDistanceA,C->ShatterCooldownA);
    else if(Id==TEXT("B01-B")) Text=FString::Printf(TEXT("冰裂伤害%.0f%%，半径缩至%gcm；与推进冰核互斥。"),C->BurstRatioB*100,C->BurstRadiusB);
    else if(Id==TEXT("B01-C")) Text=FString::Printf(TEXT("冰裂实际命中的小怪减速%.0f%%，持续%g秒；Boss免疫。"),C->BurstSlow*100,C->BurstSlowSeconds);
    else if(Id==TEXT("B01-D")) Text=FString::Printf(TEXT("冰裂命中其他敌人回复%g生命，每%g秒一次。"),C->BurstHeal,C->BurstHealCooldown);
    else if(Id==TEXT("B02")) Text=FString::Printf(TEXT("战斗冲刺实际移动%gcm，随后%g秒内下一枪全部弹丸直击+%.0f%%；空枪换弹不消耗。"),C->DashMinimumDistance,C->AssaultSeconds,C->AssaultBonus*100);
    else if(Id==TEXT("B02-A")) Text=FString::Printf(TEXT("强化下一枪直击+%.0f%%，窗口缩为%g秒；与连续突击互斥。"),C->AssaultBonusA*100,C->AssaultSecondsA);
    else if(Id==TEXT("B02-B")) Text=FString::Printf(TEXT("冲刺提供2枪，每枪直击+%.0f%%，窗口%g秒；与集中突击互斥。"),C->AssaultBonusB*100,C->AssaultSeconds);
    else if(Id==TEXT("B02-C")) Text=FString::Printf(TEXT("强化枪击杀后移速+%.0f%%，持续%g秒；每次冲刺最多一次。"),C->AssaultMoveBonus*100,C->AssaultMoveSeconds);
    else if(Id==TEXT("B02-D")) Text=FString::Printf(TEXT("强化枪散布乘%g，相机后坐乘%g；不改变换弹或开镜限制。"),C->AssaultSpreadScale,C->AssaultRecoilScale);
    else if(Id==TEXT("B03")) Text=FString::Printf(TEXT("整枪命中弱点积层（慢射速弱点+2，散弹含弱点弹丸即计数），%g秒超时；%d层后%g秒内下一枪对首敌弱点直击+%.0f%%。"),C->PrecisionSeconds,C->PrecisionHits,C->PrecisionChargeSeconds,C->PrecisionBonus*100);
    else if(Id==TEXT("B03-A")) Text=FString::Printf(TEXT("层数保留%g秒，锁定射击增伤改为%.0f%%；与极限锁定互斥。"),C->PrecisionSecondsA,C->PrecisionBonusA*100);
    else if(Id==TEXT("B03-B")) Text=FString::Printf(TEXT("层数只保留%g秒，锁定射击增伤改为%.0f%%；与冷静锁定互斥。"),C->PrecisionSecondsB,C->PrecisionBonusB*100);
    else if(Id==TEXT("B03-C")) Text=TEXT("锁定射击命中首敌弱点，从原枪备用弹转移1发；不超弹匣、不免费造弹。");
    else if(Id==TEXT("B03-D")) Text=FString::Printf(TEXT("锁定射击弱点命中标记目标%g秒，下一次队友直击+%.0f%%并消费；单人允许自己下一枪消费。"),C->ExposeSeconds,C->ExposeBonus*100);
    else if(Id==TEXT("B04")) Text=FString::Printf(TEXT("装填中在进度%.0f%%～%.0f%%再次按R，完整装填后获得%d枪供弹机会，持续%g秒；每枪首次敌人直击从原枪备弹转入1发，不加快装填。"),E->RhythmWindowStart*100,E->RhythmWindowEnd*100,E->RhythmCredits,E->RhythmCreditSeconds);
    else if(Id==TEXT("B04-A")) Text=TEXT("连拍供弹：保留逐枪命中供弹；成功节拍后必须连续命中，空枪终止本次剩余机会；与短连序列互斥。");
    else if(Id==TEXT("B04-B")) Text=FString::Printf(TEXT("短连序列：放弃供弹，节拍后%d个凭据（稳定路线%d个）常规武器可排队第二枪，拉栓武器改为本次直击+%.0f%%；遵守原射速与拉栓周期。"),E->RhythmCredits,E->RhythmStableCredits,E->RhythmBoltActionBonus*100);
    else if(Id==TEXT("B04-C")) Text=FString::Printf(TEXT("有效战斗冲刺后%g秒内完成节拍，本次凭据允许跨一次切枪；不减冲刺CD、不跨关。"),E->RhythmCarrySeconds);
    else if(Id==TEXT("B04-D")) Text=FString::Printf(TEXT("取消主动按窗；本弹匣至少%d次有效命中（双管等小弹匣按容量下调）后完整装填，自动获得%d枪供弹机会。"),E->RhythmStableHits,E->RhythmStableCredits);
    else if(Id==TEXT("B05")) Text=FString::Printf(TEXT("近战实际伤敌且目标存活时破势%g秒；随后新发射的枪击命中同一目标，消费一次额外非元素冲击（源伤害×%.0f%%，至多%gHP）。原枪已击杀只消费、不攻击尸体。"),E->CrossfireWindow,E->CrossfireRatio*100,E->CrossfireDamageCap);
    else if(Id==TEXT("B05-A")) Text=FString::Printf(TEXT("近战→枪械兑现时，固定冲击预算50%%给原目标，另50%%均分至%gcm内最多%d名其他可见敌人；空缺份额作废、不穿墙、不推动Boss。"),E->CrossfireRadius,E->CrossfireTargets);
    else if(Id==TEXT("B05-B")) Text=TEXT("改为枪械实际命中→近战兑现同一目标的集中非元素冲击；不位移吸附，仍需自己接近。");
    else if(Id==TEXT("B05-C")) Text=FString::Printf(TEXT("本人完成整次交叉火力时，从凭据原枪备用弹转入%d发；受原枪弹匣/备用限制，不免费造弹。"),E->CrossfireRounds);
    else if(Id==TEXT("B05-D")) Text=TEXT("下一名队友用对应动作消费破势，同队有其他存活参战者时本人不可抢兑；真单人自己兑现，服务器串行只消费一次、固定预算不随人数增加。");
    else if(Id==TEXT("B06")) Text=FString::Printf(TEXT("G保留队伍标记并部署半径%gcm锚点；区内%g秒内充能%d格（多弹丸%d丸或慢射速弱点+2格），首次离开释放%gHP非元素脉冲，最多%d敌人且不穿墙。"),E->AnchorRadius,E->AnchorChargeSeconds,E->AnchorChargeHits,E->AnchorPelletDoubleThreshold,E->AnchorDamage,E->AnchorTargets);
    else if(Id==TEXT("B06-A")) Text=TEXT("撤离爆破：保持充满后离场兑现整圆脉冲的路线；未充满离开失效，脉冲后撤销锚点。");
    else if(Id==TEXT("B06-B")) Text=FString::Printf(TEXT("固守信标：充满后留在区域内，下一次本人有效直击沿瞄准方向释放%gcm中远程定向脉冲；离开会失去机会。"),E->AnchorDirectedRange);
    else if(Id==TEXT("B06-C")) Text=FString::Printf(TEXT("首次锚点脉冲真实伤敌后，%g秒内可在新的合法G落点迁移一次并绕过部署CD；旧点撤销，第二点不再生成接力机会。"),E->AnchorChainSeconds);
    else if(Id==TEXT("B06-D")) Text=TEXT("区域内存活队友的有效直击也可贡献同一份充能；全队共用该锚点一次预算，定向兑现仍由锚点持有者完成。");
    else if(Id==TEXT("B07")) Text=FString::Printf(TEXT("敌方真实损血后%g秒内近战实际伤敌，回复min(固定损血预算,近战真实HP×%.0f%%)；预算至多损失%.0f%%且不超过%gHP，%g秒CD，不叠加/刷新/复活。"),E->CounterWindow,E->CounterMeleeRatio*100,FMath::Min(0.5f,E->CounterLossRatio)*100,E->CounterBudgetCap,E->CounterCooldown);
    else if(Id==TEXT("B07-A")) Text=FString::Printf(TEXT("贴身夺回：近战兑现比例提高至真实伤害的%.0f%%，仍受原固定损血预算与%gHP上限限制；未命中不回血。"),E->CounterHeavyRatio*100,E->CounterBudgetCap);
    else if(Id==TEXT("B07-B")) Text=FString::Printf(TEXT("远距止损：改为随后枪械弱点直击兑现（散弹含弱点弹丸亦可触发），真实伤害按%.0f%%计入；与近战路线互斥。"),E->CounterWeakRatio*100);
    else if(Id==TEXT("B07-C")) Text=FString::Printf(TEXT("沉着撤离：本次机会期间一次实际成功冲刺可保留预算到冲刺结束后%g秒；失败位移不延长，不改冲刺CD或无敌帧。"),E->CounterDashGrace);
    else if(Id==TEXT("B07-D")) Text=FString::Printf(TEXT("应急共享：兑现时把同一治疗预算的%.0f%%分给%gcm内最低生命比例的存活队友，本人少得相应份额；无队友正常本人领取，未用份额作废。"),E->CounterShareRatio*100,E->CounterShareRadius);
    else if(Id==TEXT("B08")) Text=FString::Printf(TEXT("【推荐：散弹/近距】%gcm内直击积攒膛压（单发+%d，多弹丸每命中1丸+1，%g秒维持）；满%d层立即引爆%gcm非元素震波（整枪实伤×%.0f%%，%g～%gHP，最多%d敌）并短退小怪；Boss免退承受%.0f%%集中冲击。"),E->CloseRangeCm,E->CloseSingleShotStacks,E->ClosePressureDecaySeconds,E->ClosePressureMax,E->CloseBurstRadius,E->CloseBurstRatio*100,E->CloseBurstMinDamage,E->CloseBurstCap,E->CloseBurstTargets,E->CloseBossMultiplier*100);
    else if(Id==TEXT("B08-A")) Text=FString::Printf(TEXT("双膛续压：触发爆膛时若弹匣≤1发，从原枪备弹转入1发（CD%g秒），且%g秒内下一枪散布×%g；与阔口清阵互斥。"),E->CloseReloadCooldown,E->CloseTightSpreadSeconds,E->CloseTightSpreadScale);
    else if(Id==TEXT("B08-B")) Text=FString::Printf(TEXT("阔口清阵：爆膛升级为前方%gcm宽扇面、最多%d敌、上限%gHP；冲击击杀获得+%.0f%%移速/%g秒；与双膛续压互斥。"),E->CloseBurstRadiusB,E->CloseBurstTargetsB,E->CloseBurstCapB,E->CloseKillMoveBonus*100,E->CloseKillMoveSeconds);
    else if(Id==TEXT("B08-C")) Text=FString::Printf(TEXT("贴身护幕：爆膛实际伤敌时回复自身%gHP（每%g秒一次，受最大生命钳制）。"),E->CloseGuardHeal,E->CloseGuardCooldown);
    else if(Id==TEXT("B08-D")) Text=FString::Printf(TEXT("震颤破甲：被爆膛实伤的存活敌人陷入%g秒公开易伤，全队对其直击伤害+%.0f%%（不叠层，刷新期限）。"),E->CloseTremorSeconds,E->CloseTremorBonus*100);
    else if(Id==TEXT("B09")) Text=FString::Printf(TEXT("【推荐：狙击/慢射速】停火自动蓄势%g秒满（开镜×%g速，拉栓天然满蓄）；满蓄直击+%.0f%%（≥%gcm或弱点再+%.0f%%）；满蓄击杀将%.0f%%过量伤害（%g～%gHP）转化为%gcm内最近可见敌人的非元素追击。"),E->MarksmanChargeSeconds,E->MarksmanScopedRate,E->MarksmanBaseBonus*100,E->MarksmanLongRangeCm,E->MarksmanBonusExtra*100,E->MarksmanOverkillRatio*100,E->MarksmanOverkillMin,E->MarksmanOverkillCap,E->MarksmanOverkillRadius);
    else if(Id==TEXT("B09-A")) Text=FString::Printf(TEXT("溢伤连爆：满蓄击杀溢伤转为%gcm范围震波（最多%d敌均分预算），并立即返还%.0f%%蓄势且从备弹补1发入匣；与贯心裂痕互斥。"),E->MarksmanOverkillRadiusA,E->MarksmanOverkillTargetsA,E->MarksmanKillRefundFocusA*100);
    else if(Id==TEXT("B09-B")) Text=FString::Printf(TEXT("贯心裂痕：满蓄命中存活目标弱点叠层（最高%d层/%g秒），立即附加本次实伤×%.0f%%/层的非元素穿透伤害（上限%gHP）并令下次蓄势+%.0f%%速；与溢伤连爆互斥。"),E->MarksmanRiftMaxStacksB,E->MarksmanRiftDurationB,E->MarksmanRiftRatioPerStackB*100,E->MarksmanRiftDamageCapB,E->MarksmanRiftSpeedBonusB*100);
    else if(Id==TEXT("B09-C")) Text=FString::Printf(TEXT("幽灵步态：蓄势≥50%%时抵消%.0f%%开镜移速惩罚，冲刺不打断蓄势；满蓄弱点命中获得+%.0f%%移速/%g秒。"),E->MarksmanScopeMoveMitigationC*100,E->MarksmanWeakMoveBonusC*100,E->MarksmanWeakMoveSecondsC);
    else if(Id==TEXT("B09-D")) Text=FString::Printf(TEXT("猎杀标定：满蓄命中存活目标施加%g秒公开标定，全队直击+%.0f%%；队友击杀（单人自己补杀）立即满蓄并从备弹补1发。"),E->MarksmanMarkSecondsD,E->MarksmanMarkBonusD*100);
    return FText::FromString(Text);
}
