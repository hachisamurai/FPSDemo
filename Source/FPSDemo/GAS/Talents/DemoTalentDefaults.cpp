// 职责：注册稳定有序的内置天赋定义。用法：在原生GC缓存的临时Outer下建立回退；这里只组合实现引用，不写战斗逻辑。
#include "GAS/Talents/DemoTalentCatalog.h"
#include "GAS/Talents/DemoTalentDefinition.h"
#include "GAS/Talents/DemoTalentPassiveEffects.h"
#include "NativeGameplayTags.h"
#include "Debug/DemoLog.h"
#include "GAS/Talents/Abilities/DemoTalentSupply.h"
#include "GAS/Talents/Modifiers/DemoTalentMedic.h"
#include "GAS/Talents/Modifiers/DemoTalentEmber.h"
#include "GAS/Talents/Modifiers/DemoTalentHunter.h"
#include "GAS/Talents/Modifiers/DemoTalentPierce.h"
#include "GAS/Talents/Modifiers/DemoTalentOverdrive.h"
#include "GAS/Talents/Modifiers/DemoTalentTwin.h"
#include "GAS/Talents/Modifiers/DemoTalentRicochet.h"
#include "GAS/Talents/Modifiers/DemoTalentSiphon.h"
#include "GAS/Talents/Modifiers/DemoTalentPrecision.h"
#include "GAS/Talents/Modifiers/DemoTalentDashLoad.h"
#include "GAS/Talents/Abilities/DemoTalentRhythm.h"
#include "GAS/Talents/Modifiers/DemoTalentExpose.h"
#include "GAS/Talents/Abilities/DemoTalentContagion.h"
#include "GAS/Talents/Abilities/DemoTalentShatter.h"
namespace
{
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionSupply,"Demo.Gamble.Talent.Supply"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionMedic,"Demo.Gamble.Talent.Medic"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionEmber,"Demo.Gamble.Talent.Ember"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionHunter,"Demo.Gamble.Talent.Hunter"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionPierce,"Demo.Gamble.Talent.Pierce"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionOverdrive,"Demo.Gamble.Talent.Overdrive"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionTwin,"Demo.Gamble.Talent.Twin"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionRicochet,"Demo.Gamble.Talent.Ricochet"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionSiphon,"Demo.Gamble.Talent.Siphon"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionPrecision,"Demo.Gamble.Talent.Precision"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionDashLoad,"Demo.Gamble.Talent.DashLoad"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionRhythm,"Demo.Gamble.Talent.Rhythm"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionExpose,"Demo.Gamble.Talent.Expose"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionContagion,"Demo.Gamble.Talent.Contagion"); // 稳定网络字典，不能由远端存档创建。
    UE_DEFINE_GAMEPLAY_TAG_STATIC(PermissionShatter,"Demo.Gamble.Talent.Shatter"); // 稳定网络字典，不能由远端存档创建。
}
void UDemoTalentCatalog::BuildDefaults(UObject* DefinitionsOuter,TArray<TObjectPtr<UDemoTalentDefinition>>& OutDefinitions) const
{
    DEMO_LOG_CALL();
    check(DefinitionsOuter&&!DefinitionsOuter->IsTemplate()); // 临时Outer只负责命名；实际保活归原生FGCObject缓存。
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_supply"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("supply"); Definition->TalentTag=PermissionSupply; Definition->DisplayName=FText::FromString(TEXT("协同补给")); Definition->RequiredAmmo=-1;
        Definition->Abilities.Add(UDemoTalentSupply::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_medic"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("medic"); Definition->TalentTag=PermissionMedic; Definition->DisplayName=FText::FromString(TEXT("战术医疗")); Definition->RequiredAmmo=-1;
        Definition->Modifiers.Add(UDemoTalentMedic::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_ember"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("ember"); Definition->TalentTag=PermissionEmber; Definition->DisplayName=FText::FromString(TEXT("灼烧余烬")); Definition->RequiredAmmo=1;
        Definition->Modifiers.Add(UDemoTalentEmber::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_hunter"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("hunter"); Definition->TalentTag=PermissionHunter; Definition->DisplayName=FText::FromString(TEXT("冰霜猎手")); Definition->RequiredAmmo=2;
        Definition->Modifiers.Add(UDemoTalentHunter::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_pierce"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("pierce"); Definition->TalentTag=PermissionPierce; Definition->DisplayName=FText::FromString(TEXT("贯穿强化")); Definition->RequiredAmmo=3;
        Definition->Modifiers.Add(UDemoTalentPierce::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_overdrive"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("overdrive"); Definition->TalentTag=PermissionOverdrive; Definition->DisplayName=FText::FromString(TEXT("超频扳机")); Definition->RequiredAmmo=-1;
        Definition->Modifiers.Add(UDemoTalentOverdrive::StaticClass());
        Definition->PassiveEffects.Add(UDemoTalentFireRateEffect::StaticClass()); // 射速变化由专属可逆GE结算。
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_twin"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("twin"); Definition->TalentTag=PermissionTwin; Definition->DisplayName=FText::FromString(TEXT("双重弹头")); Definition->RequiredAmmo=-1;
        Definition->Modifiers.Add(UDemoTalentTwin::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_ricochet"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("ricochet"); Definition->TalentTag=PermissionRicochet; Definition->DisplayName=FText::FromString(TEXT("弹射弹头")); Definition->RequiredAmmo=-1;
        Definition->Modifiers.Add(UDemoTalentRicochet::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_siphon"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("siphon"); Definition->TalentTag=PermissionSiphon; Definition->DisplayName=FText::FromString(TEXT("战斗虹吸")); Definition->RequiredAmmo=-1;
        Definition->Modifiers.Add(UDemoTalentSiphon::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_precision"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("precision"); Definition->TalentTag=PermissionPrecision; Definition->DisplayName=FText::FromString(TEXT("精准回收")); Definition->RequiredAmmo=-1;
        Definition->Modifiers.Add(UDemoTalentPrecision::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_dashload"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("dashload"); Definition->TalentTag=PermissionDashLoad; Definition->DisplayName=FText::FromString(TEXT("冲刺装填")); Definition->RequiredAmmo=-1;
        Definition->Modifiers.Add(UDemoTalentDashLoad::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_rhythm"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("rhythm"); Definition->TalentTag=PermissionRhythm; Definition->DisplayName=FText::FromString(TEXT("猎杀节奏")); Definition->RequiredAmmo=-1;
        Definition->Abilities.Add(UDemoTalentRhythm::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_expose"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("expose"); Definition->TalentTag=PermissionExpose; Definition->DisplayName=FText::FromString(TEXT("破绽追击")); Definition->RequiredAmmo=-1;
        Definition->Modifiers.Add(UDemoTalentExpose::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_contagion"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("contagion"); Definition->TalentTag=PermissionContagion; Definition->DisplayName=FText::FromString(TEXT("灼烧传染")); Definition->RequiredAmmo=1;
        Definition->Abilities.Add(UDemoTalentContagion::StaticClass());
        OutDefinitions.Add(Definition);
    }
    {
        UDemoTalentDefinition* Definition=NewObject<UDemoTalentDefinition>(DefinitionsOuter,TEXT("Default_shatter"),RF_Transient); // 独立临时命名空间，OutDefinitions由GC引用器保活；不绑定玩家。
        Definition->TalentId=TEXT("shatter"); Definition->TalentTag=PermissionShatter; Definition->DisplayName=FText::FromString(TEXT("冰封碎裂")); Definition->RequiredAmmo=2;
        Definition->Abilities.Add(UDemoTalentShatter::StaticClass());
        OutDefinitions.Add(Definition);
    }
}
