// 职责：R02无底板边缘信息与像素单点准星，非R02保留旧布局；Hub/技能/ADS仍沿真实阶段和拥有者门禁。
// 依赖：既有Canvas字体/主题及权威复制；实体老虎机用大字只读金额区，沿原HUD绘制，不改变伤害/技能/经济/输入规则。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/Flow/DemoMenuFlowComponent.h" // 本人输入稿/已确认待下注金额；仅显示，不提交或推断实际扣款。
#include "UI/R02/DemoUITheme.h" // CDO持有只读主题；不在HUD Tick加载字体/资源。
#include "UI/DemoHUDReadout.h" // 大GAS值不缩成不可读的小字；只控制表现，不截断原属性。
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Player/Coop/DemoRoomSubsystem.h"
#include "Game/DemoGameState.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h" // 节律装填条只读真实武器周期与配置窗口，不在HUD判定成功。
#include "Interaction/DemoInteractable.h"
#include "Interaction/HubShops/Casino/DemoCasinoDeviceInputComponent.h" // 本人20Hz焦点/短提示只读投影，不让HUD再做一套选键算法。
#include "AI/DemoEnemy.h"
#include "AI/DemoEnemyRoster.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h" // Boss查询只读当前World公开状态，不借其他玩家或历史场景Actor。
#include "EngineUtils.h"
#include "Debug/DemoLog.h"
#if WITH_EDITOR
#include "Debug/DemoEditorCombatPrototype.h" // 候选仅Editor单场试玩；包体没有查询、状态文案或调试权限。
#endif

const ADemoEnemy* ADemoHUD::GetR02LiveBoss() const
{
    DEMO_LOG_TICK();
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 只读实际公开阶段，隐藏样本/未加载身份也不据物理位置推断关卡。
    if(!IsR02GameplayPresentation()||!State||State->Phase!=EDemoPhase::Combat) return nullptr;
    if(R02BossObservedFrame!=GFrameCounter)
    {
        R02BossObservedFrame=GFrameCounter; R02BossActor.Reset(); // 每帧只做一次有界场景查询，复用者不各自扫描敌人。
        for(TActorIterator<ADemoEnemy> It(GetWorld());It;++It)
            if(It->IsBoss()&&It->IsAlive()&&It->GetHealth()>0.f) { R02BossActor=*It; break; }
    }
    const ADemoEnemy* Boss=R02BossActor.Get(); // 弱引用不延长生命周期；同帧死亡仍重核公开生死事实。
    return IsValid(Boss)&&Boss->IsBoss()&&Boss->IsAlive()&&Boss->GetHealth()>0.f?Boss:nullptr;
}

void ADemoHUD::DrawR02BossHeader(const ADemoGameState* State,const ADemoEnemy* Boss)
{
    DEMO_LOG_TICK();
    if(!IsR02GameplayPresentation()||!State||State->Phase!=EDemoPhase::Combat||!IsValid(Boss)||!Boss->IsBoss()||!Boss->IsAlive()) return;
    const UDemoEnemyRoster* Roster=Boss->FindComponentByClass<UDemoEnemyRoster>(); // 真实公开复制身份/阶段；不借服务器私有Config或按HP自推阶段。
    FString Name=Roster?Roster->GetLabel():FString(); // 既有公开文案包含“名称 · 阶段N”；下方仅在表现层分行，不作任何权限判断。
    FString NameOnly,StatusTail; // 只拆现有展示分隔符，名称仍来自真实Roster，不硬编码守卫者/典狱长。
    if(Name.Split(TEXT(" · "),&NameOnly,&StatusTail)) Name=NameOnly;
    if(Name.IsEmpty()) Name=TEXT("首领"); // 身份尚未复制时只给通用称呼，不伪造具体Boss。
    const bool bKnownBoss=Roster&&(Roster->GetKind()==EDemoRosterKind::Warden||Roster->GetKind()==EDemoRosterKind::Weaver);
    const uint8 Phase=Roster?Roster->GetPhase():0; // Visual.Phase原公开1..3锁存值，不能把概念图II或血量阈值当事实。
    const FString PhaseText=bKnownBoss&&Phase>=1&&Phase<=3?TEXT("阶段 ")+DemoUIR02::ToRoman(Phase):TEXT("阶段同步中");
    const FString Detail=FString::Printf(TEXT("%s %s  ·  %s"),State->bEndless?TEXT("ENDLESS"):TEXT("LEVEL"),*DemoUIR02::ToRoman(State->LevelNumber),*PhaseText);
    const float CenterX=ViewWidth*.5f; // 固定顶部中轴；不移动相机、世界曝光、角色或武器。
    Label(Name,CenterX,28.f,44.f,R02HUDTheme->Ink,true);
    Label(Detail,CenterX,88.f,14.f,R02HUDTheme->Muted,true);
    R02HUDLine(CenterX-250.f,112.f,500.f,DemoHUDReadout::Fraction(Boss->GetHealth(),Boss->GetMaxHealth()),R02HUDTheme->Ember,2.f); // 原真实HP比例，固定2物理px；不加数值账本或底板。
}

bool ADemoHUD::ShouldDrawWorldReticle(const ADemoGameState* State,const ADemoCharacter* Player) const
{
    DEMO_LOG_TICK();
    const ADemoPlayerController* PC=Cast<ADemoPlayerController>(PlayerOwner); // 当前HUD拥有者，不使用Player0/观战目标的私有状态。
    if (!State || !Player || !PC || PC->GetPawn()!=Player || !PC->IsLocalController() || PC->GetViewTarget()!=Player || Player->IsCoopEliminated() || PC->IsPaused() || PC->HasBlockingOverlay() || PC->IsUpgradeMenuOpen() || PC->IsNextLevelConfirmationOpen()) return false;
    const bool bWorldPhase=State->Phase==EDemoPhase::Hub || State->Phase==EDemoPhase::Combat || State->Phase==EDemoPhase::Intermission;
    const UDemoAttributeSet* Attributes=Player->GetDemoAttributes(); // 初始化/生命切换时隐藏，不展示死亡本人或加载中的假瞄准点。
    const UDemoWeaponComponent* Equipment=Player->GetWeaponComponent();
    return bWorldPhase && Attributes && Attributes->GetHealth()>0.f && (!Equipment || Equipment->GetScopeLevel()==0); // Scope保持原专用镜线；无枪/空弹/近战仍可用点瞄设备。
}

void ADemoHUD::DrawStatus(const ADemoGameState* State, ADemoCharacter* Player, const UDemoAttributeSet* Attributes)
{
    DEMO_LOG_TICK();
    if(!State||!Player||!Attributes||Player->IsCoopEliminated()) return; // 观战使用独立公开目标面板，不能显示已阵亡本人旧枪/技能/生命。
    const bool bR02=IsR02GameplayPresentation(); // 本帧由DrawHUD明确排除菜单/终局/靶场，不以画面位置扩大玩法权限。
    const ADemoEnemy* R02Boss=bR02?GetR02LiveBoss():nullptr; // 仅Boss存在时收起重复常态关数/银币；其余常态布局完全保留。
    const bool bCombat=State->Phase==EDemoPhase::Combat; // 准星许可与Combat伤害许可分开，不把备战弹视作战斗。
    const bool bIntermission=State->Phase==EDemoPhase::Intermission; // 清关停留当前区域，不能误称初始安全区。
    const bool bPreparation=State->Phase==EDemoPhase::Hub||bIntermission; // 仅备战阶段提供近距离终端提示。
    const auto& View=GetPlayerView(); // 当前本机拥有者快照，不借GameState旧钱包字段。
    const auto* ASC=Player->GetDemoASC(); // 当前Avatar所属PS ASC，本帧借用。
    const float Bottom=ViewHeight-132.f; // 底部信息共用基线，另留24px状态栏与32px外边距。
    const FLinearColor Plate=DemoUI::Surface.CopyWithNewOpacity(.70f); // 局部阅读衬底；不覆盖战斗中心、不作用于前景文字。
    const auto* Equipment=Player->GetWeaponComponent(); // 当前实际装备，仅本帧借用；R02/旧路径共用真实读数。
    const auto* Weapon=Equipment?Equipment->GetActiveWeapon():nullptr; // 近战装配时仍由下面独立槽判定，不能沿枪械旧值假报。
    const auto* Melee=Player->FindComponentByClass<UDemoMeleeComponent>(); // 本人独立近战槽，不继承枪械弹药类型。
    const bool bMelee=Melee&&Melee->IsEquipped()&&Melee->GetDefinition(); // 实际DA与装配状态均就绪才显示近战。
    const bool bSkillsAllowed=Player->CanUsePlayerSkills(); // 与GA共用原许可；不因精简文字刷新冷却或放开阶段。
    const float SkillX=ViewWidth*.5f-211.f; // 旧路径核心/技能锚点；R02使用独立紧凑双槽，不常驻核心第三栏。

    if(bR02)
    {
        // 用户参考的层级由真实大数字和留白构成；不复刻背景、十字或生成图中的标注文字。
        if((bCombat||bIntermission)&&!R02Boss) Label(DemoUIR02::ToRoman(State->LevelNumber),44,24,100,DemoUI::Ink); // Boss关数由顶部C07真值下行承接，常态不变。
        else if(State->Phase==EDemoPhase::Hub) Label(TEXT("安全区"),44,38,42,DemoUI::Ink);
        const float WalletX=ViewWidth-252.f; // 右上保持本人真钱包；战斗不常驻金币账本/难度。
        if(State->Phase==EDemoPhase::Hub)
        {
            Label(FString::Printf(TEXT("金币  %d"),View.Progress.Coins),WalletX,36,18,DemoUI::Ink);
            if(View.Progress.SilverCoins!=0) Label(FString::Printf(TEXT("银币  %d"),View.Progress.SilverCoins),WalletX,64,18,DemoUI::Ink); // 2026-11-19文字减负：银币回安全区按规则清零，常态0不常驻；异常非0仍如实显示。
        }
        else if(!R02Boss) Label(FString::Printf(TEXT("银币  %d"),View.Progress.SilverCoins),WalletX,36,22,DemoUI::Ink); // Boss场聚焦战斗，钱包仍可从既有菜单读取，不改变经济。

        const FString Health=DemoHUDReadout::Number(Attributes->GetHealth()); // 完整实际生命，极端数值沿原有界格式，不伪造三位上限。
        const float HealthSize=FitR02NumeralSize(Health,84.f,248.f); // 常态远大于旧43号；只有实际长数值才按真实衬线字格收敛。
        Label(Health,44,Bottom,HealthSize,Attributes->GetHealth()<=Attributes->GetMaxHealth()*.25f?DemoUI::Danger:DemoUI::Ink);
        R02HUDLine(48,ViewHeight-44.f,248,DemoHUDReadout::Fraction(Attributes->GetHealth(),Attributes->GetMaxHealth()),DemoUI::Ink); // 与受击残影原锚点一致，1物理像素不加VITALS/分栏。
        const float DefaultHealthCap=GetDefault<UDemoAttributeSet>()->GetMaxHealth(); // 项目属性CDO基准，不把所有角色最大值硬编码为100。
        const bool bNotableHealthCap=!FMath::IsFinite(Attributes->GetHealth())||Attributes->GetHealth()<0.f||!FMath::IsFinite(Attributes->GetMaxHealth())||Attributes->GetMaxHealth()<=0.f
            ||!FMath::IsNearlyEqual(Attributes->GetMaxHealth(),DefaultHealthCap,.01f)||Attributes->GetHealth()>Attributes->GetMaxHealth(); // 增益或异常时才补小号上限，常态只留当前数。
        if(bNotableHealthCap) Label(TEXT("/ ")+DemoHUDReadout::Number(Attributes->GetMaxHealth()),300,ViewHeight-72.f,14,DemoUI::Muted);

        const float WeaponX=ViewWidth-270.f; // 右下真实弹数与备用相邻，最后44px留作安全边界。
        if(bMelee)
        {
            Label(Melee->GetDefinition()->DisplayName.ToString().Left(18),WeaponX,Bottom+30,30,DemoUI::Ink);
            if(Melee->IsAttacking()) Label(TEXT("挥击中"),WeaponX,ViewHeight-48.f,14,DemoUI::Ink); // 仅实际动作态出现；不常驻左右键教学或“就绪”说明。
        }
        else if(Weapon)
        {
            const FString Rounds=FString::Printf(TEXT("%02d"),Weapon->GetAmmo()); // 保留完整实际弹量，不因两位参考图截断增益弹匣。
            const FString Reserve=TEXT("/ ")+(Weapon->Config.bInfiniteReserve?FString(TEXT("∞")):FString::FromInt(Weapon->GetReserveAmmo())); // 斜线后为真实备用而非弹匣容量。
            Label(Rounds,WeaponX,Bottom-10.f,FitR02NumeralSize(Rounds,84.f,132.f),Weapon->GetAmmo()==0?DemoUI::Danger:DemoUI::Ink);
            Label(Reserve,ViewWidth-132.f,Bottom+46.f,FitR02NumeralSize(Reserve,28.f,88.f),DemoUI::Muted);
            const auto* Ammo=Player->FindComponentByClass<UDemoAmmoComponent>(); // 只读本人单一装配弹种，未同步时明确等待，不假定普通弹。
            const FString AmmoName=Ammo&&Ammo->GetCatalog()&&Ammo->GetCatalog()->Entries.IsValidIndex(Ammo->GetSelected())?Ammo->GetCatalog()->Entries[Ammo->GetSelected()].Name.ToString():TEXT("弹药待同步");
            Label(Weapon->Config.DisplayName.ToString().Left(18)+TEXT(" · ")+AmmoName,WeaponX,ViewHeight-48.f,14,DemoUI::Ink); // 单行正式武器名/真弹种，底缘留空，不显示伤害乘弹丸或切枪教学。
        }

        const float CenterX=ViewWidth*.5f,SkillY=ViewHeight-64.f; // 中下双槽，仅一行；与左右大读数留开完整间距。
        R02HUDLine(CenterX-196.f,ViewHeight-76.f,392,1.f,DemoUI::Muted.CopyWithNewOpacity(.35f));
        R02HUDLine(CenterX-196.f,ViewHeight-36.f,392,1.f,DemoUI::Muted.CopyWithNewOpacity(.35f));
        const double LocalNow=GetWorld()?GetWorld()->GetTimeSeconds():0.;
        for(int32 Index=0;Index<2;++Index)
        {
            const FGameplayTag Tag=Index==0?DemoTags::DashCooldown:DemoTags::HealCooldown; // 只读实际GAS标签，不从按键或默认GA时长推算。
            float Remaining=0.f,Duration=0.f; // 当前活跃GE的最长剩余秒数，普通阶段切换仍保留原冷却。
            if(ASC) for(const auto& Time:ASC->GetActiveEffectsTimeRemainingAndDuration(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
                if(Time.Key>Remaining) { Remaining=Time.Key; Duration=Time.Value; }
            const bool bFull=Index==1&&Attributes->GetHealth()>=Attributes->GetMaxHealth(); // 满血治疗保持原不可用表达。
            const bool bReadyFlash=Index==0?(LocalNow<AbilityFeedback.DashReadyFlashUntil):(LocalNow<AbilityFeedback.HealReadyFlashUntil);
            const bool bActiveFlash=Index==0?(LocalNow<AbilityFeedback.DashFlashUntil):(LocalNow<AbilityFeedback.HealFlashUntil);
            const FLinearColor SkillColor=(!bSkillsAllowed||Remaining>0.f||bFull)?DemoUI::Muted:(bReadyFlash||bActiveFlash)?R02HUDTheme->Ember:DemoUI::Ink;
            const FString StateText=!bSkillsAllowed?TEXT(" · 不可用"):Remaining>0.f?FString::Printf(TEXT(" · %.1fs"),Remaining):bFull?TEXT(" · 满血"):TEXT(""); // 同一紧凑槽保留真实拒绝/冷却，常态不重复写“就绪”。
            Label((Index==0?FString(TEXT("SHIFT 冲刺")):FString(TEXT("Q 治疗")))+StateText,CenterX+(Index==0?-98.f:98.f),SkillY,16,SkillColor,true);
            const float SlotX=CenterX+(Index==0?-180.f:16.f);
            if(Remaining>0.f&&Duration>0.f) R02HUDLine(SlotX,ViewHeight-40.f,164.f,1.f-Remaining/Duration,R02HUDTheme->Ember,1.f);
            else if(bReadyFlash) R02HUDLine(SlotX,ViewHeight-40.f,164.f,1.f,R02HUDTheme->Ember,2.f);
        }
        Label(TEXT("|"),CenterX,SkillY,16,DemoUI::Muted,true); // 纯文字分隔，不新增技能热区或输入绑定。
        DrawAbilityProcToasts(Player,Attributes);
    }

    if(!bR02)
    {

    if(!bR02)
    {
        Panel(32,28,342,bCombat?166.f:118.f,Plate,2);
        Panel(32,28,2,bCombat?166.f:118.f,DemoUI::Gold,0);
    } // R02保留信息锚点和目标区空白，不增加底板或装饰边框。
    Label(State->bEndless?TEXT("BREACH / ENDLESS"):TEXT("BREACH / OPERATION"),48,40,13,DemoUI::Gold);
    const FString PhaseTitle=bPreparation
        ? State->bEndless?FString::Printf(TEXT("无尽 · 下一关 %d"),State->LevelNumber+1)
            :FString::Printf(TEXT("%s / %02d"),bIntermission?TEXT("原地备战"):TEXT("安全区"),FMath::Min(DemoCombatConfig::LevelCount,State->LevelNumber+1))
        :bCombat?FString::Printf(TEXT("第 %02d%s"),State->LevelNumber,State->bEndless?TEXT(" 关 / 无尽"):TEXT(" / 10 关"))
            :State->Phase==EDemoPhase::Reward?TEXT("区域肃清"):TEXT("行动结束"); // 阶段来自GS，不根据物理位置开放权限。
    const FString R02PhaseTitle=bR02&&bCombat?DemoUIR02::ToRoman(State->LevelNumber)+FString::Printf(TEXT(" · 第%d关"),State->LevelNumber):PhaseTitle; // 罗马号只显示，旁有中文真实关数，无尽超过十关回阿拉伯数。
    Label(R02PhaseTitle,48,62,bR02&&bCombat?32:27,DemoUI::Ink);
    if(bPreparation) Label(bIntermission?TEXT("前往中心终端 · 整备后继续突破"):TEXT("整备装备与核心 · 出发前可自由准备"),48,108,14,DemoUI::Muted);
    if(State->Phase==EDemoPhase::Hub&&GetNetMode()!=NM_Standalone)
        if(const auto* Rooms=GetGameInstance()->GetSubsystem<UDemoRoomSubsystem>()) Label(Rooms->GetStatus().Left(38),32,159,13,DemoUI::Accent);

    const float WalletX=ViewWidth-264.f; // 两行数值右侧保留边距，10位int32余额使用独立等宽数字段。
    if(!bR02) Panel(WalletX,28,232,144,Plate,2); // R02钱包直接排字；旧变体保留原衬底。
    else if(!bCombat) R02HUDLine(WalletX+16,132,200,0.f,DemoUI::Muted); // 安全区账本1px分隔；战斗压成两行轻量读数。
    if(bR02&&bCombat) Label(FString::Printf(TEXT("银币  %d"),View.Progress.SilverCoins),WalletX+16,40,18,DemoUI::Ink); // 战斗只常驻小号本轮收入，不把双币账本铺在瞄准画面。
    else
    {
        Label(TEXT("PERSONAL WALLET"),WalletX+16,40,12,DemoUI::Muted);
        Label(TEXT("金币"),WalletX+16,66,14,DemoUI::Gold);
        Label(FString::FromInt(View.Progress.Coins),WalletX+66,62,22,DemoUI::Gold);
        Label(TEXT("银币"),WalletX+16,99,14,DemoUI::Ink);
        Label(FString::FromInt(View.Progress.SilverCoins),WalletX+66,95,22,DemoUI::Ink);
    }
    const TCHAR* Difficulty=State->Difficulty==EDemoDifficulty::Easy?TEXT("简单"):State->Difficulty==EDemoDifficulty::Hard?TEXT("困难"):State->Difficulty==EDemoDifficulty::Hell?TEXT("地狱"):TEXT("普通"); // DifficultyName是DT行键，不能直接当中文显示名称。
    Label(State->bEndless?TEXT("无尽 / 地狱基线"):FString(Difficulty)+TEXT("难度"),WalletX+16,bR02&&bCombat?68:146,13,DemoUI::Ink);

    if(!bR02) Panel(32,Bottom,280,106,Plate,2);
    Label(TEXT("VITALS"),48,Bottom+12,12,DemoUI::Muted);
    const FString Health=DemoHUDReadout::Number(Attributes->GetHealth()); // 超大健康使用有限科学记数，避免数十位读数退化成2px字体。
    const float HealthSize=bR02?FitR02NumeralSize(Health,43.f,96.f):FMath::Min(43.f,96.f/FMath::Max(1.f,Health.Len()*.64f)); // R02使用真实衬线格测量；旧变体保留原字体预算。
    const FString HealthCap=TEXT("/ ")+DemoHUDReadout::Number(Attributes->GetMaxHealth()); // 上限与当前值使用同一口径，不假设固定3位。
    Label(Health,48,Bottom+31,HealthSize,Attributes->GetHealth()<=Attributes->GetMaxHealth()*.25f?DemoUI::Danger:DemoUI::Ink);
    Label(HealthCap,159,Bottom+53,FMath::Min(18.f,136.f/FMath::Max(1.f,HealthCap.Len()*.64f)),DemoUI::Muted);
    if(bR02) R02HUDLine(48,Bottom+88,248,DemoHUDReadout::Fraction(Attributes->GetHealth(),Attributes->GetMaxHealth()),DemoUI::Accent);
    else Meter(48,Bottom+88,248,4,DemoHUDReadout::Fraction(Attributes->GetHealth(),Attributes->GetMaxHealth()),DemoUI::Accent);

    const float WeaponX=ViewWidth-340.f; // 独立右下装备区，留出散弹描述与长中文型号的空间。
    if(!bR02) Panel(WeaponX,Bottom-28,308,134,Plate,2); // R02装备读数无底板，原锚点保留切换/近战/元素说明。
    else R02HUDLine(WeaponX+16,Bottom+104,276,0.f,DemoUI::Muted);
    if(bMelee)
    {
        Label(Melee->GetDefinition()->DisplayName.ToString().Left(18),WeaponX+16,Bottom+12,15,DemoUI::Muted);
        Label(Melee->IsAttacking()?TEXT("挥击中"):TEXT("近战就绪"),WeaponX+16,Bottom+36,29,DemoUI::Ink);
        const auto& Attacks=Melee->GetDefinition()->Attacks; // 实际DA配置动作名，不硬编码旧棒球棍动作。
        const FString Main=Melee->GetDefinition()->HorizontalAttackSequence.Num()>1?TEXT("交替连斩")
            :Attacks.IsValidIndex(0)?Attacks[0].DisplayName.ToString():TEXT("攻击"); // 多招左键显示循环职责，右键继续读取实际副攻名；单招与旧枪械提示保持。
        const FString Hint=Attacks.IsValidIndex(1)?FString::Printf(TEXT("左键 %s / 右键 %s"),*Main,*Attacks[1].DisplayName.ToString()):FString::Printf(TEXT("左键 %s"),*Main);
        Label(Hint.Left(28),WeaponX+16,Bottom+82,12,DemoUI::Muted);
    }
    else if(Weapon)
    {
        Label(Weapon->Config.DisplayName.ToString().Left(22),WeaponX+16,Bottom+12,15,DemoUI::Muted);
        const FString Rounds=FString::Printf(TEXT("%02d"),Weapon->GetAmmo()); // MagazineBonus允许到10000，不以两位绘制预算截断真实弹量。
        const float AmmoSize=bR02?FitR02NumeralSize(Rounds,43.f,90.f):FMath::Min(43.f,90.f/FMath::Max(1.f,Rounds.Len()*.64f)); // 大弹量按真实R02字格缩放，保留完整整数。
        Label(Rounds,WeaponX+16,Bottom+33,AmmoSize,Weapon->GetAmmo()==0?DemoUI::Danger:DemoUI::Ink);
        const FString Capacity=FString::Printf(TEXT("/ %d"),Weapon->GetCapacity()); // 容量也可能五位，独立92px区域不挤入备用列。
        Label(Capacity,WeaponX+118,Bottom+53,FMath::Min(18.f,84.f/FMath::Max(1.f,Capacity.Len()*.64f)),DemoUI::Muted);
        const FString Reserve=Weapon->Config.bInfiniteReserve?TEXT("∞"):FString::FromInt(Weapon->GetReserveAmmo()); // 备用值独立一行，最大int32也不会与“备用”混排压缩。
        Label(TEXT("备用"),WeaponX+210,Bottom+38,11,DemoUI::Muted);
        Label(Reserve,WeaponX+210,Bottom+56,FMath::Min(15.f,82.f/FMath::Max(1.f,Reserve.Len()*.64f)),DemoUI::Muted);
        const auto* Ammo=Player->FindComponentByClass<UDemoAmmoComponent>(); // 主副枪共用本人单一装配，目录缺失安全回退。
        const FString AmmoName=Ammo&&Ammo->GetCatalog()&&Ammo->GetCatalog()->Entries.IsValidIndex(Ammo->GetSelected())?Ammo->GetCatalog()->Entries[Ammo->GetSelected()].Name.ToString():TEXT("弹药待同步");
        Label(AmmoName+TEXT(" · 伤害 ")+DemoHUDReadout::Number(Weapon->GetDamagePerPellet(),1)+FString::Printf(TEXT(" × %d"),Weapon->GetProjectilesPerShot()),WeaponX+16,Bottom+82,12,DemoUI::Accent);
    }
    Label(Equipment&&Equipment->GetPrimaryIndex()!=INDEX_NONE?TEXT("[1] 主武器   [2] 手枪   [3] 近战"):TEXT("[2] 手枪   [3] 近战 · 终端装备主武器"),WeaponX+12,Bottom-22,12,DemoUI::Ink);

    for(int32 Index=0;Index<2;++Index)
    {
        const float X=SkillX+Index*131.f; // 左冲刺、右治疗，同一底部基线。
        const FGameplayTag Tag=Index==0?DemoTags::DashCooldown:DemoTags::HealCooldown; // 原GAS冷却标签，不访问GA CDO默认时长。
        float Remaining=0.f,Duration=0.f; // 读取同一个活跃GE的剩余/总秒数，遗物改变冷却时进度仍正确。
        if(ASC) for(const auto& Time:ASC->GetActiveEffectsTimeRemainingAndDuration(FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(Tag))))
            if(Time.Key>Remaining) { Remaining=Time.Key; Duration=Time.Value; }
        const bool bFull=Index==1&&Attributes->GetHealth()>=Attributes->GetMaxHealth(); // 满血不展示治疗就绪。
        const bool bReady=bSkillsAllowed&&Remaining<=0.f&&!bFull; // 表现不能当作实际技能授权。
        if(!bR02) Panel(X,Bottom+16,120,90,Plate,2);
        Label(Index==0?TEXT("SHIFT"):TEXT("Q"),X+12,Bottom+28,12,DemoUI::Gold);
        Label(Index==0?TEXT("冲刺"):TEXT("治疗"),X+12,Bottom+48,19,DemoUI::Ink);
        const FString Status=!bSkillsAllowed?TEXT("当前不可用"):Remaining>0.f?FString::Printf(TEXT("%.1f s"),Remaining):bFull?TEXT("生命已满"):TEXT("就绪");
        Label(Status,X+12,Bottom+77,13,bReady?DemoUI::Accent:DemoUI::Muted);
        if(Remaining>0.f&&Duration>0.f)
        {
            if(bR02) R02HUDLine(X+12,Bottom+100,96,1.f-Remaining/Duration,DemoUI::Accent);
            else Meter(X+12,Bottom+100,96,2,1.f-Remaining/Duration,DemoUI::Accent);
        }
        else if(bR02) R02HUDLine(X+12,Bottom+100,96,0.f,DemoUI::Muted);
    }
    } // 非R02完整保留旧阶段/双币/最大生命/枪械教学与技能排版。
    const auto* PS=Player->GetPlayerState<ADemoPlayerState>(); // 仅本人拥有者计数；队友只公开核心ID。
    const auto* Build=PS?PS->GetRunBuildComponent():nullptr;
    const float CoreX=SkillX+262.f; // 第三个技能位使用真实确认层数，禁止伪造百分比充能。
    if(!bR02) Panel(CoreX,Bottom+16,160,90,Plate,2);
    bool bPrototype=false; FString CoreTitle; // 表现覆盖只在Editor候选实例有效时存在，不修改正式选择或存档。
#if WITH_EDITOR
    const FDemoEditorCrossfireView Trial=DemoEditorCombatPrototype::ReadView(Player); // 同步弱UObject读取器，不加载Editor类/资源、不跨帧持有。
    bPrototype=Trial.bActive;
    if(bPrototype) CoreTitle=TEXT("交叉火力·原型");
#endif
    if(!bPrototype) CoreTitle=Build&&Build->HasSelectedCore()?DemoRunBuild::Title(Build->GetSnapshot().CoreId).ToString():TEXT("未选核心");
    if(!bR02)
    {
        Label(TEXT("RUN CORE"),CoreX+12,Bottom+28,12,DemoUI::Gold);
        Label(CoreTitle,CoreX+12,Bottom+48,bPrototype?16.f:19.f,DemoUI::Ink);
    }
    FString CoreState=bR02?FString():Build&&Build->HasSelectedCore()?TEXT("等待战斗"):TEXT("终端选择"); // R02常态留空；完整核心规则在Tab，不常驻教学或假充能。
    if(bCombat&&Build&&Build->HasSelectedCore())
    {
        const auto& Runtime=Build->GetRuntimeView(); // 服务器事件快照，不本地累加、不写档。
        const float Remaining=FMath::Max(0.f,Runtime.ServerEndTime-State->GetServerWorldTimeSeconds());
        CoreState=Remaining>0.f&&Runtime.ChargedShots>0?FString::Printf(TEXT("强化 %d 发 · %.1fs"),Runtime.ChargedShots,Remaining)
            :Remaining>0.f&&Runtime.PrecisionStacks>0?FString::Printf(TEXT("弱点连击 %d · %.1fs"),Runtime.PrecisionStacks,Remaining):TEXT("战斗中生效");
        const FName Action=Runtime.ActionState; // 服务器确认的扩展状态；本地时钟只显示剩余时间，不授予能力。
        const float Cooldown=FMath::Max(0.f,Runtime.CooldownEndTime-State->GetServerWorldTimeSeconds()); // World游戏秒，与各策略CD相同。
        if(Action==TEXT("Rhythm.Credits")&&Remaining>0.f) CoreState=FString::Printf(TEXT("节律 %d 枪 · %.1fs"),Runtime.ActionStacks,Remaining);
        else if(Action==TEXT("Rhythm.Ready")) CoreState=TEXT("节拍成功 · 完成装填");
        else if(Action==TEXT("Rhythm.Attempted")) CoreState=TEXT("节拍失败 · 正常装填");
        else if(Action==TEXT("Rhythm.Window")) CoreState=Build->GetSnapshot().Branch7==TEXT("B04-D")?TEXT("稳定装填 · 自动判定"):Remaining>0.f?TEXT("装填中 · 按R踩节拍"):TEXT("错过节拍 · 正常装填");
        else if(Action==TEXT("Rhythm.Idle")&&Build->GetSnapshot().Branch7==TEXT("B04-D")) CoreState=FString::Printf(TEXT("弹匣命中 %d / %d"),Runtime.ActionStacks,Runtime.ActionCapacity);
        else if(Action==TEXT("Crossfire.Gun")) CoreState=Remaining>0.f?FString::Printf(TEXT("切枪兑现 · %.1fs"),Remaining):TEXT("近战破势 → 新枪命中");
        else if(Action==TEXT("Crossfire.Melee")) CoreState=Remaining>0.f?FString::Printf(TEXT("近战兑现 · %.1fs"),Remaining):TEXT("枪击破势 → 近战命中");
        else if(Action==TEXT("Crossfire.Idle")) CoreState=Build->GetSnapshot().Branch3==TEXT("B05-B")?TEXT("枪击破势 → 近战命中"):TEXT("近战破势 → 新枪命中");
        else if(Action==TEXT("AnchorCharge")) CoreState=Remaining>0.f?FString::Printf(TEXT("锚点 %d / %d · %.1fs"),Runtime.ActionStacks,Runtime.ActionCapacity,Remaining):TEXT("锚点充能到期");
        else if(Action==TEXT("AnchorLeave")) CoreState=TEXT("锚点充满 · 离场爆破");
        else if(Action==TEXT("AnchorFire")) CoreState=TEXT("锚点充满 · 枪击兑现");
        else if(Action==TEXT("AnchorRelay")) CoreState=Remaining>0.f?FString::Printf(TEXT("G接力 · %.1fs"),Remaining):TEXT("G部署新锚点");
        else if(Action==TEXT("AnchorReady")) CoreState=TEXT("G部署锚点 · 入场充能");
        else if(Action==TEXT("AnchorCooldown")) CoreState=Cooldown>0.f?FString::Printf(TEXT("部署冷却 · %.1fs"),Cooldown):TEXT("G部署锚点");
        else if(Action==TEXT("counter_dash")) CoreState=TEXT("冲刺确认 · 保留预算");
        else if(Action==TEXT("counter_ready")) CoreState=Remaining>0.f?FString::Printf(TEXT("回收≤%d HP · %.1fs"),Runtime.ActionStacks,Remaining):Cooldown>0.f?FString::Printf(TEXT("反击冷却 · %.1fs"),Cooldown):TEXT("等待敌方实际损血");
        else if(Action==TEXT("counter_cooldown")) CoreState=Cooldown>0.f?FString::Printf(TEXT("反击冷却 · %.1fs"),Cooldown):TEXT("等待敌方实际损血");
        else if(Action==TEXT("counter_idle")) CoreState=TEXT("受伤开窗 → 反击回收");
        else if(Action==TEXT("Close.Tight")) CoreState=Remaining>0.f?FString::Printf(TEXT("收束散布 · %.1fs"),Remaining):TEXT("近距直击积攒膛压");
        else if(Action==TEXT("Close.Pressure")) CoreState=Remaining>0.f?FString::Printf(TEXT("膛压 %d / %d · %.1fs"),Runtime.ActionStacks,Runtime.ActionCapacity,Remaining):TEXT("近距直击积攒膛压");
        else if(Action==TEXT("Close.Idle")) CoreState=TEXT("近距多丸 → 满压爆膛");
        else if(Action==TEXT("Marksman.Ready")) CoreState=TEXT("蓄势已满 · 重击就绪");
        else if(Action==TEXT("Marksman.Charging")) CoreState=FString::Printf(TEXT("蓄势 %d%%"),FMath::Clamp(Runtime.ActionStacks*10,0,100));
        if(bR02)
        {
            CoreState.Empty(); // 精简只作用显示；不改上述真实Owner状态、时钟、触发/冷却或分支权限。
            if(Remaining>0.f)
            {
                if(Runtime.ChargedShots>0) CoreState=FString::Printf(TEXT("强化 %d 发 · %.1fs"),Runtime.ChargedShots,Remaining);
                else if(Runtime.PrecisionStacks>0) CoreState=FString::Printf(TEXT("弱点 %d · %.1fs"),Runtime.PrecisionStacks,Remaining);
                else if(Action==TEXT("Rhythm.Credits")) CoreState=FString::Printf(TEXT("节律 %d 枪 · %.1fs"),Runtime.ActionStacks,Remaining);
                else if(Action==TEXT("Crossfire.Gun")) CoreState=FString::Printf(TEXT("切枪兑现 · %.1fs"),Remaining);
                else if(Action==TEXT("Crossfire.Melee")) CoreState=FString::Printf(TEXT("近战兑现 · %.1fs"),Remaining);
                else if(Action==TEXT("AnchorCharge")) CoreState=FString::Printf(TEXT("锚点 %d/%d · %.1fs"),Runtime.ActionStacks,Runtime.ActionCapacity,Remaining);
                else if(Action==TEXT("AnchorRelay")) CoreState=FString::Printf(TEXT("接力 · %.1fs"),Remaining);
                else if(Action==TEXT("counter_ready")) CoreState=FString::Printf(TEXT("反击回收 ≤%d · %.1fs"),Runtime.ActionStacks,Remaining);
                else if(Action==TEXT("Close.Tight")) CoreState=FString::Printf(TEXT("散布收束 · %.1fs"),Remaining);
                else if(Action==TEXT("Close.Pressure")) CoreState=FString::Printf(TEXT("膛压 %d/%d · %.1fs"),Runtime.ActionStacks,Runtime.ActionCapacity,Remaining);
            }
            if(Action==TEXT("AnchorLeave")) CoreState=TEXT("锚点已满 · 离场爆破");
            else if(Action==TEXT("AnchorFire")) CoreState=TEXT("锚点已满 · 枪击兑现"); // 服务器确认的未消费就绪态；不把Idle/冷却说明常驻回HUD。
            else if(Action==TEXT("Marksman.Ready")) CoreState=TEXT("蓄势已满 · 贯杀就绪");
            else if(Action==TEXT("Marksman.Charging")&&Runtime.ActionStacks>0) CoreState=FString::Printf(TEXT("蓄势 %d%%"),FMath::Clamp(Runtime.ActionStacks*10,0,100));
        }
    }
#if WITH_EDITOR
    if(bPrototype)
    {
        CoreState=bR02?(Trial.Remaining>0.f?FString::Printf(TEXT("破势 · %.1fs"),Trial.Remaining):FString())
            :Trial.Remaining>0.f?FString::Printf(TEXT("破势 %.1fs / +%.0f"),Trial.Remaining,Trial.ChargeDamage):TEXT("近战标记 / 枪械加成");
        if(!bR02&&Trial.Remaining>0.f) Label(TEXT("破势目标：")+Trial.TargetName.Left(16),ViewWidth*.5f,ViewHeight*.5f+78,14,DemoUI::Gold,true); // R02仅保短活跃窗口；旧路径仍给目标提示，原枪伤始终正常。
    }
#endif
    if(bR02)
    {
        if(Build&&Build->HasSelectedCore()&&!bPrototype)
        {
            const FDemoRunBuildSnapshot& Snap=Build->GetSnapshot();
            const FDemoRunBuildRuntimeView& Runtime=Build->GetRuntimeView();
            const float ServerTime=State->GetServerWorldTimeSeconds();
            const float Remaining=FMath::Max(0.f,Runtime.ServerEndTime-ServerTime);
            const float Cooldown=FMath::Max(0.f,Runtime.CooldownEndTime-ServerTime);
            const bool bEmpowered=IsCoreEmpoweredNow(Player,State);
            const bool bPulsing=GetWorld()&&GetWorld()->GetTimeSeconds()<AbilityFeedback.CorePulseUntil;
            FString DisplayText;
            FLinearColor CoreColor=R02HUDTheme->Muted;
            float BarFraction=0.f;
            bool bShowBar=false;
            if(bCombat&&!CoreState.IsEmpty())
            {
                DisplayText=CoreTitle+TEXT(" · ")+CoreState;
                CoreColor=(bEmpowered||bPulsing)?R02HUDTheme->Ember:R02HUDTheme->Ink;
                if(Remaining>0.f) { BarFraction=FMath::Clamp(Remaining/4.f,0.f,1.f); bShowBar=true; }
                else if((Runtime.ActionState==TEXT("Marksman.Charging")||Runtime.ActionState==TEXT("Marksman.Ready"))&&Runtime.ActionCapacity>0)
                { BarFraction=FMath::Clamp(static_cast<float>(Runtime.ActionStacks)/static_cast<float>(Runtime.ActionCapacity),0.f,1.f); bShowBar=true; }
            }
            else if(bCombat&&Cooldown>0.f)
            {
                DisplayText=CoreTitle+FString::Printf(TEXT(" · 冷却 %.1fs"),Cooldown);
                CoreColor=R02HUDTheme->Muted;
            }
            else
            {
                DisplayText=CoreTitle;
                if(!Snap.Branch3.IsNone()) DisplayText+=TEXT(" · ")+DemoRunBuild::Title(Snap.Branch3).ToString();
                if(!Snap.Branch7.IsNone()) DisplayText+=TEXT(" · ")+DemoRunBuild::Title(Snap.Branch7).ToString();
                CoreColor=bCombat?R02HUDTheme->Ink.CopyWithNewOpacity(.78f):R02HUDTheme->Muted;
            }
            Label(DisplayText,ViewWidth*.5f,ViewHeight-106.f,bEmpowered?15.f:13.f,CoreColor,true);
            if(bShowBar) R02HUDLine(ViewWidth*.5f-90.f,ViewHeight-86.f,180.f,BarFraction,bEmpowered?R02HUDTheme->Ember:R02HUDTheme->Ink,bEmpowered?2.f:1.f);
            // 绘制层数/充能点（精准弱点连击或战术锚点充能进度）
            int32 PipCount=0,PipTotal=0;
            if(bCombat&&Remaining>0.f&&Runtime.PrecisionStacks>0) { PipCount=Runtime.PrecisionStacks; PipTotal=3; }
            else if(bCombat&&Runtime.ActionCapacity>0&&(Runtime.ActionState==TEXT("AnchorCharge")||Runtime.ActionState==TEXT("Rhythm.Idle")||Runtime.ActionState==TEXT("Close.Pressure"))) { PipCount=Runtime.ActionStacks; PipTotal=FMath::Min(Runtime.ActionCapacity,8); }
            else if(bCombat&&bEmpowered&&Runtime.ChargedShots>0) { PipCount=Runtime.ChargedShots; PipTotal=Runtime.ChargedShots; }
            if(PipTotal>0)
            {
                const float PipSpacing=10.f,StartX=ViewWidth*.5f-(PipTotal-1)*PipSpacing*.5f,PipY=ViewHeight-118.f;
                for(int32 P=0;P<PipTotal;++P)
                {
                    const FLinearColor PipCol=(P<PipCount)?(bEmpowered?R02HUDTheme->Ember:R02HUDTheme->Ink):R02HUDTheme->Muted.CopyWithNewOpacity(.45f);
                    DrawRect(PipCol,FMath::RoundToFloat((StartX+P*PipSpacing-2.f)*UIScale),FMath::RoundToFloat(PipY*UIScale),4.f*UIScale,4.f*UIScale);
                }
            }
        }
        else if(!CoreState.IsEmpty())
        {
            Label(CoreState,ViewWidth*.5f,ViewHeight-112.f,14,DemoUI::Ink,true);
        }
    }
    else
    {
        Label(CoreState,CoreX+12,Bottom+77,12,DemoUI::Accent);
        DrawAbilityProcToasts(Player,Attributes);
    }

    if (ShouldDrawWorldReticle(State,Player))
    {
        const float CenterX=ViewWidth*.5f,CenterY=ViewHeight*.5f; // 与真实视口中心同源，不由武器射击许可/弹药/散布改变。
        const ADemoPlayerController* LocalPC=Cast<ADemoPlayerController>(PlayerOwner); // 本帧同一HUD拥有者，不能借下方终端提示块的局部PC。
        const UDemoCasinoDeviceInputComponent* Input=LocalPC?LocalPC->FindComponentByClass<UDemoCasinoDeviceInputComponent>():nullptr; // 当前PC焦点纯读，无新增Trace/Timer/Widget。
        FLinearColor Color=DemoUI::Ink; // 默认白点，亮背景用暗边；1080p内径约3.75px，沿现UI比例。
        if (Input && Input->IsOperating() && !Input->GetFocusedControlId().IsNone()) Color=Input->IsFocusedControlEnabled()?DemoUI::Accent:DemoUI::Muted;
        if(bR02) DrawR02Reticle(); // 单点由真实物理视口栅格对齐，原ShouldDrawWorldReticle阶段/ADS/观战守卫完整保留。
        else
        {
            Panel(CenterX-2.f,CenterY-2.f,4.f,4.f,FLinearColor(0.f,0.f,0.f,.9f),2.f);
            Panel(CenterX-1.25f,CenterY-1.25f,2.5f,2.5f,Color,1.25f);
        }
        if (Input && Input->IsOperating())
        {
            const FString& FocusPrompt=Input->GetFocusedControlPrompt(); // 固定短文案，未选中时提示入口；金额只显示本人草稿。
            Panel(CenterX-280.f,CenterY+27.f,560.f,38.f,DemoUI::Surface.CopyWithNewOpacity(.92f),2.f);
            Label(FocusPrompt.IsEmpty()?TEXT("左键点击按键 · Esc 离开设备"):FocusPrompt,CenterX,CenterY+32.f,22.f,Color,true);
            const UDemoMenuFlowComponent* Flow=LocalPC->GetMenuFlow(); // 当前Owner本帧借用；退出Operate后本区域自然不画，不持有别人的金额。
            if (Flow)
            {
                const float CardX=ViewWidth-388.f,CardY=FMath::Max(116.f,CenterY-104.f); // 参考HUD坐标，右侧留28px边距；不挡中央数字键/拉杆和底部弹药。
                const FString& Amount=Flow->GetCasinoDeviceAmountText(); // 未确认的编辑稿必须与已确认Stake分开，完整显示int32数字而不缩写。
                const bool bWaiting=Flow->IsGambleWaiting(); // 提交在途不能写“已扣款”或“尚未扣款”，原服务回执仍是唯一支付事实。
                Panel(CardX,CardY,360.f,160.f,DemoUI::Surface.CopyWithNewOpacity(.94f),4.f);
                Label(bWaiting?TEXT("军械老虎机 · 提交中"):TEXT("军械老虎机 · 金额"),CardX+16.f,CardY+12.f,20.f,DemoUI::Ink);
                Label(FString::Printf(TEXT("输入  %s"),Amount.IsEmpty()?TEXT("0"):*Amount),CardX+16.f,CardY+46.f,32.f,DemoUI::Gold);
                Label(FString::Printf(TEXT("%s  %d"),bWaiting?TEXT("提交金额"):TEXT("待下注"),Flow->GetCasinoDraft().Stake),CardX+16.f,CardY+94.f,26.f,DemoUI::Ink);
                Label(bWaiting?TEXT("等待服务器确认结果"):TEXT("SET BET 确认金额 · 拉杆才下注"),CardX+16.f,CardY+133.f,16.f,DemoUI::Ink);
            }
        }
    }
    if(Player->CanUseFirearmActions()&&Equipment)
    {
        const float CenterX=ViewWidth*.5f,CenterY=ViewHeight*.5f; // 准星始终相机中心，原红色斜向命中标记独立绘制。
        const bool bLoadingAmmo=Weapon&&Weapon->IsReloading()&&(Weapon->Config.bReloadOneRoundAtATime || State->GetServerWorldTimeSeconds()<Weapon->GetReloadServerStartTime()+Weapon->GetReloadInsertionSeconds()); // 用同步时钟收起整匣提示；余额仍只读服务器。
        const bool bRhythmReload=bLoadingAmmo&&bCombat&&Build&&Build->GetSnapshot().CoreId==TEXT("B04");
        if(bLoadingAmmo&&ASC&&ASC->HasMatchingGameplayTag(DemoTags::Reloading)&&(!bR02||!bRhythmReload)) Label(TEXT("装填中…"),CenterX,CenterY+44,16,DemoUI::Accent,true);
        if(bRhythmReload)
        {
            const auto& Runtime=Build->GetRuntimeView(); // 成功/失败只看拥有者复制的权威回执，按键本身不提前显示成功。
            const UDemoExpandedBuildSettings* Settings=GetDefault<UDemoExpandedBuildSettings>(); // 显示窗口与服务端配置同源；不使用动画Notify估计节拍。
            const float ServerTime=State->GetServerWorldTimeSeconds(); // 与武器复制ReloadServerStartTime同一时钟。
            const float Duration=Weapon->Config.bReloadOneRoundAtATime ? Weapon->GetReloadDuration() : Weapon->GetReloadInsertionSeconds(); // 与权威节律窗口使用相同分母。
            const bool bStable=Build->GetSnapshot().Branch7==TEXT("B04-D"); // 稳定路线没有主动按窗，避免给出无效R操作提示。
            const bool bWindow=Runtime.ActionState==TEXT("Rhythm.Window")&&!bStable&&Duration>0.f
                &&ServerTime<=Runtime.ServerEndTime+Settings->RhythmToleranceSeconds; // 逐颗装填只有首周期可判，后续周期不重新出现按窗。
            if(bWindow)
            {
                const float Progress=FMath::Clamp((ServerTime-Weapon->GetReloadServerStartTime())/Duration,0.f,1.f); // 本地只读视觉位置，不回传“成功”布尔值。
                const float WindowStart=FMath::Clamp(Settings->RhythmWindowStart,0.f,1.f); // 配置范围钳制仅保护绘制，非法配置仍由服务端拒绝出发。
                const float WindowEnd=FMath::Clamp(Settings->RhythmWindowEnd,WindowStart,1.f); // 色段只表达正式判窗，不把容错范围画成额外奖励。
                const float BarX=CenterX-140.f; // 中央下方独立280px条，不覆盖准星/命中标记。
                if(bR02)
                {
                    R02HUDLine(BarX,CenterY+92,280,0.f,DemoUI::Muted);
                    R02HUDLine(BarX+WindowStart*280.f,CenterY+92,(WindowEnd-WindowStart)*280.f,1.f,DemoUI::Gold,2.f);
                    const float PixelX=FMath::RoundToFloat((BarX+Progress*278.f)*UIScale),PixelY=FMath::RoundToFloat((CenterY+88)*UIScale); // 真实节律指针仅1物理px宽，不改判窗。
                    DrawRect(R02HUDTheme->Ink,PixelX,PixelY,1.f,9.f);
                }
                else
                {
                    Panel(BarX,CenterY+92,280,8,DemoUI::Raised,1);
                    Panel(BarX+WindowStart*280.f,CenterY+92,(WindowEnd-WindowStart)*280.f,8,DemoUI::Gold,0);
                    Panel(BarX+Progress*278.f,CenterY+87,2,18,DemoUI::Ink,0);
                }
                Label(bR02?(Progress>=WindowStart&&Progress<=WindowEnd?TEXT("现在按 R"):TEXT("节律装填"))
                    :(Progress>=WindowStart&&Progress<=WindowEnd?TEXT("现在再次按 R"):TEXT("节律装填 · 等待金色窗口")),CenterX,CenterY+64,15,DemoUI::Gold,true);
            }
            else Label(bR02?(bStable?TEXT("稳定装填"):Runtime.ActionState==TEXT("Rhythm.Ready")?TEXT("节拍已确认"):TEXT("节拍已结束"))
                :(bStable?TEXT("稳定装填 · 完成后自动判定"):Runtime.ActionState==TEXT("Rhythm.Ready")?TEXT("节拍已确认 · 等待装填完成"):TEXT("本次节拍已结束 · 正常装填")),CenterX,CenterY+69,14,DemoUI::Muted,true);
        }
    }
    if(bCombat)
    {
        if(bR02) DrawR02BossHeader(State,R02Boss); // C07只替换Boss区，不改变下面任何技能/ADS/核心许可。
        else
        {
        const float CenterX=ViewWidth*.5f; // Boss窄幅区域位于目标与钱包之间，最多绘制一个活Boss。
        for(TActorIterator<ADemoEnemy> It(GetWorld());It;++It)
        {
            if(!It->IsBoss()||!It->IsAlive()) continue;
            const auto* Roster=It->FindComponentByClass<UDemoEnemyRoster>(); // 公开身份来自既有复制，不假定总为典狱长。
            Label(Roster&&!Roster->GetLabel().IsEmpty()?Roster->GetLabel():TEXT("BOSS / 首领"),CenterX,bR02?26:37,bR02?28:20,DemoUI::Ink,true);
            if(bR02) R02HUDLine(CenterX-230,72,460,DemoHUDReadout::Fraction(It->GetHealth(),It->GetMaxHealth()),DemoUI::Gold,R02HUDTheme->BossLinePhysicalPixels);
            else Meter(CenterX-230,72,460,4,DemoHUDReadout::Fraction(It->GetHealth(),It->GetMaxHealth()),DemoUI::Gold);
            Label(DemoHUDReadout::Number(It->GetHealth())+TEXT(" / ")+DemoHUDReadout::Number(It->GetMaxHealth()),CenterX,86,13,DemoUI::Gold,true);
            break;
        }
        }
    }
    else if(bPreparation)
    {
        const auto* PC=Cast<ADemoPlayerController>(PlayerOwner); // 交互提示仅真实附近终端，不改变查询/距离/授权。
        if(PC&&!Player->IsCoopEliminated()&&!PC->IsUpgradeMenuOpen()&&!PC->IsNextLevelConfirmationOpen()&&Equipment&&Equipment->GetScopeLevel()==0)
            if(const auto* Target=Player->FindInteractable())
            {
                if(!bR02) Panel(ViewWidth*.5f-180,Bottom-64,360,34,Plate,2);
                Label(Target->GetPrompt(),ViewWidth*.5f,Bottom-55,14,DemoUI::Ink,true);
            }
    }
}
