// 职责：Editor单人UnlockAll的菜单投影、真实终端装备及持久库存隔离回归；不伪造金币、Legacy/Paid来源或购买成功。
// 使用：独立UnrealEditor FPSDemo.uproject /Game/Whitebox/Maps/L_ThreeSector_Whitebox -game -DemoEditorWeaponUnlockTest
//       -GameUserSettingsINI="本次独占INI" -ExecCmds="Automation RunTests FPSDemo.Debug.EditorWeaponUnlock" -TestExit="Automation Test Queue Empty"。
// 依赖：生产CreateSlot/StartRun/OnRunReady、真实控制台UnlockAll、Shop、武器Actor和严格V10保存；最长30秒，无PIE/多人/HTTP或资产写入。
// 边界：三枪临时实装及弹量使用真实生产入口；末尾只测首购准备快照包含借用Actor，随后Abort，不把该准备合同称为真实金币购买验收。
#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Characters/DemoCharacter.h"
#include "Debug/DemoStaticLog.h"
#include "Economy/DemoShopComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/DemoGameState.h"
#include "Game/FPSDemoGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Interaction/DemoInteractable.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Player/DemoPlayerState.h"
#include "Save/DemoRunSave.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/DemoWeaponComponent.h"

namespace
{
    /** 仅寻找本显式独立游戏进程的Game World；不领取Editor/PIE世界，也不启动新World。 */
    UWorld* FindEditorUnlockWorld()
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        if (!GEngine) return nullptr;
        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            UWorld* World=Context.World(); // 引擎拥有，调用者只在当前GT更新同步借用。
            if (World && World->WorldType==EWorldType::Game && !World->bIsTearingDown && World->GetNetMode()==NM_Standalone) return World;
        }
        return nullptr;
    }

    /** 只读领域库存，按稳定ID定位唯一项；不以活体Actor或菜单高亮推断已入检查点。 */
    const FDemoWeaponSnapshot* FindCapturedWeapon(const FDemoLoadoutSnapshot& Loadout,FName WeaponId)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        for (const FDemoWeaponSnapshot& Weapon : Loadout.Weapons) if (Weapon.Id==WeaponId) return &Weapon;
        return nullptr;
    }

    /** 在新GI真实游戏中执行有限阶段；Automation拥有本对象，所有UObject跨帧仅弱引用。 */
    class FEditorWeaponUnlockLatent final : public IAutomationLatentCommand
    {
    public:
        /** InTest由Automation持有至latent队列结束；仅借用断言接收器，不延长任何游戏对象寿命。 */
        explicit FEditorWeaponUnlockLatent(FAutomationTestBase& InTest) : Test(InTest),Started(FPlatformTime::Seconds())
        { DEMO_LOG_STATIC_CALL(nullptr); }

        /** GT轮询真实初始化，30秒超时即失败；完成业务检查后true出队，不自动重试失败装备或购买。 */
        virtual bool Update() override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            if (FPlatformTime::Seconds()-Started>30.) { Check(false,TEXT("production standalone dependencies or Hub did not become ready within 30 seconds")); return true; }
            UWorld* World=FindEditorUnlockWorld(); // 本帧同步借用；不在latent成员里保留裸World。
            if (!World || World->GetTimeSeconds()<1.f) return false;
            if (Stage>0 && Stage<3 && TestWorld.Get()!=World) { Check(false,TEXT("test World changed before restart")); return true; }
            UGameInstance* Instance=World->GetGameInstance(); // 当前游戏GI所有服务采用同一所有者，拒绝跨PIE/GI借用。
            ADemoPlayerController* PC=Instance?Cast<ADemoPlayerController>(Instance->GetFirstLocalPlayerController()):nullptr;
            ADemoCharacter* Player=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr;
            ADemoPlayerState* PS=PC?PC->GetPlayerState<ADemoPlayerState>():nullptr;
            AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>();
            ADemoGameState* State=World->GetGameState<ADemoGameState>();
            UDemoRunSaves* Saves=Instance?Instance->GetSubsystem<UDemoRunSaves>():nullptr;
            UDemoPlayerProfile* Profile=Instance?Instance->GetSubsystem<UDemoPlayerProfile>():nullptr;
            UDemoMenuFlowComponent* Flow=PC?PC->GetMenuFlow():nullptr;
            UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr;
            UDemoWeaponComponent* Inventory=Player?Player->GetWeaponComponent():nullptr;
            if (!PC || !Player || !Mode || !State || !Saves || !Profile || !Flow || !Progress || !Inventory || !Player->GetDemoAttributes() || !Inventory->GetActiveWeapon()) return false;
            if (!Check(PC->HasAuthority() && PC->IsLocalController() && Player->IsLocallyControlled(),TEXT("fixture uses the actual local authoritative Pawn"))) return true;
            if (Stage>=3)
            {
                if (World==TestWorld.Get() || State->Phase!=EDemoPhase::Hub) return false; // 等真实 OpenLevel、恢复和新 Pawn。
                const int32 WantedSlot=Stage==3?1:2;
                if (!Check(Inventory->GetPrimaryIndex()==BorrowedIndex && Inventory->GetActiveSlot()==WantedSlot
                    && Inventory->GetActiveWeapon()->GetWeaponId()==(WantedSlot==1?DemoWeaponCatalog::IdAt(BorrowedIndex+1):FName(TEXT("pistol"))),
                    FString::Printf(TEXT("new Hub Pawn retains borrowed primary and held slot %d"),WantedSlot))) return true;
                FDemoLoadoutSnapshot Captured; Inventory->CaptureSnapshot(Captured);
                if (!Check(Captured.PrimaryId.IsNone() && Captured.Weapons.Num()==1 && Progress->GetSnapshot().Ownership.Entries.IsEmpty(),
                    TEXT("borrowed restart does not grant ownership or modify durable inventory"))) return true;
                if (Stage==4)
                {
                    if (!Check(Inventory->EquipSlot(1) && Inventory->GetActiveWeapon()->GetWeaponId()==TEXT("rifle"),
                        TEXT("slot 1 switches from the restored pistol to the actual borrowed rifle"))) return true;
                    UE_LOG(LogFPSDemo,Display,TEXT("EDITOR_WEAPON_UNLOCK_TEST_SUCCESS including both rifle death restart slots")); return true;
                }
                if (!Check(Inventory->EquipSlot(2),TEXT("switch to pistol while retaining borrowed primary"))) return true;
                TestWorld=World; Stage=4; Mode->NotifyPlayerDied(); Mode->RestartDemo(); return false;
            }

            if (Stage==0)
            {
                Flow->UpdateRealtime(0.f);
                if (Flow->GetMenuPage()==EDemoMenuPage::FirstRunGuide) { Flow->CloseFirstRunGuide(); return false; } // 只通过正常已读入口关闭，启动器必须提供本次独占设备INI。
                if (!Check(State->Phase==EDemoPhase::Lobby && Saves->GetActiveSlot()==INDEX_NONE && !Saves->SlotExists(0) && !Profile->IsDebugUnlockAllForSession(),TEXT("isolated fresh GI has no active slot or preexisting debug permission"))) return true;
                if (!Check(!Flow->HasBlockingOverlay() && Saves->CreateSlot(0) && Mode->StartRun(),TEXT("production slot creation and StartRun initialize a fresh Hub"))) return true;
                Saves->ConsumePending(); PC->OnRunReady(); // 沿用R02S1既有夹具初始化顺序，不直接改Phase、来源或钱包。
                TestWorld=World; Stage=1; return false;
            }

            if (!Mode->GetShopTerminal() || State->Phase!=EDemoPhase::Hub) return false;
            if (Stage==1)
            {
                Player->SetActorLocation(Mode->GetShopTerminal()->GetActorLocation()+FVector(100,0,110)); // 本人实际终端范围内，厘米；不移动终端或改代数。
                Player->GetCharacterMovement()->StopMovementImmediately();
                Mode->GetShopTerminal()->Interact(Player); // 真实PC BeginInteraction及服务器会话，UI页面本身不授予交易权。
                if (!Check(Flow->IsUpgradeMenuOpen() && Mode->GetShopSystem()->GetTerminalBlockReason(Player).IsEmpty(),TEXT("actual shop terminal grants a valid owner session"))) return true;
                BeforeProgress=Progress->GetSnapshot(); BeforeSaveSerial=Saves->GetChangeSerial(); // 从生产新槽初始化完成后冻结，不把初始化保存算作调试副作用。
                BeforeProfileRevision=Profile->GetData().LocalRevision;
                if (!Check(BeforeProgress.Ownership.Validate(true) && BeforeProgress.Ownership.Entries.IsEmpty() && BeforeProgress.UnlockedWeaponIds.Num()==1 && BeforeProgress.UnlockedWeaponIds.Contains(TEXT("pistol")),TEXT("fresh slot owns only pistol through the real frozen ownership ledger"))) return true;
                const FDemoCommandResult Locked=Mode->GetShopSystem()->EquipPrimaryWeapon(TEXT("rifle"),Player); // 真实入口的未授权基线，不伪造客户端状态。
                if (!Check(!Locked.IsApplied() && Locked.Code==TEXT("WeaponNotOwned") && Inventory->GetPrimaryIndex()==INDEX_NONE,TEXT("same terminal rejects a locked rifle before the debug command"))) return true;
                PC->ConsoleCommand(TEXT("Demo.Debug.UnlockAll"),true); // 使用实际注册命令，覆盖RefreshView是否真实发布PC菜单快照。
                Stage=2; return false;
            }

            if (!Check(Profile->IsDebugUnlockAllForSession(),TEXT("registered UnlockAll command enables this GI runtime override"))) return true;
            for (int32 Index=0;Index<DemoWeaponCatalog::Num();++Index)
            {
                const FName Id=DemoWeaponCatalog::IdAt(Index); // 同一稳定目录，不复制四枪名字/索引规则到测试。
                if (!Check(PC->GetMenuSnapshot().Progress.UnlockedWeaponIds.Contains(Id) && Progress->IsWeaponUnlocked(Id),TEXT("owner menu projection and runtime access include ")+Id.ToString())) return true;
            }
            if (!CheckUnchanged(*Progress,*Saves,*Profile,TEXT("UnlockAll and its menu refresh"))) return true;
            ADemoWeaponBase* BorrowedRifle=nullptr; // 本次同步断言栈借用，Pawn库存拥有；后续准备/Abort应继续保留同一Actor。
            int32 RifleAmmoAfterSpend=0,RifleReserve=0; // 直接冻结真实扣弹后的发数，避免用Config推算运行时初始化供弹。
            for (int32 Index=1;Index<DemoWeaponCatalog::Num();++Index)
            {
                const FName Id=DemoWeaponCatalog::IdAt(Index); // 每枪依次通过真实终端Shop入口，未Owned走明确临时分支。
                const FDemoCommandResult Result=Mode->GetShopSystem()->EquipPrimaryWeapon(Id,Player);
                ADemoWeaponBase* Actual=Inventory->GetActiveWeapon(); // 读真实装备Actor，不能以Result/菜单自己证明装备成功。
                if (!Check(Result.IsApplied() && Result.Status==EDemoCommandStatus::AppliedPendingSave && Result.Code==TEXT("EditorTemporaryEquipment") && Actual && Actual->GetWeaponId()==Id && Inventory->GetActiveSlot()==1 && Inventory->GetPrimaryIndex()==Index-1,TEXT("temporary terminal equip installs actual ")+Id.ToString())) return true;
                const int32 BeforeAmmo=Actual->GetAmmo(),BeforeReserve=Actual->GetReserveAmmo(); // 单位发；仅此隔离夹具移除一发，不要求Hub枪械伤害/开火许可。
                if (!Check(BeforeAmmo>0,TEXT("temporary actor has its normal initial supply: ")+Id.ToString())) return true;
                Actual->ModifyAmmo(-1);
                if (!Check(Actual->GetAmmo()==BeforeAmmo-1,TEXT("fixture removes exactly one round from actual ")+Id.ToString())) return true;
                const FDemoCommandResult Repeated=Mode->GetShopSystem()->EquipPrimaryWeapon(Id,Player);
                if (!Check(Repeated.IsApplied() && Repeated.Status==EDemoCommandStatus::AppliedPendingSave && Repeated.Code==TEXT("EditorTemporaryEquipment") && Inventory->GetActiveWeapon()==Actual && Actual->GetAmmo()==BeforeAmmo-1 && Actual->GetReserveAmmo()==BeforeReserve,TEXT("repeated temporary equip retains same Actor and spent ammo: ")+Id.ToString())) return true;
                if (Id==TEXT("rifle")) { BorrowedRifle=Actual; RifleAmmoAfterSpend=Actual->GetAmmo(); RifleReserve=Actual->GetReserveAmmo(); }
                FDemoLoadoutSnapshot Captured; Inventory->CaptureSnapshot(Captured); // 只读正常保存入口实际输出，不手动过滤测试结果。
                if (!Check(Captured.PrimaryId.IsNone() && Captured.ActiveSlot==2 && Captured.Weapons.Num()==1 && FindCapturedWeapon(Captured,TEXT("pistol")) && !FindCapturedWeapon(Captured,Id),TEXT("normal capture excludes temporary guns and safely selects pistol: ")+Id.ToString())) return true;
                if (!CheckUnchanged(*Progress,*Saves,*Profile,TEXT("temporary and repeated equip: ")+Id.ToString())) return true;
            }

            const bool bPrepared=Inventory->PrepareShopSelection(TEXT("rifle"),true); // 仅生产首购准备合同：复用刚才的临时Actor，不提交来源/收据或金币。
            FDemoLoadoutSnapshot PurchaseTarget; // 候选快照在Abort前独立拷贝，后续不借用被清理的ShopTarget内部引用。
            if (bPrepared) Inventory->CaptureSnapshot(PurchaseTarget);
            Inventory->AbortShopSelection(); // 即使断言失败也先撤销准备，未创建的借用Actor及其弹量保持。
            const FDemoWeaponSnapshot* Rifle=FindCapturedWeapon(PurchaseTarget,TEXT("rifle"));
            if (!Check(bPrepared && IsValid(BorrowedRifle) && Rifle && Rifle->Ammo==RifleAmmoAfterSpend && Rifle->Reserve==RifleReserve && BorrowedRifle->GetAmmo()==RifleAmmoAfterSpend && PurchaseTarget.Weapons.Num()==2 && PurchaseTarget.PrimaryId==TEXT("rifle") && PurchaseTarget.ActiveSlot==1,TEXT("first-purchase preparation includes the borrowed rifle and its spent ammo in target snapshot"))) return true;
            if (!CheckUnchanged(*Progress,*Saves,*Profile,TEXT("purchase preparation followed by Abort"))) return true;
            FDemoLoadoutSnapshot AfterAbort; Inventory->CaptureSnapshot(AfterAbort);
            if (!Check(AfterAbort.PrimaryId.IsNone() && AfterAbort.ActiveSlot==2 && AfterAbort.Weapons.Num()==1 && FindCapturedWeapon(AfterAbort,TEXT("pistol")),TEXT("Abort restores normal capture isolation without deleting test inventory"))) return true;
            FDemoLoadoutSnapshot ResetCapture; Inventory->CaptureSnapshot(ResetCapture,true); // 死亡/放弃补给捕获也不能把调试枪变成持久库存。
            if (!Check(ResetCapture.PrimaryId.IsNone() && ResetCapture.ActiveSlot==2 && ResetCapture.Weapons.Num()==1 && FindCapturedWeapon(ResetCapture,TEXT("pistol")),TEXT("reset-ammo checkpoint capture also excludes all temporary guns"))) return true;
            if (!Check(Mode->SaveCheckpoint() && Saves->GetChangeSerial()>BeforeSaveSerial,TEXT("explicit normal checkpoint save still passes strict V10 validation"))) return true;
            const UDemoRunSave* Saved=Saves->GetSlot(Saves->GetActiveSlot()); // 从真实Saves缓存读完整已保存值，不直接NewObject拼一份期望存档。
            if (!Check(Saved && Saved->Validate() && Saved->Ownership.Entries.IsEmpty() && Saved->PrimaryId.IsNone() && Saved->ActiveSlot==2 && Saved->Weapons.Num()==1 && Saved->Weapons[0].Id==TEXT("pistol"),TEXT("saved V10 checkpoint contains only genuine ownership and pistol inventory"))) return true;
            if (!Check(FDemoPlayerProgressSnapshot::StaticStruct()->CompareScriptStruct(&BeforeProgress,&Progress->GetSnapshot(),0),TEXT("explicit checkpoint save still does not grant permanent debug weapons"))) return true;
            if (!Check(Mode->GetShopSystem()->EquipPrimaryWeapon(TEXT("rifle"),Player).IsApplied(),TEXT("select reported rifle before death"))) return true;
            Flow->CloseUpgradeMenu();
            BorrowedIndex=Inventory->GetPrimaryIndex(); Stage=3; // 从死亡结算入口触发真实重开，不伪造磁盘库存。
            Mode->NotifyPlayerDied(); Mode->RestartDemo(); return false;
        }

    private:
        /** Value为生产读数断言，Message为可检索范围；失败保留Automation错误，不退出他人的Editor进程。 */
        bool Check(bool Value,const FString& Message)
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            Test.TestTrue(Message,Value);
            UE_LOG(LogFPSDemo,Display,TEXT("EDITOR_WEAPON_UNLOCK_TEST %s %s"),Value?TEXT("PASS"):TEXT("FAIL"),*Message);
            return Value;
        }

        /** 三个服务均为当前World/GI同步借用；验证运行期授权/装备不更改个人账本、Profile修订或本地保存水位。 */
        bool CheckUnchanged(const UDemoPlayerProgressComponent& Progress,const UDemoRunSaves& Saves,const UDemoPlayerProfile& Profile,const FString& Context)
        {
            DEMO_LOG_STATIC_CALL(nullptr);
            return Check(FDemoPlayerProgressSnapshot::StaticStruct()->CompareScriptStruct(&BeforeProgress,&Progress.GetSnapshot(),0) && Saves.GetChangeSerial()==BeforeSaveSerial && Profile.GetData().LocalRevision==BeforeProfileRevision,TEXT("unchanged persistent ledger, source receipts and save serial after ")+Context);
        }

        FAutomationTestBase& Test; // Automation拥有，生命周期覆盖此latent命令；只接收本测试断言。
        const double Started; // 单调真实秒，全部初始化/阶段合计最多30秒，不依赖暂停World时钟。
        TWeakObjectPtr<UWorld> TestWorld; // 新槽初始化后冻结的真实World，旅行/替换会拒绝继续。
        FDemoPlayerProgressSnapshot BeforeProgress; // 新槽初始化后的真实完整账本副本，包含来源/收据/钱包/修订。
        uint64 BeforeSaveSerial=0; // 正常初始化后本地持久水位；只有末尾明确SaveCheckpoint允许推进。
        int32 BeforeProfileRevision=0; // 调试不允许伪造账号事实或推进Profile修订。
        int32 Stage=0; // 0初始化、1合法终端及真实命令、2有限同步断言；失败不重复业务动作。
        int32 BorrowedIndex=INDEX_NONE; // 跨两个真实 World 的值断言，不持有旧武器 Actor。
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDemoEditorWeaponUnlockTest,"FPSDemo.Debug.EditorWeaponUnlock",
    EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

/** Parameters由Automation提供而不作文件路径；仅显式独立Editor游戏及独占设备INI允许创建本GI临时槽。 */
bool FDemoEditorWeaponUnlockTest::RunTest(const FString& Parameters)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    FString SettingsINI; // 启动器提供本次独占设备设置路径，CloseFirstRunGuide不会写用户正常已读设置。
    if (!FParse::Param(FCommandLine::Get(),TEXT("game")) || !FParse::Param(FCommandLine::Get(),TEXT("DemoEditorWeaponUnlockTest")) || !FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),SettingsINI) || SettingsINI.IsEmpty())
    {
        AddError(TEXT("Requires a fresh independent Editor -game -DemoEditorWeaponUnlockTest process with an isolated -GameUserSettingsINI path; do not run in an ordinary Editor/PIE session."));
        return false;
    }
    ADD_LATENT_AUTOMATION_COMMAND(FEditorWeaponUnlockLatent(*this));
    return true;
}
#endif
