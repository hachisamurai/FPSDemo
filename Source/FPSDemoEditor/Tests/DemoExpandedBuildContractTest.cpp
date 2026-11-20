// 职责：B01～B07选择/分支与扩展核心配置的纯值合同专项。使用：Automation RunTests FPSDemo.Builds.ExpandedContract；不创建World、不进入PIE/-game、不访问真实存档/云或修改配置CDO，不证明Gameplay通过。
#include "Misc/AutomationTest.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Progression/Builds/DemoExpandedBuildSettings.h"
#include "UObject/UObjectGlobals.h"
#include "Debug/DemoLog.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoExpandedBuildContractTest, "FPSDemo.Builds.ExpandedContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Parameters由Automation提供且不用于路径/配置覆盖；只调用生产纯值校验，所有可变UObject均为瞬态独立实例。 */
bool FDemoExpandedBuildContractTest::RunTest(const FString& Parameters)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] FDemoExpandedBuildContractTest::RunTest"));
    const TCHAR* ExpectedCores[] = { TEXT("B01"), TEXT("B02"), TEXT("B03"), TEXT("B04"), TEXT("B05"), TEXT("B06"), TEXT("B07"), TEXT("B08"), TEXT("B09") }; // 人工稳定基线：旧三核心及B04～B07索引不能因扩容重排。
    const TCHAR* OriginalTitles[] = { TEXT("冰裂突围"), TEXT("冲刺突击"), TEXT("精准猎杀") }; // 原三核心显示身份保留，测试不重解释其Gameplay语义。
    TestEqual(TEXT("Trusted catalog contains nine cores"), DemoRunBuild::CoreCount(), 9);
    TestTrue(TEXT("Negative catalog index is empty"), DemoRunBuild::CoreAt(-1).IsNone());
    TestTrue(TEXT("Past-end catalog index is empty"), DemoRunBuild::CoreAt(9).IsNone());
    TestFalse(TEXT("Unknown core ID rejected"), DemoRunBuild::IsCore(TEXT("B10")));
    TestTrue(TEXT("Unknown title remains empty"), DemoRunBuild::Title(TEXT("B10")).IsEmpty());
    TestTrue(TEXT("Unknown description remains empty"), DemoRunBuild::Describe(TEXT("B10")).IsEmpty());

    FDemoRunBuildSnapshot Empty; // 尚未选择的合法根/队员检查点值，不创建组件或写文件。
    TestTrue(TEXT("Unselected clean snapshot accepted"), Empty.Validate());
    Empty.bLocked = true; TestFalse(TEXT("No-core locked residue rejected"), Empty.Validate());
    Empty = FDemoRunBuildSnapshot(); Empty.Branch3 = TEXT("B01-A"); TestFalse(TEXT("No-core first branch residue rejected"), Empty.Validate());
    Empty = FDemoRunBuildSnapshot(); Empty.Branch7 = TEXT("B01-C"); TestFalse(TEXT("No-core second branch residue rejected"), Empty.Validate());
    Empty = FDemoRunBuildSnapshot(); Empty.PendingBranchStage = 3; TestFalse(TEXT("No-core pending reward residue rejected"), Empty.Validate());
    Empty.PendingBranchStage = 7; TestFalse(TEXT("No-core later pending reward residue rejected"), Empty.Validate());
    Empty = FDemoRunBuildSnapshot(); Empty.Revision = -1; TestFalse(TEXT("Negative revision rejected"), Empty.Validate());
    Empty = FDemoRunBuildSnapshot(); Empty.CoreId = TEXT("B10"); TestFalse(TEXT("Unknown selected snapshot rejected"), Empty.Validate());

    for (int32 CoreIndex = 0; CoreIndex < UE_ARRAY_COUNT(ExpectedCores); ++CoreIndex)
    {
        const FName Core(ExpectedCores[CoreIndex]); // 每个稳定ID均走相同保存合同，不只检查新增四核心。
        const FString Prefix = Core.ToString() + TEXT("-"); // 只由人工可信ID构造，不解析外部资源路径。
        const FString Label = Core.ToString(); // 断言前缀保留失败所属核心，便于自动化日志定位。
        TestEqual(*FString::Printf(TEXT("%s keeps stable catalog index"), *Label), DemoRunBuild::CoreAt(CoreIndex), Core);
        TestTrue(*FString::Printf(TEXT("%s recognized by shared catalog"), *Label), DemoRunBuild::IsCore(Core));
        TestFalse(*FString::Printf(TEXT("%s has title"), *Label), DemoRunBuild::Title(Core).IsEmpty());
        TestFalse(*FString::Printf(TEXT("%s has description"), *Label), DemoRunBuild::Describe(Core).IsEmpty());
        if (CoreIndex < UE_ARRAY_COUNT(OriginalTitles))
            TestEqual(*FString::Printf(TEXT("%s preserves original title"), *Label), DemoRunBuild::Title(Core).ToString(), FString(OriginalTitles[CoreIndex]));

        FDemoRunBuildSnapshot Selected; // 纯值模拟选择已接受，不声称覆盖需要World/权威门禁的TrySelectCore。
        Selected.CoreId = Core; Selected.Revision = 1;
        TestTrue(*FString::Printf(TEXT("%s unlocked selection snapshot accepted"), *Label), Selected.Validate());
        Selected.bLocked = true;
        TestTrue(*FString::Printf(TEXT("%s locked selection without branch accepted"), *Label), Selected.Validate());
        Selected.PendingBranchStage = 3;
        TestTrue(*FString::Printf(TEXT("%s pending first reward accepted"), *Label), Selected.Validate());
        Selected.bLocked = false;
        TestFalse(*FString::Printf(TEXT("%s unlocked pending reward rejected"), *Label), Selected.Validate());
        Selected.bLocked = true; Selected.PendingBranchStage = 7;
        TestFalse(*FString::Printf(TEXT("%s second reward requires first branch"), *Label), Selected.Validate());
        Selected.PendingBranchStage = 4;
        TestFalse(*FString::Printf(TEXT("%s unsupported pending stage rejected"), *Label), Selected.Validate());
        Selected.PendingBranchStage = 0; Selected.Branch7 = FName(Prefix + TEXT("C"));
        TestFalse(*FString::Printf(TEXT("%s second branch requires first branch"), *Label), Selected.Validate());

        for (const TCHAR* FirstSuffix : { TEXT("A"), TEXT("B") }) // 穷举第一组两条路线及第二组两条路线，共28个合法完整组合。
        {
            const FName First(Prefix + FirstSuffix); // 本核心第3关稳定分支ID。
            FDemoRunBuildSnapshot FirstChoice; // 每条路径独立值，失败用例不能污染下一条组合。
            FirstChoice.CoreId = Core; FirstChoice.Branch3 = First; FirstChoice.bLocked = true; FirstChoice.Revision = 2;
            TestTrue(*FString::Printf(TEXT("%s first branch accepted"), *First.ToString()), FirstChoice.Validate());
            TestFalse(*FString::Printf(TEXT("%s branch title exists"), *First.ToString()), DemoRunBuild::Title(First).IsEmpty());
            TestFalse(*FString::Printf(TEXT("%s branch description exists"), *First.ToString()), DemoRunBuild::Describe(First).IsEmpty());
            FirstChoice.bLocked = false;
            TestFalse(*FString::Printf(TEXT("%s unlocked branch rejected"), *First.ToString()), FirstChoice.Validate());
            FirstChoice.bLocked = true; FirstChoice.PendingBranchStage = 3;
            TestFalse(*FString::Printf(TEXT("%s cannot offer already selected first reward"), *First.ToString()), FirstChoice.Validate());
            FirstChoice.PendingBranchStage = 7;
            TestTrue(*FString::Printf(TEXT("%s may offer second reward"), *First.ToString()), FirstChoice.Validate());
            for (const TCHAR* SecondSuffix : { TEXT("C"), TEXT("D") })
            {
                const FName Second(Prefix + SecondSuffix); // 本核心第7关稳定分支ID。
                FDemoRunBuildSnapshot Complete = FirstChoice; // 模拟完成第7关选择，清待领奖状态后才合法。
                Complete.Branch7 = Second; Complete.PendingBranchStage = 0; Complete.Revision = 3;
                const FString Combination = First.ToString() + TEXT("+") + Second.ToString(); // 可复核完整路径标识。
                TestTrue(*FString::Printf(TEXT("%s complete combination accepted"), *Combination), Complete.Validate());
                TestFalse(*FString::Printf(TEXT("%s branch title exists"), *Second.ToString()), DemoRunBuild::Title(Second).IsEmpty());
                TestFalse(*FString::Printf(TEXT("%s branch description exists"), *Second.ToString()), DemoRunBuild::Describe(Second).IsEmpty());
                Complete.PendingBranchStage = 7;
                TestFalse(*FString::Printf(TEXT("%s cannot duplicate second reward"), *Combination), Complete.Validate());
                Complete.PendingBranchStage = 0; Complete.bLocked = false;
                TestFalse(*FString::Printf(TEXT("%s unlocked complete branches rejected"), *Combination), Complete.Validate());
            }
        }

        FDemoRunBuildSnapshot Wrong; // 同一核心只允许A/B在第一槽、C/D在第二槽。
        Wrong.CoreId = Core; Wrong.bLocked = true; Wrong.Branch3 = FName(Prefix + TEXT("C"));
        TestFalse(*FString::Printf(TEXT("%s second-tier ID rejected in first slot"), *Label), Wrong.Validate());
        Wrong.Branch3 = FName(Prefix + TEXT("A")); Wrong.Branch7 = FName(Prefix + TEXT("B"));
        TestFalse(*FString::Printf(TEXT("%s first-tier ID rejected in second slot"), *Label), Wrong.Validate());
        Wrong.Branch3 = FName(Prefix + TEXT("E")); Wrong.Branch7 = NAME_None;
        TestFalse(*FString::Printf(TEXT("%s unknown branch suffix rejected"), *Label), Wrong.Validate());
        for (int32 OtherIndex = 0; OtherIndex < UE_ARRAY_COUNT(ExpectedCores); ++OtherIndex)
        {
            if (OtherIndex == CoreIndex) continue;
            const FString OtherPrefix = FString(ExpectedCores[OtherIndex]) + TEXT("-"); // 穷举其余六个核心，防止只测相邻ID而漏掉前缀宽松解析。
            for (const TCHAR* ForeignFirst : { TEXT("A"), TEXT("B") })
            {
                Wrong.Branch3 = FName(OtherPrefix + ForeignFirst); Wrong.Branch7 = NAME_None;
                TestFalse(*FString::Printf(TEXT("%s rejects foreign first branch %s%s"), *Label, *OtherPrefix, ForeignFirst), Wrong.Validate());
            }
            for (const TCHAR* ForeignSecond : { TEXT("C"), TEXT("D") })
            {
                Wrong.Branch3 = FName(Prefix + TEXT("A")); Wrong.Branch7 = FName(OtherPrefix + ForeignSecond);
                TestFalse(*FString::Printf(TEXT("%s rejects foreign second branch %s%s"), *Label, *OtherPrefix, ForeignSecond), Wrong.Validate());
            }
        }
    }

    const UDemoExpandedBuildSettings* Defaults = GetDefault<UDemoExpandedBuildSettings>(); // 生产配置只读；INI若非法，本专项应明确失败而非静默改写。
    if (!TestTrue(TEXT("Configured expanded-build defaults validate"), Defaults->Validate())) return false;
    UDemoExpandedBuildSettings* Candidate = NewObject<UDemoExpandedBuildSettings>(GetTransientPackage()); // 独立瞬态实例从默认模板初始化，所有破坏性用例只修改此对象。
    if (!TestNotNull(TEXT("Independent settings candidate exists"), Candidate)) return false;
    TestTrue(TEXT("Candidate is not production CDO"), Candidate != Defaults);
    if (!TestTrue(TEXT("Copied settings initially validate"), Candidate->Validate())) return false;
    Candidate->AnchorRadius = std::numeric_limits<float>::quiet_NaN(); TestFalse(TEXT("NaN anchor radius rejected"), Candidate->Validate());
    Candidate->AnchorRadius = Defaults->AnchorRadius;
    Candidate->CounterBudgetCap = std::numeric_limits<float>::infinity(); TestFalse(TEXT("Infinite healing budget rejected"), Candidate->Validate());
    Candidate->CounterBudgetCap = Defaults->CounterBudgetCap;
    Candidate->RhythmWindowStart = .8f; Candidate->RhythmWindowEnd = .2f; TestFalse(TEXT("Reversed reload timing window rejected"), Candidate->Validate());
    Candidate->RhythmWindowStart = Defaults->RhythmWindowStart; Candidate->RhythmWindowEnd = Defaults->RhythmWindowEnd;
    Candidate->RhythmWindowEnd = Candidate->RhythmWindowStart; TestFalse(TEXT("Empty reload timing window rejected"), Candidate->Validate());
    Candidate->RhythmWindowEnd = Defaults->RhythmWindowEnd;
    Candidate->RhythmToleranceSeconds = .151f; TestFalse(TEXT("Unbounded reload network tolerance rejected"), Candidate->Validate());
    Candidate->RhythmToleranceSeconds = Defaults->RhythmToleranceSeconds;
    Candidate->RhythmStableCredits = Candidate->RhythmCredits + 1; TestFalse(TEXT("Automatic reload credits above base budget rejected"), Candidate->Validate());
    Candidate->RhythmStableCredits = Defaults->RhythmStableCredits;
    Candidate->CounterShareRatio = 1.f; TestFalse(TEXT("Shared healing ratio equal to one rejected"), Candidate->Validate());
    Candidate->CounterShareRatio = 1.1f; TestFalse(TEXT("Shared healing ratio above one rejected"), Candidate->Validate());
    Candidate->CounterShareRatio = Defaults->CounterShareRatio;
    Candidate->CounterLossRatio = .5001f; TestFalse(TEXT("Healing loss budget above one half rejected"), Candidate->Validate());
    Candidate->CounterLossRatio = Defaults->CounterLossRatio;
    Candidate->CounterCooldown = Candidate->CounterWindow * .5f; TestFalse(TEXT("Counter cooldown shorter than recovery window rejected"), Candidate->Validate());
    Candidate->CounterCooldown = Defaults->CounterCooldown;
    Candidate->CrossfireTargets = 17; TestFalse(TEXT("Crossfire target count above sixteen rejected"), Candidate->Validate());
    Candidate->CrossfireTargets = Defaults->CrossfireTargets;
    Candidate->AnchorTargets = 17; TestFalse(TEXT("Anchor target count above sixteen rejected"), Candidate->Validate());
    Candidate->AnchorTargets = Defaults->AnchorTargets;
    Candidate->AnchorChargeHits = 0; TestFalse(TEXT("Zero anchor hit threshold rejected"), Candidate->Validate());
    Candidate->AnchorChargeHits = Defaults->AnchorChargeHits;
    Candidate->AnchorLifetime = 61.f; TestFalse(TEXT("Unbounded anchor lifetime rejected"), Candidate->Validate());
    Candidate->AnchorLifetime = Defaults->AnchorLifetime;
    Candidate->AnchorDeployRange = Candidate->AnchorRadius - 1.f; TestFalse(TEXT("Anchor deployment range smaller than area rejected"), Candidate->Validate());
    Candidate->AnchorDeployRange = Defaults->AnchorDeployRange;
    Candidate->AnchorConeCos = 1.01f; TestFalse(TEXT("Impossible cone cosine rejected"), Candidate->Validate());
    Candidate->AnchorConeCos = Defaults->AnchorConeCos;
    Candidate->CloseRangeCm = 50.f; TestFalse(TEXT("Too-small close-quarters range rejected"), Candidate->Validate());
    Candidate->CloseRangeCm = Defaults->CloseRangeCm;
    Candidate->CloseBurstCap = Candidate->CloseBurstMinDamage - 1.f; TestFalse(TEXT("Close burst cap below minimum rejected"), Candidate->Validate());
    Candidate->CloseBurstCap = Defaults->CloseBurstCap;
    Candidate->MarksmanChargeSeconds = 0.05f; TestFalse(TEXT("Too-fast marksman charge time rejected"), Candidate->Validate());
    Candidate->MarksmanChargeSeconds = Defaults->MarksmanChargeSeconds;
    Candidate->MarksmanOverkillCap = Candidate->MarksmanOverkillMin - 1.f; TestFalse(TEXT("Marksman overkill cap below minimum rejected"), Candidate->Validate());
    Candidate->MarksmanOverkillCap = Defaults->MarksmanOverkillCap;
    TestTrue(TEXT("Restored private candidate remains valid"), Candidate->Validate());
    TestTrue(TEXT("Read-only production CDO remains valid"), Defaults->Validate());
    if (!HasAnyErrors()) UE_LOG(LogFPSDemo, Display, TEXT("EXPANDED_BUILD_CONTRACT_SUCCESS scope=selection-config-only gameplay=not-tested"));
    return !HasAnyErrors();
}
#endif
