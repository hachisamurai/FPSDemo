// 职责：R04个人构筑只读页：主区实时GAS/永久账本/装备，右侧首屏核心及独立分页天赋。使用：每次借用当前Avatar，不执行能力/交易、不反推成长来源。
#include "UI/Screens/DemoPlayerStatusScreen.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIStyle.h"
#include "UI/R02/DemoUITheme.h"
#include "UI/Common/DemoMenuButton.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Weapons/Melee/DemoMeleeComponent.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Game/DemoGameState.h" // 正式战役才需要选择核心；无尽/旧模式不冒称缺少必选项。
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h" // 固定页脚左滚动提示/右分页组，不让提示挤压分页动作。
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Debug/DemoLog.h"
bool UDemoPlayerStatusScreen::UsesR02Presentation() const
{
    DEMO_LOG_TICK();
    return true;
}
FString UDemoPlayerStatusScreen::RefreshKey() const
{
    DEMO_LOG_TICK(); FString Key=FString::FromInt(Flow()->GetStatusPage()); // 天赋变化才重建分页控件。
    for(const auto& Talent:Controller()->GetMenuSnapshot().OwnedTalentViews) Key+=TEXT("|")+Talent.Title+TEXT("|")+Talent.Description;
    const auto& View=Controller()->GetMenuSnapshot(); // 页头钱包与核心静态选择变化才重建；HP/冷却仍更新既有TextBlock。
    Key+=FString::Printf(TEXT("|%d:%d:%d"),View.Progress.Coins,View.Progress.SilverCoins,View.Build.Revision);
    return Key;
}
void UDemoPlayerStatusScreen::BuildBody()
{
    DEMO_LOG_CALL();
    ConfigureLayout(1600.f,835.f);
    SetHeading(TEXT("当前构筑"),GetWorld()->GetNetMode()==NM_Standalone?TEXT("游戏已暂停 · Tab / Esc 返回"):TEXT("联机战斗继续 · 此页仅查看，不释放技能"));
    AddBack(TEXT("OverlayBack"),TEXT("← 返回游戏 [Tab]"));
    UHorizontalBox* Columns=AddRow(PageBody); UVerticalBox* Stats=AddCard(Columns); UVerticalBox* Equipment=AddCard(Columns); UVerticalBox* Talents=ShowAside(); // 主区属性/装备与右侧核心/天赋各自滚动，核心先于全部天赋，分页动作仍在固定页脚。
    const UDemoUITheme* Theme=R02Theme()?R02Theme():GetDefault<UDemoUITheme>();
    AddText(Stats,TEXT("VITALS / CURRENT ATTRIBUTES"),14,Theme->Muted);
    AddText(Stats,TEXT("生命状态"),23,Theme->Ink)->SetFont(Theme->GetSerifFont(23,TEXT("Regular"),true));
    AddR02Rule(Stats,8.f);
    HealthText=AddText(Stats,TEXT(""),48,Theme->Ink); HealthText->SetFont(Theme->GetSerifFont(48,TEXT("Regular"),false));
    AttributesText=AddText(Stats,TEXT(""),18,Theme->Muted); AttributesText->SetAutoWrapText(false); AttributesText->SetWrapTextAt(470.f);
    AddGap(Stats,8.f); AddText(Stats,TEXT("PERMANENT GROWTH"),14,Theme->Muted);
    AddText(Stats,TEXT("永久成长账本"),21,Theme->Ink)->SetFont(Theme->GetSerifFont(21,TEXT("Regular"),true));
    AddR02Rule(Stats,6.f);
    GrowthText=AddText(Stats,TEXT(""),17,Theme->Muted); GrowthText->SetAutoWrapText(false); GrowthText->SetWrapTextAt(470.f);
    AddText(Equipment,TEXT("LOADOUT / ABILITIES"),14,Theme->Muted);
    AddText(Equipment,TEXT("技能与装备"),23,Theme->Ink)->SetFont(Theme->GetSerifFont(23,TEXT("Regular"),true));
    AddR02Rule(Equipment,8.f);
    SkillsText=AddText(Equipment,TEXT(""),18,Theme->Ink); EquipmentText=AddText(Equipment,TEXT(""),17,Theme->Muted);
    SkillsText->SetAutoWrapText(false); SkillsText->SetWrapTextAt(470.f); EquipmentText->SetAutoWrapText(false); EquipmentText->SetWrapTextAt(470.f);
    AddText(Talents,TEXT("RUN CORE / BRANCHES"),14,Theme->Muted);
    AddR02Rule(Talents,6.f);
    CoreText=AddText(Talents,TEXT(""),17,Theme->Muted); // 沿用AddText为固定侧栏设置的240设计单位显式换行，不能沿用主区470宽度而越过侧栏。
    AddGap(Talents,14.f); // 核心与天赋同属右侧独立滚动区，视觉分组不截断任何权威描述。
    const auto& Owned=Controller()->GetMenuSnapshot().OwnedTalentViews; const int32 Page=Flow()->GetStatusPage(); // 权威生成描述，不在客户端估算等级收益。
    AddText(Talents,TEXT("FIELD TALENTS"),14,Theme->Muted);
    AddText(Talents,FString::Printf(TEXT("本轮天赋 / %d种"),Owned.Num()),22,Theme->Ink)->SetFont(Theme->GetSerifFont(22,TEXT("Regular"),true));
    AddR02Rule(Talents,6.f);
    if(Owned.IsEmpty()) AddText(Talents,TEXT("尚未获得天赋。关间终端可使用银币抽取；重复获得会提升等级。"),17,DemoUI::Muted);
    for(int32 Row=0;Row<5&&Owned.IsValidIndex(Page*5+Row);++Row)
    {
        const auto& Talent=Owned[Page*5+Row]; // 完整服务端描述，不在UI重算等级公式或裁掉后半段。
        AddText(Talents,Talent.Title,19,DemoUI::Ink); AddText(Talents,Talent.Description,17,DemoUI::Muted); AddGap(Talents,10.f);
    }
    UHorizontalBox* Pages=PageFooter?PageFooter.Get():AddRow(PageBody); // 即使五条长天赋撑高侧栏，翻页仍可见/可聚焦。
    if(PageFooter) Pages->ClearChildren(); // 移除基类默认的占满留白，固定页脚明确分成左提示与右分页组。
    UTextBlock* ScrollHint=AddText(Pages,TEXT("滚轮查看 · 主区与右侧分别滚动"),15,DemoUI::Muted); // 提示不随正文消失，也不冒充不存在的全页统一滚动。
    ScrollHint->SetAutoWrapText(false); ScrollHint->SetWrapTextAt(0.f);
    UHorizontalBox* Pagination=AddRow(Pages); // 右侧原三项分页为自动宽度整体，不因左提示内容改变按钮位置。
    if(auto* PaginationSlot=Cast<UHorizontalBoxSlot>(Pagination->Slot)) { PaginationSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); PaginationSlot->SetVerticalAlignment(VAlign_Center); }
    AddCompactAction(Pagination,TEXT("StatusPrevious"),TEXT("上一页"),Page>0,100.f);
    UTextBlock* PageNumber=AddText(Pagination,FString::Printf(TEXT("天赋页 %d / %d"),Page+1,FMath::Max(1,(Owned.Num()+4)/5)),17,DemoUI::Muted); // 页码是不可拆分读数，自动宽度按完整字符串分配，不在720p把分母挤到下一行。
    PageNumber->SetJustification(ETextJustify::Center); PageNumber->SetAutoWrapText(false); PageNumber->SetWrapTextAt(0.f);
    AddCompactAction(Pagination,TEXT("StatusNext"),TEXT("下一页"),(Page+1)*5<Owned.Num(),100.f);
    AddText(PageBody,TEXT("永久成长与解锁保留；银币成长、免费奖励和本轮天赋在挑战结束时清除。"),17,DemoUI::Muted);
    UpdateValues();
}
void UDemoPlayerStatusScreen::AnimateScreen(float DeltaTime)
{
    DEMO_LOG_TICK(); if(FPlatformTime::Seconds()<NextValueRefresh) return;
    NextValueRefresh=FPlatformTime::Seconds()+.1; UpdateValues();
}
void UDemoPlayerStatusScreen::UpdateValues()
{
    DEMO_LOG_TICK(); if(!HealthText||!AttributesText||!GrowthText||!SkillsText||!EquipmentText||!CoreText) return;
    const auto* Pawn=Cast<ADemoCharacter>(Controller()->GetPawn()); const auto* Attr=Pawn?Pawn->GetDemoAttributes():nullptr; // 每次读取当前Avatar，旅行不持有旧指针。
    if(!Pawn||!Attr) { HealthText->SetText(FText::FromString(TEXT("--"))); AttributesText->SetText(FText::FromString(TEXT("正在等待玩家数据…"))); GrowthText->SetText(FText::GetEmpty()); SkillsText->SetText(FText::GetEmpty()); EquipmentText->SetText(FText::GetEmpty()); CoreText->SetText(FText::GetEmpty()); return; } // 旧Avatar销毁时清掉旧数字，不假报0HP死亡。
    const auto& View=Controller()->GetMenuSnapshot(); const auto& Growth=View.Progress.UpgradeProgress; // 个人钱包/来源账本。
    HealthText->SetText(FText::FromString(FString::Printf(TEXT("%.0f / %.0f"),Attr->GetHealth(),Attr->GetMaxHealth())));
    const FString HealthValue=HealthText->GetText().ToString(); // 五位或更长生命值仍在本列阅读预算内，不覆盖相邻装备列。
    const UDemoUITheme* Theme=R02Theme()?R02Theme():GetDefault<UDemoUITheme>();
    const int32 HealthFontSize=FMath::Clamp(FMath::FloorToInt(470.f/FMath::Max(1.f,HealthValue.Len()*.64f)),18,48);
    HealthText->SetFont(Theme->GetSerifFont(HealthFontSize,TEXT("Regular"),false));
    HealthText->SetColorAndOpacity(Attr->GetHealth()<=Attr->GetMaxHealth()*.25f?Theme->Ember:Theme->Ink);
    AttributesText->SetText(FText::FromString(FString::Printf(TEXT("当前伤害加成  +%.1f\n当前弹匣加成  +%.0f\n射速倍率  ×%.2f\n移动速度  %.0f cm/s\n团队累计击杀  %d"),Attr->GetWeaponDamageBonus(),Attr->GetMagazineBonus(),Attr->GetFireRateMultiplier(),Pawn->GetCharacterMovement()->MaxWalkSpeed,View.Progress.Kills)));
    GrowthText->SetText(FText::FromString(FString::Printf(TEXT("生命上限  +%.0f\n武器伤害  +%.1f\n弹匣容量  +%.0f\n\n金币购买 %d 次 · 本轮银币购买 %d 次\n当前数值已包含本轮和临时效果。"),Growth.PermanentHealth,Growth.PermanentDamage,Growth.PermanentMagazine,Growth.Total(true),Growth.Total(false)))); // 永久金额直接取账本；不从GAS合计倒推出来源。
    FString Skills; const auto* ASC=Pawn->GetDemoASC(); // 真实GAS剩余游戏秒，单机暂停时不流逝。
    for(int32 Index=0;Index<2;++Index)
    {
        const float Remaining=ASC?ASC->GetCooldownRemaining(Index==0?DemoTags::DashCooldown:DemoTags::HealCooldown):0.f; // 冷却值只读。
        Skills+=(Index==0?TEXT("冲刺 [Shift] · "):TEXT("治疗 [Q] · "))+(Remaining>0?FString::Printf(TEXT("冷却 %.1fs"),Remaining):Index==1&&Attr->GetHealth()>=Attr->GetMaxHealth()?FString(TEXT("生命已满")):FString(TEXT("已就绪")));
        Skills+=Index==0?FString::Printf(TEXT("\n速度 %.0f cm/s\n\n"),Pawn->GetDashSpeed()):FString::Printf(TEXT("\n恢复 %.1f HP\n"),Pawn->GetHealAmount());
    }
    SkillsText->SetText(FText::FromString(Skills));
    FString Equipment; const auto* Weapons=Pawn->GetWeaponComponent(); const auto* Weapon=Weapons?Weapons->GetActiveWeapon():nullptr; // 当前武器实际伤害/弹片/间隔，缺组件安全等待。
    const auto* Melee=Pawn->FindComponentByClass<UDemoMeleeComponent>(); // 独立槽优先说明实际手持，不把隐藏枪叫作近战弹药。
    if(Melee&&Melee->IsEquipped()&&Melee->GetDefinition()) Equipment=TEXT("当前手持：")+Melee->GetDefinition()->DisplayName.ToString()+TEXT("\n枪械库存：\n");
    if(Weapon) Equipment+=FString::Printf(TEXT("%s\n伤害 %.1f × %d · 射速 %.0f RPM\n弹匣 %d/%d · 备用 %s\n"),*Weapon->Config.DisplayName.ToString(),Weapon->GetDamagePerPellet(),Weapon->GetProjectilesPerShot(),60.f/FMath::Max(.001f,Weapon->GetFireInterval()),Weapon->GetAmmo(),Weapon->GetCapacity(),Weapon->Config.bInfiniteReserve?TEXT("∞"):*FString::FromInt(Weapon->GetReserveAmmo()));
    const auto* Ammo=Pawn->FindComponentByClass<UDemoAmmoComponent>(); // 只借用已加载弹药目录。
    if(Ammo&&Ammo->GetCatalog()&&Ammo->GetCatalog()->Entries.IsValidIndex(Ammo->GetSelected())) Equipment+=Ammo->GetCatalog()->Entries[Ammo->GetSelected()].Name.ToString()+TEXT("\n");
    Equipment+=TEXT("遗物：")+(View.Progress.Gambling.EquippedRelic.IsNone()?TEXT("未装配"):DemoGamble::Name(View.Progress.Gambling.EquippedRelic));
    EquipmentText->SetText(FText::FromString(Equipment));
    const auto& Build=View.Build; // 原正式选择值，不读取队友私有分支或UI自行推导。
    const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 公开冻结内容身份，不按所在物理房间猜测规则。
    const bool RequiresCore=State&&!State->bEndless&&State->CampaignPlan.CampaignId==TEXT("steam_demo")&&State->CampaignPlan.ContentRevision>0;
    const FString Core=Build.CoreId.IsNone()?(RequiresCore?TEXT("未选择核心\n出发前在终端选定"):State?TEXT("当前模式无需核心"):TEXT("核心等待同步")):DemoRunBuild::Title(Build.CoreId).ToString(); // 空状态分成两个完整短句，固定侧栏/DPI舍入不会产生孤字行。
    CoreText->SetText(FText::FromString(Core+TEXT("\n03 / ")+(Build.Branch3.IsNone()?TEXT("尚未选择"):DemoRunBuild::Title(Build.Branch3).ToString())+TEXT("\n07 / ")+(Build.Branch7.IsNone()?TEXT("尚未选择"):DemoRunBuild::Title(Build.Branch7).ToString())));
}
