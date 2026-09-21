#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Debug/DemoLog.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

UDemoAmmoCatalog::UDemoAmmoCatalog()
{
    DEMO_LOG_CALL();
    const TCHAR* Names[] = {TEXT("普通弹"),TEXT("火焰弹"),TEXT("冰冻弹"),TEXT("穿透弹")}; // 固定目录顺序的初始本地化文本。
    const TCHAR* Icons[] = {TEXT("Normal"),TEXT("Fire"),TEXT("Ice"),TEXT("Piercing")}; // 复用已导入图标，不生成替代素材。
    for (int32 Index=0; Index<4; ++Index) // CDO构造阶段允许资产引用收集。
    {
        FDemoAmmoEntry Entry; // 只在构造时写入默认目录项。
        Entry.Id=IdAt(Index); Entry.Name=FText::FromString(Names[Index]); Entry.UnlockGoldCost=Index==0?0:200+Index*100; Entry.bUnlockedByDefault=Index==0;
        const FString Path=FString::Printf(TEXT("/Game/UI/Textures/ElementIcons/T_Ammo_%s.T_Ammo_%s"),Icons[Index],Icons[Index]); // 稳定现成资产路径。
        ConstructorHelpers::FObjectFinder<UTexture2D> Texture(*Path); // ConstructorHelpers仅在UObject构造中使用。
        Entry.Icon=Texture.Object; Entries.Add(Entry);
    }
}
FName UDemoAmmoCatalog::IdAt(int32 Index)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] Ammo IdAt"));
    const FName Ids[]={TEXT("normal"),TEXT("fire"),TEXT("frost"),TEXT("piercing")}; // 协议白名单，禁用资源路径作为标识。
    return Index>=0 && Index<4?Ids[Index]:NAME_None;
}
int32 UDemoAmmoCatalog::IndexOf(FName Id)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] Ammo IndexOf"));
    for(int32 Index=0;Index<4;++Index) if(IdAt(Index)==Id)return Index; // 四项稳定映射。
    return INDEX_NONE;
}
bool UDemoAmmoCatalog::Validate() const
{
    DEMO_LOG_CALL();
    if(Entries.Num()!=4){UE_LOG(LogFPSDemo,Error,TEXT("AMMO_CONFIG Entries requires 4 entries"));return false;}
    for(int32 Index=0;Index<4;++Index) // 固定顺序是存档协议的一部分；拒绝重复ID、负价与钱包溢出。
    {
        if(Entries[Index].Id!=IdAt(Index)||Entries[Index].UnlockGoldCost<0||Entries[Index].UnlockGoldCost>100000000)
        {UE_LOG(LogFPSDemo,Error,TEXT("AMMO_CONFIG entry=%d invalid stable Id or UnlockGoldCost"),Index);return false;}
    }
    const float Values[]={BurnDuration,BurnPeriod,BurnDamagePerStack,ExplosionDamage,ChillDuration,SlowPerStack,MaxSlow,FreezeDuration,PostThawImmunityDuration,PiercingDamageMultiplier,SecondaryDamageRatio}; // 先排除NaN/Inf再比较范围。
    for(float Value:Values) if(!FMath::IsFinite(Value)||Value<0){UE_LOG(LogFPSDemo,Error,TEXT("AMMO_CONFIG effect values must be finite and nonnegative"));return false;} // 所有浮点参数统一拒绝NaN和负值。
    const bool Valid=BurnDuration>0 && BurnPeriod>0 && BurnPeriod<=BurnDuration && ChillDuration>0 && FreezeDuration>0 && SlowPerStack<=.9f && MaxSlow<=.9f && SecondaryDamageRatio<=1 && BurnThreshold>=1 && BurnThreshold<=32 && FreezeThreshold>=1 && FreezeThreshold<=32; // 时长、比例和32层硬上限共同约束GE Spec。
    if(!Valid)UE_LOG(LogFPSDemo,Error,TEXT("AMMO_CONFIG invalid duration/period/slow/secondary ratio/threshold range"));
    return Valid;
}
