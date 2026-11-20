// 职责：显式开发测试参数的启动分发。使用：GameMode开始时调用，Editor专项只用本GI临时存档，客户端不生成权威测试Actor。
#include "Debug/DemoDevelopmentBootstrap.h"
#include "Tests/DemoMeleeTest.h" // 近战专项仅WITH_EDITOR参数启用，普通游玩不创建测试夹具。
#include "Tests/DemoViewTurnTest.h" // 超阈值转身专项只在下方WITH_EDITOR显式分发，不给包体增加控制台入口。
#include "Tests/DemoViewGhostAudit.h" // 画面拖影与真实投影隔离，只在Editor显式单进程采样，不改正式资产。
#include "Tests/DemoMetalImpactTest.h" // 金属命中/池压力测试，仅显式Editor进程，使用当前GI临时进度。
#include "Tests/DemoMeleeNetworkTest.h" // 双进程近战专项，只用正式LAN与Editor临时进度。
#include "Tests/DemoCasinoPhysicalInteractionTest.h" // CP-01独立真实控件/本地耐久夹具，仅下方Editor -game Standalone显式分发。
#include "Debug/DemoLog.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#if !UE_BUILD_SHIPPING
#include "Tests/DemoGambleTest.h" // 启动条件额外限制WITH_EDITOR，生产包无赌博测试命令。
#include "Tests/DemoCommonUITest.h" // 真实UMG专项，启动分发仍受WITH_EDITOR限制。
#include "Tests/DemoSessionUITest.h" // 大厅/会话迁移专项，只在Editor显式启动。
#include "Tests/DemoPlayerStatusTest.h" // Tab/等级专项只在显式Editor单人进程运行。
#include "Tests/DemoCombatTalentTest.h" // 新天赋只在显式Editor临时World回归。
#include "Tests/DemoViewReelTest.h" // 起跳相机/真实UI滚动专项，只有Editor显式参数创建。
#include "Tests/DemoAmmoTest.h"
#include "Tests/DemoProjectileTest.h"
#include "Tests/DemoSmokeTest.h"
#include "Tests/DemoSessionTest.h"
#include "Tests/DemoWeaponTest.h"
#include "Tests/DemoWeaponAnimationTest.h" // 显式命令行启动的四枪动画/音效专项，不影响正常关卡流程。
#include "Tests/DemoPlayerAnimationTest.h" // 玩家模型专项只在显式Editor临时进度中运行，记录真实全身骨姿势。
#include "Tests/DemoPlayerSpectatorTest.h" // 显式独立观战回归，验证新全身模型的ViewTarget可见性契约。
#include "Tests/DemoPlayerNetworkTest.h" // 真实LAN双进程探针仅由显式房主启动；客户端通过拥有者测试Actor观测。
#include "Tests/DemoDashNetworkTest.h" // 独立按键/方向/冷却双端回归，不能与其他驱动角色的专项同开。
#include "Tests/DemoPlayerEyesTest.h" // 独立近景眼神实机入口，不扩大原808帧四枪回归。
#include "Tests/DemoArmoryTest.h"
#include "Tests/DemoCloudTest.h"
#include "Tests/DemoCampaignTest.h"
#include "Tests/DemoEnemyAttackTest.h"
#endif
void DemoDevelopmentBootstrap::Start(UWorld* World)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    if (!World || World->GetNetMode() == NM_Client) return; // 普通客户端不生成权威测试Actor。
#if !UE_BUILD_SHIPPING
	FString CloudTestMode; // 真实云测试只有显式模式且测试Actor再次验证隔离GUID才可推进。
	if (FParse::Value(FCommandLine::Get(), TEXT("DemoCloudTest="), CloudTestMode)) World->SpawnActor<ADemoCloudTest>();
	// 测试入口只有显式命令行参数才启用；普通 PIE/游戏不生成测试 Actor。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoSmokeTest"))) World->SpawnActor<ADemoSmokeTest>();
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoSessionTest"))) World->SpawnActor<ADemoSessionTest>(); // 显式测试才覆盖暂停/旅行输入。
	// 武器专用测试通过真实InputKey进入映射链路，仅显式参数开启。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponTest"))) World->SpawnActor<ADemoWeaponTest>();
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoWeaponAnimationTest"))) World->SpawnActor<ADemoWeaponAnimationTest>(); // 独立进程验证真实LinkedLayer、双网格与取消清理。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoPlayerAnimationTest"))) World->SpawnActor<ADemoPlayerAnimationTest>(); // 全身移动/装填独立进程，不与其他专项同时驱动同一World。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoPlayerSpectatorTest"))) World->SpawnActor<ADemoPlayerSpectatorTest>(); // 观战与动作专项各用独立World，避免同时修改本机ViewTarget。
#if WITH_EDITOR
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoCasinoPhysicalTest")))
    {
        if (FParse::Param(FCommandLine::Get(),TEXT("game")) && World->WorldType==EWorldType::Game && World->GetNetMode()==NM_Standalone) World->SpawnActor<ADemoCasinoPhysicalInteractionTest>();
        else UE_LOG(LogFPSDemo,Warning,TEXT("CASINO_PHYSICAL_TEST_REJECT isolated Editor -game Standalone required")); // 不在用户PIE启动会退出进程的测试，不放开Editor云边界。
    }
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoViewTurnTest"))) World->SpawnActor<ADemoViewTurnTest>(); // 独立真实输入World，不与其他驱动测试同开。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoViewGhostAudit"))) World->SpawnActor<ADemoViewGhostAudit>(); // 六种相同视线轨迹消融；普通PIE及包体无入口。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMetalImpactTest"))) World->SpawnActor<ADemoMetalImpactTest>(); // 独立专项，不与其他输入驱动测试共用World。
    const bool bComboRegression=FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeComboRegression")); // 新专项必须额外-game且非PIE；禁止在用户Editor中生成会退出进程的夹具。
    if (bComboRegression && (!FParse::Param(FCommandLine::Get(),TEXT("game")) || World->WorldType!=EWorldType::Game || World->GetNetMode()!=NM_Standalone))
        UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_COMBO_REGRESSION_REJECT isolated Editor -game Standalone World required"));
    if ((!bComboRegression && (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeTest")) || FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeWideTest"))))
        || (bComboRegression && FParse::Param(FCommandLine::Get(),TEXT("game")) && World->WorldType==EWorldType::Game && World->GetNetMode()==NM_Standalone)) World->SpawnActor<ADemoMeleeTest>(); // 新显式模式独立优先；旧两种计划不变。
    if (FParse::Param(FCommandLine::Get(),TEXT("DemoMeleeNetworkTest"))) World->SpawnActor<ADemoMeleeNetworkTest>(); // 远端显式参数参与测试，不自动代替客户端输入。
    if (FParse::Param(FCommandLine::Get(), TEXT("DemoPlayerStatusTest"))) World->SpawnActor<ADemoPlayerStatusTest>(); // 独立临时World，不启用云同步。
    if (FParse::Param(FCommandLine::Get(), TEXT("DemoCombatTalentTest"))) World->SpawnActor<ADemoCombatTalentTest>(); // 独立进程，不与其他输入/战斗专项共用World。
    if (FParse::Param(FCommandLine::Get(), TEXT("DemoViewReelTest"))) World->SpawnActor<ADemoViewReelTest>(); // 独立临时存档进程，不与其他输入驱动专项共开。
    if (FParse::Param(FCommandLine::Get(), TEXT("DemoCasinoUITest"))) World->SpawnActor<ADemoViewReelTest>(); // Editor隔离金币UI分支，使用真实UMG按钮、Slate输入和保存。
    if (FParse::Param(FCommandLine::Get(), TEXT("DemoGambleTest"))) World->SpawnActor<ADemoGambleTest>(); // 仅本GI临时存档，不创建HTTP。
    if (FParse::Param(FCommandLine::Get(), TEXT("DemoCommonUITest"))) World->SpawnActor<ADemoCommonUITest>(); // 独立进程验证真实CommonUI栈/Slate输入，普通游戏不创建。
    if (FParse::Param(FCommandLine::Get(), TEXT("DemoSessionUITest"))) World->SpawnActor<ADemoSessionUITest>(); // 使用独立设备INI与每GI临时存档，生产无入口。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoDashNetworkTest"))) World->SpawnActor<ADemoDashNetworkTest>(); // 仅本次GI临时存档；客户端显式-DemoDashNetworkClient并正式JoinCoop。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoPlayerNetworkTest"))) World->SpawnActor<ADemoPlayerNetworkTest>(); // 仅Editor临时进度运行；正式HostSlot旅行后重新创建，远端不生成权威测试Actor。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoPlayerEyesCapture"))) World->SpawnActor<ADemoPlayerEyesTest>(); // 仅Editor显式六方向近景采样，普通游戏不执行测试相机/输入。
#endif
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoArmoryTest"))) World->SpawnActor<ADemoArmoryTest>(); // 非Shipping显式隔离回归，不影响正常游玩。
	// 三难度/十等级独立回归入口，普通游戏及 Shipping 不自动执行。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoCampaignTest"))) World->SpawnActor<ADemoCampaignTest>();
	// 新敌人攻击测试独立进程运行，避免与完整战役测试同时驱动World。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoProjectileTest"))) World->SpawnActor<ADemoProjectileTest>(); // 显式隔离的真实飞行/碰撞专项。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoAmmoTest"))) World->SpawnActor<ADemoAmmoTest>(); // 显式隔离回归入口。
	if (FParse::Param(FCommandLine::Get(), TEXT("DemoEnemyAttackTest"))) World->SpawnActor<ADemoEnemyAttackTest>();
#endif
}
