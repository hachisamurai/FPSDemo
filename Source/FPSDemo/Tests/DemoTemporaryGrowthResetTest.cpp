// 职责：真实PS ASC上的临时成长重置合同，区分金币永久来源、银币/关卡临时成长，并保护治疗冷却与严格V10验证。
// 使用：独立Editor -game -DemoTemporaryGrowthResetTest -GameUserSettingsINI="本次独占INI"
//       -ExecCmds="Automation RunTests FPSDemo.Progress.TemporaryGrowthReset" -TestExit="Automation Test Queue Empty"。
// 依赖：实际初始化Pawn/PS/ASC、生产Instant GE、ApplyAbilityReward、Progress.ResetTemporary及ResetTemporaryRunAttributes。
// 边界：只用显式隔离fixture，不创建活动槽/存盘/关卡战斗，不跑完整Flow/死亡/旅行/多人；30秒只等待真实初始化，非数值镜像实现。
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Characters/DemoCharacter.h"
#include "Debug/DemoStaticLog.h"
#include "Economy/DemoUpgradeCatalog.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoEffects.h"
#include "GAS/DemoTags.h"
#include "Game/DemoGameState.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerState.h"
#include "Save/DemoRunSave.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    /** 只借本进程实际Standalone Game World；不创建World、不接管普通Editor/PIE。 */
    UWorld* FindTemporaryGrowthWorld()
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if (!GEngine) return nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            UWorld* World=Context.World(); // 引擎拥有；调用方只在当前GT更新同步借用。
            if (World && World->WorldType==EWorldType::Game && !World->bIsTearingDown && World->GetNetMode()==NM_Standalone) return World;
        }
        return nullptr;
    }

    /** Automation拥有的有限真实GAS合同；只在初始化就绪后执行一次，不在失败后重放GE或重置。 */
    class FTemporaryGrowthResetLatent final : public IAutomationLatentCommand
    {
    public:
        /** InTest由Automation持有至latent结束，仅借用断言接收器；Started采用单调真实秒。 */
        explicit FTemporaryGrowthResetLatent(FAutomationTestBase& InTest) : Test(InTest),Started(FPlatformTime::Seconds())
        { DEMO_LOG_STATIC_CALL(nullptr); }

        /** GT最多等待真实Pawn/PS/ASC初始化30秒；随后同步执行公开生产入口，true结束，不推进战役/World时钟。 */
        virtual bool Update() override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            if (FPlatformTime::Seconds()-Started>30.) { Check(false,TEXT("actual standalone Pawn/PS/ASC initialization exceeded 30 seconds")); return true; }
            UWorld* World=FindTemporaryGrowthWorld(); // 当前同步栈借用，不跨帧缓存旧World/Avatar。
            if (!World || World->GetTimeSeconds()<1.f) return false;
            UGameInstance* Instance=World->GetGameInstance(); // 所有服务必须来自同一游戏GI，不枚举别的PIE实例。
            ADemoPlayerController* PC=Instance?Cast<ADemoPlayerController>(Instance->GetFirstLocalPlayerController()):nullptr;
            ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
            ADemoPlayerState* PS=PC?PC->GetPlayerState<ADemoPlayerState>():nullptr;
            UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr;
            UDemoAbilitySystemComponent* ASC=Player?Player->GetDemoASC():nullptr;
            const UDemoAttributeSet* Attributes=Player?Player->GetDemoAttributes():nullptr;
            UDemoRunSaves* Saves=Instance?Instance->GetSubsystem<UDemoRunSaves>():nullptr;
            const ADemoGameState* State=World->GetGameState<ADemoGameState>();
            if (!PC || !Player || !PS || !Progress || !ASC || !Attributes || !Saves || !State) return false;
            if (!Check(PC->HasAuthority() && PC->IsLocalController() && Player->IsLocallyControlled() && ASC==PS->GetDemoASC() && ASC->GetOwnerActor()==PS && ASC->GetAvatarActor()==Player,TEXT("fixture uses the actual local authoritative Pawn and its PlayerState ASC"))) return true;
            if (!Check(State->Phase==EDemoPhase::Lobby && Saves->GetActiveSlot()==INDEX_NONE && !Saves->SlotExists(0),TEXT("fresh isolated GI remains in Lobby without creating or loading a slot"))) return true;
            if (!Check(Attributes->GetMaxHealth()==100.f && Attributes->GetHealth()==100.f && Attributes->GetWeaponDamageBonus()==0.f && Attributes->GetMagazineBonus()==0.f && Player->GetHealAmount()==35.f && Player->GetDashSpeed()==1300.f,TEXT("actual fresh GAS and skill defaults are the unmodified baseline"))) return true;

            const uint64 BeforeSaveSerial=Saves->GetChangeSerial(); // 本合同无活动槽/保存调用，全部fixture与重置不得推进此水位。
            FDemoPlayerProgressSnapshot Fixture=Progress->GetSnapshot(); // 明确隔离fixture；保原Ownership/解锁/研究身份，不伪造永久枪权。
            Fixture.Coins=731; Fixture.SilverCoins=83; // 金币永久余额与本轮银币余额，单位币，仅该独立进程使用。
            Fixture.UpgradeProgress.GoldLevels={1,2,1}; Fixture.UpgradeProgress.SilverLevels={4,3,2}; // 固定伤害/生命/弹匣顺序；生命两次金币来源保留。
            Fixture.UpgradeProgress.PermanentHealth=50.f; Fixture.UpgradeProgress.PermanentDamage=5.f; Fixture.UpgradeProgress.PermanentMagazine=4.f;
            Fixture.PistolChallenge=1; Fixture.LastRewardLevel=6; // 本轮资格/奖励水位，重置后必须清零，不能污染下一轮。
            if (!Check(Progress->Commit(Fixture),TEXT("explicit permanent-and-temporary ledger fixture is accepted by production Commit"))) return true;
            const FDemoPlayerProgressSnapshot BeforeReset=Progress->GetSnapshot(); // 冻结生产Commit后的真实版本与完整永久来源，不使用输入Revision猜结果。
            TArray<int32> BeforeGoldPrices; // 三项原生产报价，验证金币价格不会随临时重置降低。
            for (int32 Choice=0;Choice<3;++Choice) BeforeGoldPrices.Add(BeforeReset.UpgradeProgress.Cost(Choice,true));

            const bool bPermanentGE=DemoEffects::Apply(ASC,ASC,UDemoMaxHealthEffect::StaticClass(),50.f)
                && DemoEffects::Apply(ASC,ASC,UDemoPowerEffect::StaticClass(),5.f)
                && DemoEffects::Apply(ASC,ASC,UDemoMagazineEffect::StaticClass(),4.f); // 真实Instant GE模拟已经取得的永久增量，不直接写AttributeSet成员。
            const bool bTemporaryGE=DemoEffects::Apply(ASC,ASC,UDemoMaxHealthEffect::StaticClass(),75.f)
                && DemoEffects::Apply(ASC,ASC,UDemoPowerEffect::StaticClass(),20.f)
                && DemoEffects::Apply(ASC,ASC,UDemoMagazineEffect::StaticClass(),8.f); // 本轮银币/奖励合计，仍沿真实GE属性结算路径。
            if (!Check(bPermanentGE && bTemporaryGE && Attributes->GetMaxHealth()==225.f && Attributes->GetWeaponDamageBonus()==25.f && Attributes->GetMagazineBonus()==12.f,TEXT("real Instant GEs produce combined MaxHealth225 damage25 magazine12"))) return true;
            if (!Check(DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),125.f) && Attributes->GetHealth()==225.f && DemoEffects::Apply(ASC,ASC,UDemoHealthEffect::StaticClass(),-42.f) && Attributes->GetHealth()==183.f,TEXT("real Health GEs fill then injure the actual Avatar before reset"))) return true;
            if (!Check(Player->ApplyAbilityReward(1) && Player->ApplyAbilityReward(2) && Player->GetHealAmount()>35.f && Player->GetDashSpeed()>1300.f,TEXT("production free ability rewards increase heal and dash for this run"))) return true;

            FGameplayEffectQuery CooldownQuery; CooldownQuery.EffectDefinition=UDemoHealCooldownEffect::StaticClass(); // 精确查询正常治疗冷却定义，不用全量GE清理制造测试前提。
            if (!Check(ASC->GetActiveEffects(CooldownQuery).IsEmpty() && DemoEffects::Apply(ASC,ASC,UDemoHealCooldownEffect::StaticClass(),0.f),TEXT("production duration GE creates a real heal cooldown without removing unrelated effects"))) return true;
            const TArray<FActiveGameplayEffectHandle> BeforeCooldowns=ASC->GetActiveEffects(CooldownQuery); // ASC持有活动GE，本数组仅保存身份句柄。
            if (!Check(BeforeCooldowns.Num()==1 && ASC->HasMatchingGameplayTag(DemoTags::HealCooldown) && ASC->GetCooldownRemaining(DemoTags::HealCooldown)>11.f,TEXT("actual heal cooldown is active with its normal duration"))) return true;
            const FActiveGameplayEffectHandle CooldownHandle=BeforeCooldowns[0]; // 后续必须同一活动GE，不能删掉重建来冒充保持冷却。
            const FActiveGameplayEffect* Cooldown=ASC->GetActiveGameplayEffect(CooldownHandle); // 只同步读取；每次可能修改GE的生产调用后重新取指针。
            if (!Check(Cooldown!=nullptr,TEXT("real cooldown handle resolves before reset"))) return true;
            const float CooldownStart=Cooldown->StartWorldTime,CooldownDuration=Cooldown->Spec.GetDuration(); // 世界秒；检验不刷新起点/时长。

            Progress->ResetTemporary(); // 真实生产账本/临时天赋清理入口；不是对结果手动赋零。
            const FDemoPlayerProgressSnapshot LedgerAfterReset=Progress->GetSnapshot(); // helper不应再提交或改动这份真实账本。
            if (!Check(LedgerAfterReset.Coins==731 && LedgerAfterReset.SilverCoins==0 && LedgerAfterReset.UpgradeProgress.GoldLevels==BeforeReset.UpgradeProgress.GoldLevels && LedgerAfterReset.UpgradeProgress.PermanentHealth==50.f && LedgerAfterReset.UpgradeProgress.PermanentDamage==5.f && LedgerAfterReset.UpgradeProgress.PermanentMagazine==4.f,TEXT("production ledger reset retains gold and all permanent source values while clearing silver"))) return true;
            if (!Check(LedgerAfterReset.UpgradeProgress.SilverLevels==TArray<int32>({0,0,0}) && LedgerAfterReset.PistolChallenge==0 && LedgerAfterReset.LastRewardLevel==0 && FDemoWeaponOwnership::StaticStruct()->CompareScriptStruct(&BeforeReset.Ownership,&LedgerAfterReset.Ownership,0) && LedgerAfterReset.UnlockedWeaponIds==BeforeReset.UnlockedWeaponIds,TEXT("silver levels and run metadata clear without granting or removing weapon ownership"))) return true;
            const UDemoUpgradeCatalog* Catalog=UDemoUpgradeCatalog::Get(); // 与实际购买同一目录，仅检验原Cost查询输出，不复制价格算法。
            for (int32 Choice=0;Choice<3;++Choice)
            {
                const FDemoUpgradeDefinition* Offer=Catalog?Catalog->Find(false,Choice):nullptr; // 同步借用目录定义，不改DA/CDO。
                if (!Check(Offer && LedgerAfterReset.UpgradeProgress.Cost(Choice,false)==Offer->BasePrice && LedgerAfterReset.UpgradeProgress.Cost(Choice,true)==BeforeGoldPrices[Choice],FString::Printf(TEXT("silver price resets but gold price remains for choice %d"),Choice))) return true;
            }

            if (!Check(Player->ResetTemporaryRunAttributes(),TEXT("production same-Pawn reset accepts the real owner ASC and ledger"))) return true;
            if (!Check(Attributes->GetMaxHealth()==150.f && Attributes->GetHealth()==150.f && Attributes->GetWeaponDamageBonus()==5.f && Attributes->GetMagazineBonus()==4.f && Player->GetHealAmount()==35.f && Player->GetDashSpeed()==1300.f,TEXT("actual GAS and skills reset to full permanent Health150 damage5 magazine4 heal35 dash1300"))) return true;
            if (!Check(FDemoPlayerProgressSnapshot::StaticStruct()->CompareScriptStruct(&LedgerAfterReset,&Progress->GetSnapshot(),0) && Saves->GetChangeSerial()==BeforeSaveSerial,TEXT("attribute reset leaves complete progress revision and save serial unchanged"))) return true;
            const TArray<FActiveGameplayEffectHandle> AfterCooldowns=ASC->GetActiveEffects(CooldownQuery); // 按同一真实定义重新查询，不能沿用调用前数组冒充GE仍存活。
            Cooldown=ASC->GetActiveGameplayEffect(CooldownHandle);
            if (!Check(AfterCooldowns.Num()==1 && AfterCooldowns[0]==CooldownHandle && Cooldown && Cooldown->StartWorldTime==CooldownStart && Cooldown->Spec.GetDuration()==CooldownDuration && ASC->HasMatchingGameplayTag(DemoTags::HealCooldown) && ASC->GetCooldownRemaining(DemoTags::HealCooldown)>11.f,TEXT("ledger and attribute reset preserve the same heal cooldown handle and clock"))) return true;
            if (!Check(Player->ResetTemporaryRunAttributes() && Attributes->GetMaxHealth()==150.f && Attributes->GetHealth()==150.f && Attributes->GetWeaponDamageBonus()==5.f && Attributes->GetMagazineBonus()==4.f && Player->GetHealAmount()==35.f && Player->GetDashSpeed()==1300.f && FDemoPlayerProgressSnapshot::StaticStruct()->CompareScriptStruct(&LedgerAfterReset,&Progress->GetSnapshot(),0),TEXT("repeated attribute reset is idempotent and cannot compound permanent growth or revision"))) return true;
            Cooldown=ASC->GetActiveGameplayEffect(CooldownHandle); // 第二次helper后再次读真实活动GE，确认幂等调用也没有删重建或刷新冷却。
            if (!Check(Cooldown && Cooldown->StartWorldTime==CooldownStart && Cooldown->Spec.GetDuration()==CooldownDuration && ASC->HasMatchingGameplayTag(DemoTags::HealCooldown),TEXT("repeated reset retains the original heal cooldown and timing"))) return true;

            UDemoRunSave* Incomplete=NewObject<UDemoRunSave>(GetTransientPackage(),NAME_None,RF_Transient); // 只复现旧局部基线的缺身份形状，不写盘或传入恢复接口。
            Incomplete->CreatedLocal=FDateTime::Now(); Incomplete->RunId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens); Incomplete->UpgradeProgress=BeforeReset.UpgradeProgress;
            Incomplete->ResetTemporaryGrowth();
            if (!Check(Incomplete->Version==10 && !Incomplete->Validate(),TEXT("strict V10 still rejects the old incomplete reset baseline without slot identity or frozen ownership"))) return true;
            if (!Check(Saves->GetActiveSlot()==INDEX_NONE && Saves->GetChangeSerial()==BeforeSaveSerial,TEXT("isolated GAS contract created no active slot and performed no checkpoint save"))) return true;
            UE_LOG(LogFPSDemo,Display,TEXT("TEMPORARY_GROWTH_RESET_TEST_SUCCESS real PS ASC; permanent and temporary Instant GE fixture; production rewards; silver source/price reset; permanent attributes retained; heal cooldown preserved; strict V10. Full Flow/victory/death/travel/UI/multiplayer NOT_RUN"));
            return true;
        }

    private:
        /** Condition为真实生产读数；Message说明本条范围，失败交给Automation记录，不退出普通Editor进程。 */
        bool Check(bool Condition,const FString& Message)
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            Test.TestTrue(Message,Condition);
            UE_LOG(LogFPSDemo,Display,TEXT("TEMPORARY_GROWTH_RESET_TEST %s %s"),Condition?TEXT("PASS"):TEXT("FAIL"),*Message);
            return Condition;
        }

        FAutomationTestBase& Test; // Automation拥有且寿命覆盖latent队列，仅借用断言接收器。
        const double Started; // 单调真实秒；只用于30秒初始化超时，不改World时间或冷却。
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoTemporaryGrowthResetTest,"FPSDemo.Progress.TemporaryGrowthReset",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

/** Parameters为Automation未使用参数；只允许显式独立Editor游戏与隔离设备INI，fixture不创建slot或写正式进度。 */
bool FDemoTemporaryGrowthResetTest::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    FString SettingsINI; // 启动器本次独占设备设置路径；不能复用用户正常设备配置作为测试前提。
    if (!FParse::Param(FCommandLine::Get(),TEXT("game")) || !FParse::Param(FCommandLine::Get(),TEXT("DemoTemporaryGrowthResetTest")) || !FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),SettingsINI) || SettingsINI.IsEmpty())
    {
        AddError(TEXT("Requires a fresh independent Editor -game -DemoTemporaryGrowthResetTest process with an isolated -GameUserSettingsINI path; no ordinary Editor/PIE session."));
        return false;
    }
    ADD_LATENT_AUTOMATION_COMMAND(FTemporaryGrowthResetLatent(*this));
    return true;
}
#endif
