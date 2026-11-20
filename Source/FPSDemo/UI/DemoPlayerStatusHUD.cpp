#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Debug/DemoLog.h"

void ADemoHUD::DrawPlayerStatus(ADemoPlayerController* PC)
{
    DEMO_LOG_TICK();
    const float X=ViewWidth*.5f-560.f,Y=ViewHeight*.5f-288.f; // 1120×576设计坐标，沿用HUD分辨率缩放。
    const ADemoCharacter* Pawn=Cast<ADemoCharacter>(PC->GetPawn()); // 借用当前Avatar，不跨帧持有。
    const UDemoAttributeSet* Attr=Pawn?Pawn->GetDemoAttributes():nullptr; // 生命/合计属性来自GAS实时值。
    const auto& View=PC->GetMenuSnapshot(); // 个人钱包、来源账本与权威天赋描述。
    const auto& Growth=View.Progress.UpgradeProgress; // 永久来源单独展示，合计减永久得到本轮增益。
    Panel(0,0,ViewWidth,ViewHeight,DemoUI::Overlay,0);
    Panel(X,Y,1120,576,GetPanelSurface(),12);
    Label(TEXT("PLAYER STATUS / 玩家状态"),X+28,Y+22,14,DemoUI::Accent);
    Label(TEXT("当前构筑"),X+28,Y+49,30,DemoUI::Ink);
    Label(GetNetMode()==NM_Standalone?TEXT("单人游戏已暂停 · Tab / Esc 关闭"):TEXT("联机战斗继续进行 · Tab / Esc 关闭"),X+265,Y+61,15,DemoUI::Muted);
    Button(TEXT("OverlayBack"),TEXT("关闭 [Tab]"),X+930,Y+28,162,40,true,true);
    if(!Pawn||!Attr) { Label(TEXT("正在等待玩家数据…"),X+28,Y+130,20,DemoUI::Muted); return; }
    Panel(X+28,Y+106,310,426,DemoUI::Raised);
    Panel(X+352,Y+106,310,426,DemoUI::Raised);
    Panel(X+676,Y+106,416,426,DemoUI::Raised);
    Label(TEXT("属性与成长"),X+44,Y+122,20,DemoUI::Accent);
    // 值按实际GAS/钱包读取；治疗与弹药不误用永久成长数值。
    const TArray<FString> Stats={
        FString::Printf(TEXT("生命       %.0f / %.0f"),Attr->GetHealth(),Attr->GetMaxHealth()),
        FString::Printf(TEXT("伤害加成   +%.1f"),Attr->GetWeaponDamageBonus()),
        FString::Printf(TEXT("弹匣加成   +%.0f"),Attr->GetMagazineBonus()),
        FString::Printf(TEXT("射速倍率   ×%.2f"),Attr->GetFireRateMultiplier()),
        FString::Printf(TEXT("移动速度   %.0f cm/s"),Pawn->GetCharacterMovement()->MaxWalkSpeed),
        FString::Printf(TEXT("金币 %d    银币 %d"),View.Progress.Coins,View.Progress.SilverCoins),
        FString::Printf(TEXT("累计击杀   %d"),View.Progress.Kills)}; // 本帧纯文字，不写回任何属性。
    for(int32 Index=0;Index<Stats.Num();++Index) Label(Stats[Index],X+44,Y+159+Index*30,16,DemoUI::Ink); // 固定七行，避免遍历无界历史。
    Label(TEXT("永久 / 本轮增益"),X+44,Y+379,15,DemoUI::Gold);
    Label(FString::Printf(TEXT("生命 +%.0f / +%.0f"),Growth.PermanentHealth,FMath::Max(0.f,Attr->GetMaxHealth()-100.f-Growth.PermanentHealth)),X+44,Y+409,14,DemoUI::Muted);
    Label(FString::Printf(TEXT("伤害 +%.1f / +%.1f"),Growth.PermanentDamage,FMath::Max(0.f,Attr->GetWeaponDamageBonus()-Growth.PermanentDamage)),X+44,Y+436,14,DemoUI::Muted);
    Label(FString::Printf(TEXT("弹匣 +%.0f / +%.0f"),Growth.PermanentMagazine,FMath::Max(0.f,Attr->GetMagazineBonus()-Growth.PermanentMagazine)),X+44,Y+463,14,DemoUI::Muted);
    Label(TEXT("技能与装备"),X+368,Y+122,20,DemoUI::Accent);
    const UDemoAbilitySystemComponent* ASC=Pawn->GetDemoASC(); // 复制GAS查询真实剩余秒，面板不触发能力。
    for(int32 Index=0;Index<2;++Index) // 0冲刺、1治疗，冷却与现有HUD共用Tag。
    {
        const float Remaining=ASC?ASC->GetCooldownRemaining(Index==0?DemoTags::DashCooldown:DemoTags::HealCooldown):0.f; // 游戏秒，单人暂停不流逝。
        const float RowY=Y+162+Index*77; // 两行技能各留出效果说明。
        Label(Index==0?TEXT("冲刺 [Shift]"):TEXT("治疗 [Q]"),X+368,RowY,17,DemoUI::Ink);
        Label(Remaining>0?FString::Printf(TEXT("冷却 %.1fs"),Remaining):Index==1&&Attr->GetHealth()>=Attr->GetMaxHealth()?TEXT("生命已满"):TEXT("已就绪"),X+542,RowY+2,13,DemoUI::Gold);
        Label(Index==0?FString::Printf(TEXT("冲刺速度 %.0f cm/s"),Pawn->GetDashSpeed()):FString::Printf(TEXT("恢复 %.1f HP"),Pawn->GetHealAmount()),X+368,RowY+29,14,DemoUI::Muted);
    }
    const ADemoWeaponBase* Weapon=Pawn->GetWeaponComponent()->GetActiveWeapon(); // 当前装备实例，切枪/重生后不复用旧指针。
    Label(TEXT("当前武器 / 弹药"),X+368,Y+324,15,DemoUI::Gold);
    if(Weapon)
    {
        Label(Weapon->Config.DisplayName.ToString(),X+368,Y+353,18,DemoUI::Ink);
        Label(FString::Printf(TEXT("伤害 %.1f × %d · 射速 %.0f"),Weapon->GetDamagePerPellet(),Weapon->GetProjectilesPerShot(),60.f/Weapon->GetFireInterval()),X+368,Y+384,14,DemoUI::Muted);
        Label(FString::Printf(TEXT("弹匣 %d/%d · 备用 %s"),Weapon->GetAmmo(),Weapon->GetCapacity(),Weapon->Config.bInfiniteReserve?TEXT("∞"):*FString::FromInt(Weapon->GetReserveAmmo())),X+368,Y+411,14,DemoUI::Muted);
    }
    const UDemoAmmoComponent* Ammo=Pawn->FindComponentByClass<UDemoAmmoComponent>(); // 已加载目录只读，绘制不读盘。
    if(Ammo&&Ammo->GetCatalog()&&Ammo->GetCatalog()->Entries.IsValidIndex(Ammo->GetSelected()))
        Label(Ammo->GetCatalog()->Entries[Ammo->GetSelected()].Name.ToString(),X+368,Y+440,15,DemoUI::Accent);
    Label(TEXT("遗物：")+(View.Progress.Gambling.EquippedRelic.IsNone()?TEXT("未装配"):DemoGamble::Name(View.Progress.Gambling.EquippedRelic)),X+368,Y+481,15,DemoUI::Muted);
    const int32 Page=PC->GetMenuFlow()->GetStatusPage(),Count=View.OwnedTalentViews.Num(); // 本地每页5项，服务器增减天赋时自动钳制。
    Label(FString::Printf(TEXT("本轮天赋 · %d 种"),Count),X+692,Y+122,20,DemoUI::Accent);
    if(Count==0) DrawGambleText(TEXT("尚未获得天赋。清关后可在终端使用银币抽取；重复抽中会提升等级。"),X+692,Y+171,4);
    for(int32 Row=0;Row<5&&View.OwnedTalentViews.IsValidIndex(Page*5+Row);++Row) // 分页覆盖全部拥有项，不因排版限制天赋容量。
    {
        const auto& Talent=View.OwnedTalentViews[Page*5+Row]; // 服务器生成的当前等级描述，不使用客户端配置估算。
        const float RowY=Y+162+Row*64.f; // 两行说明+标题，距下方分页按钮保留间隔。
        Label(Talent.Title,X+692,RowY,16,DemoUI::Ink);
        DrawGambleText(Talent.Description,X+692,RowY+23,2);
    }
    Button(TEXT("StatusPrevious"),TEXT("上一页"),X+692,Y+491,112,28,Page>0,true);
    Label(FString::Printf(TEXT("%d / %d"),Page+1,FMath::Max(1,(Count+4)/5)),X+882,Y+495,14,DemoUI::Muted,true);
    Button(TEXT("StatusNext"),TEXT("下一页"),X+964,Y+491,112,28,(Page+1)*5<Count,true);
    Label(TEXT("重复天赋提升等级 · 本轮天赋在死亡或挑战结束时清除 · 永久成长与解锁保留"),X+28,Y+548,13,DemoUI::Muted);
}
