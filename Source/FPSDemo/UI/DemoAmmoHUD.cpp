#include "UI/DemoHUD.h"
#include "Player/DemoPlayerController.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Game/DemoGameState.h"
#include "Engine/Texture2D.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Debug/DemoLog.h"

void ADemoHUD::DrawAmmoMenu(ADemoPlayerController* PC,ADemoCharacter* Player)
{
    DEMO_LOG_TICK();
    const UDemoAmmoComponent* Ammo=Player->FindComponentByClass<UDemoAmmoComponent>(); // 当前Avatar目录/状态，仅本帧借用。
    const UDemoAmmoCatalog* Data=Ammo->GetCatalog(); // 资产已随组件加载，绘制不做磁盘IO。
    if(Data->Entries.Num()!=4){Label(TEXT("弹药目录配置无效"),ViewWidth/2,ViewHeight/2,20,FLinearColor::Red,true);return;}
    const auto* State=GetWorld()->GetGameState<ADemoGameState>(); // 钱包只读，交易由组件处理。
    const float X=ViewWidth/2-480,Y=ViewHeight/2-240; // 与武器/属性页一致的居中960×480锚点；切页不改变顶部按钮的屏幕坐标。
    const FLinearColor Surface(FColor(24,38,45)),Raised(FColor(34,52,62)),Ink(FColor(230,240,243)),Muted(FColor(154,178,188)),Accent(FColor(96,219,204)),Gold(FColor(248,208,118)); // 与现有终端一致。
    const int32 Selected=PC->GetInspectedAmmo(); // 查看项，不等于已装配。
    const FDemoAmmoEntry& Entry=Data->Entries[Selected]; // 已验证固定索引。
    const bool Confirm=PC->GetPendingAmmoPrice()!=INDEX_NONE; // 报价期间禁用底层按钮。
    // 面板、页签及关闭热区与DrawWeaponMenu逐项对齐；仅当前页高亮变化，确认弹窗仍禁用底层热区。
    Panel(X,Y,960,480,Surface,12);
    Label(TEXT("FIELD SYSTEMS / 备战终端"),X+32,Y+25,13,Accent);
    Button(TEXT("Close"),TEXT("×  关闭"),X+822,Y+25,106,36,!Confirm,true);
    Button(TEXT("TerminalStats"),TEXT("属性升级"),X+430,Y+25,120,36,!Confirm,true);
    Button(TEXT("TerminalWeapons"),TEXT("武器"),X+560,Y+25,100,36,!Confirm,true);
    Button(TEXT("TerminalAmmo"),TEXT("弹药类型"),X+670,Y+25,130,36,!Confirm);
    Label(TEXT("弹药类型"),X+32,Y+53,30,Ink);
    // 钱包与已装配信息放到内容说明行，避免占用右上固定页签位置。
    Label(FString::Printf(TEXT("金币 %d"),State->Coins),X+32,Y+98,15,Gold);
    Label(FString::Printf(TEXT("当前装配：%s"),*Data->Entries[Ammo->GetSelected()].Name.ToString()),X+700,Y+98,15,Accent);
    for(int32 Index=0;Index<4;++Index) // 四项名称左侧使用用户现成图标，保持宽高比。
    {
        const FDemoAmmoEntry& Item=Data->Entries[Index]; // 当前目录项及其独立价格。
        const float Row=Y+137+Index*66; // 480高面板内四行，每项60高、6间距，末行结束于Y+395。
        Panel(X+28,Row,282,60,Index==Selected?FLinearColor(FColor(35,70,73)):Raised);
        if(Item.Icon)
        {
            FCanvasTileItem Tile(FVector2D(X+38,Row+6)*UIScale,Item.Icon->GetResource(),FVector2D(48,48)*UIScale,FLinearColor::White); // 48px图标在60px行内垂直居中，保持原图Alpha。
            Tile.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Tile);
        }
        else Label(TEXT("?"),X+55,Row+18,22,Muted); // 缺图回退仍可操作。
        Label(Item.Name.ToString(),X+99,Row+9,19,Ink);
        Label(Ammo->GetSelected()==Index?TEXT("已装配"):Ammo->IsUnlocked(Index)?TEXT("已解锁 · 可装配"):FString::Printf(TEXT("未解锁 · %d 金币"),Item.UnlockGoldCost),X+99,Row+37,12,Accent);
        if(!Confirm)AddHitBox(FVector2D(X+28,Row)*UIScale,FVector2D(282,60)*UIScale,FName(*FString::Printf(TEXT("Ammo%d"),Index)),true); // 命中范围随新行高同步，间隙不可点击。
    }
    // 详情区与列表上下对齐；压缩内部留白，保留完整说明与交易按钮。
    Panel(X+332,Y+137,600,258,Raised);
    Label(Entry.Name.ToString(),X+354,Y+153,25,Ink);
    const TCHAR* Descriptions[]={TEXT("原始武器伤害，无附加状态。"),TEXT("命中叠加灼烧，满层爆炸并清空层数。"),TEXT("逐层减速，满层冻结；冻结只限制移动。"),TEXT("提高直接伤害，沿原轨迹额外穿透一个敌人。")}; // 固定玩法语义，数值读取配置。
    Label(Descriptions[Selected],X+354,Y+195,16,Muted);
    TArray<FString> Lines; // 本帧详情行，全部从真实配置派生。
    if(Selected==0)Lines={TEXT("已默认解锁"),TEXT("原射速、弹匣和装填规则保持不变")};
    if(Selected==1)Lines={FString::Printf(TEXT("每层每 %.1f 秒灼烧 %.1f HP · 持续 %.1f 秒"),Data->BurnPeriod,Data->BurnDamagePerStack,Data->BurnDuration),FString::Printf(TEXT("%d 层触发单体爆炸 · 额外 %.1f 伤害"),Data->BurnThreshold,Data->ExplosionDamage),TEXT("同一枪对同一敌人只叠一层（含散弹）")};
    if(Selected==2)Lines={FString::Printf(TEXT("每层减速 %.0f%% · 上限 %.0f%% · 持续 %.1f 秒"),100*Data->SlowPerStack,100*Data->MaxSlow,Data->ChillDuration),FString::Printf(TEXT("%d 层冻结 %.1f 秒 · 解冻后免疫 %.1f 秒"),Data->FreezeThreshold,Data->FreezeDuration,Data->PostThawImmunityDuration),TEXT("免疫期间仍受到子弹直接伤害")};
    if(Selected==3)Lines={FString::Printf(TEXT("首个敌人 %.0f%% 伤害 · 后方敌人承受其 %.0f%%"),100*Data->PiercingDamageMultiplier,100*Data->SecondaryDamageRatio),TEXT("额外命中 1 个敌人 · 无法穿墙"),TEXT("散弹每颗弹丸独立检测穿透")};
    for(int32 Index=0;Index<Lines.Num();++Index)Label(Lines[Index],X+354,Y+231+Index*30,15,Ink); // 三行详情在操作区上方结束，不与缩短后的面板底部重叠。
    const bool Owned=Ammo->IsUnlocked(Selected); // 权限以当前槽解锁为准。
    const FString Reason=Ammo->GetBlockReason(); // 与点击提交共用权限。
    Label(Owned?TEXT("已解锁 · 免费装配"):TEXT("一次购买 · 当前存档永久解锁"),X+354,Y+347,13,Muted);
    Button(TEXT("AmmoAction"),Owned?(Ammo->GetSelected()==Selected?TEXT("已装配"):TEXT("装配此弹药")):State->Coins<Entry.UnlockGoldCost?FString::Printf(TEXT("还差 %d 金币"),Entry.UnlockGoldCost-State->Coins):FString::Printf(TEXT("%d 金币解锁"),Entry.UnlockGoldCost),X+696,Y+338,214,48,!Confirm&&Reason.IsEmpty()&&(Owned?Selected!=Ammo->GetSelected():State->Coins>=Entry.UnlockGoldCost));
    Label(PC->GetMenuMessage().IsEmpty()?Reason:PC->GetMenuMessage(),X+28,Y+415,14,Accent);
    Label(TEXT("主副武器通用 · 仅安全区更换 · 同时生效一种"),X+28,Y+449,13,Muted);
    if(!Confirm)return;
    Panel(0,0,ViewWidth,ViewHeight,FLinearColor(0,0,0,.75f),0);
    // 新面板锚点下弹窗同步上移30设计像素，保持其原有屏幕居中位置和按钮间距。
    Panel(X+190,Y+105,580,265,Surface);
    Label(FString::Printf(TEXT("确认解锁%s？"),*Entry.Name.ToString()),X+222,Y+132,25,Ink);
    Label(FString::Printf(TEXT("花费 %d 金币 · 余额 %d → %d"),PC->GetPendingAmmoPrice(),State->Coins,State->Coins-PC->GetPendingAmmoPrice()),X+222,Y+184,18,Gold);
    Label(TEXT("永久解锁后可免费装配，不会自动替换当前弹药"),X+222,Y+224,14,Muted);
    Button(TEXT("AmmoBuyYes"),TEXT("确认解锁"),X+222,Y+292,230,48);
    Button(TEXT("AmmoBuyNo"),TEXT("取消"),X+478,Y+292,260,48,true,true);
}
