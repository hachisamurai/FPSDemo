// 职责：Editor专用控制台调试命令。使用：WITH_EDITOR且本地单人试玩才注册，天赋列表来自可信目录，包体不含授权入口。
// 依赖GI Profile持有会话权限、PS Progress提交及PC菜单投影；整个注册和回调仅在Editor编译，包体不含授权入口。
#if WITH_EDITOR
#include "Debug/DemoLog.h"
#include "Debug/DemoEditorCheats.h" // 所有注册/回调均在WITH_EDITOR内，包体没有调试命令字符串。
#include "Player/DemoPlayerController.h" // 开关改变后刷新本机权威报价，不要求玩家重新交互。
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoPlayerState.h"
#include "Player/DemoPlayerProgressComponent.h"
#include "Economy/Gambling/DemoGambleSettings.h" // 命令复用正式目录/容量验证，不生成任意GameplayTag。
#include "Weapons/DemoWeaponCatalog.h"
#include "Game/FPSDemoGameMode.h" // AddGold/AddSilver在安全区复用正式检查点保存。
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "HAL/IConsoleManager.h"

namespace
{
    /** World来自控制台，Output为同步输出；只返回当前单人试玩GI，拒绝编辑器预览及联机World。 */
    UDemoPlayerProfile* TestProfile(UWorld* World,FOutputDevice& Output)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::TestProfile"));
        UGameInstance* Instance=World?World->GetGameInstance():nullptr; // 命令World所属GI，不用全局GWorld。
        if(!World||!World->IsGameWorld()||World->GetNetMode()!=NM_Standalone||!Instance)
        { Output.Log(TEXT("测试命令仅限单人编辑器试玩；联机与非游戏World不可使用。"));UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_TEST rejected world/net mode"));return nullptr; }
        return Instance->GetSubsystem<UDemoPlayerProfile>();
    }
    /** Args=list/clear或若干稳定ID[:等级]（缺省1级）；World/Output仅本次游戏线程借用，不改币或触发云请求。 */
    void SetTalents(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::SetTalents"));
        if(!TestProfile(World,Output)) return;
        if(Args.IsEmpty()||(Args.Num()==1&&Args[0]==TEXT("list")))
        {
            for(int32 Index=0;Index<DemoGamble::TalentCount();++Index) // 输出完整白名单与配置收益，便于手动组合测试。
                Output.Logf(TEXT("%s : %s - %s"),*DemoGamble::Talent(Index).ToString(),*DemoGamble::Name(DemoGamble::Talent(Index)),*DemoGamble::DescribeTalent(DemoGamble::Talent(Index)));
            Output.Log(TEXT("Demo.Debug.Talents overdrive:2 twin:3 siphon | Demo.Debug.Talents clear")); return;
        }
        APlayerController* PC=World->GetGameInstance()->GetFirstLocalPlayerController(); // 仅当前GI拥有者。
        ADemoPlayerState* PS=PC?PC->GetPlayerState<ADemoPlayerState>():nullptr; // 当前试玩PS拥有账本。
        if(!PS||!PS->GetProgressComponent()||!GetDefault<UDemoGambleSettings>()->Validate())
        { Output.Log(TEXT("当前玩家/配置尚未就绪。")); UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_TALENTS invalid context")); return; }
        FDemoPlayerProgressSnapshot Next=PS->GetProgressComponent()->GetSnapshot(); // 原子候选，非法输入不清原天赋。
        Next.Gambling.Talents.Reset(); Next.Gambling.TalentRanks.Reset(); // 替换整个调试组合，非法输入不会提交。
        if(!(Args.Num()==1&&Args[0]==TEXT("clear")))
            for(const FString& Arg:Args) // 参数限当前同步栈；解析必须完整成功，不能把abc截断成0。
            {
                FString Id=Arg,LevelText; // 可选冒号后的等级；ID保持原白名单校验。
                int32 Level=1; // 未提供等级时与旧命令兼容。
                if(Arg.Split(TEXT(":"),&Id,&LevelText))
                {
                    bool bDigits=!LevelText.IsEmpty()&&LevelText.Len()<=10; // int32最多10位；禁止小数、符号与指数被截断解析。
                    for(TCHAR Digit:LevelText) bDigits&=Digit>=TEXT('0')&&Digit<=TEXT('9'); // 同步遍历参数，不持有外部状态。
                    int64 Parsed=0; // 先宽类型解析，避免4294967297回绕为1。
                    if(!bDigits||!LexTryParseString(Parsed,*LevelText)||Parsed<1||Parsed>MAX_int32)
                    { Output.Log(TEXT("等级必须为正int32整数，原天赋未改变。")); UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_TALENTS rejected rank")); return; }
                    Level=int32(Parsed);
                }
                Next.Gambling.Talents.Add(FName(*Id)); Next.Gambling.TalentRanks.Add(Level);
            }
        if(Next.Gambling.Serial==MAX_int32||!Next.Gambling.Validate())
        { Output.Log(TEXT("天赋ID无效、重复或等级非法；原天赋未改变。")); UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_TALENTS rejected ids/ranks/serial")); return; }
        ++Next.Gambling.Serial; // 使调试前的确认报价失效，不伪造抽取回执。
        if(!PS->GetProgressComponent()->Commit(Next)) { Output.Log(TEXT("提交失败，未应用。")); return; }
        if(ADemoPlayerController* DemoPC=Cast<ADemoPlayerController>(PC)) DemoPC->PublishMenuSnapshot(); // 账本提交重建正式GAS效果。
        Output.Log(TEXT("天赋组合已替换；只影响本次Editor试玩，死亡/通关清除。"));
    }
    FAutoConsoleCommandWithWorldArgsAndOutputDevice TalentsCommand(TEXT("Demo.Debug.Talents"),TEXT("Editor standalone only: list, clear, or talent IDs with optional :rank (e.g. twin:3 overdrive:2)."),FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&SetTalents)); // WITH_EDITOR整体隔离；模块卸载自动注销。
    /** Flag=0金币/1银币/2无敌；Args为空切换或单个0/1；World/Output只在本次游戏线程调用中借用。 */
    void SetTestFlag(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output,int32 Flag)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::SetTestFlag"));
        UDemoPlayerProfile* Profile=TestProfile(World,Output); // 无副作用预检，失败不写其他PIE实例。
        if(!Profile) return;
        if(Args.Num()>1||(Args.Num()==1&&Args[0]!=TEXT("0")&&Args[0]!=TEXT("1")))
        { Output.Log(TEXT("参数：1 开启，0 关闭；不带参数切换。"));UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_TEST rejected arguments"));return; }
        const bool bEnable=Args.IsEmpty()?!Profile->IsDebugTestFlagEnabled(Flag):Args[0]==TEXT("1"); // 无歧义布尔解析，不把非法字符串当0。
        Profile->SetDebugTestFlag(Flag,bEnable);
        if(ADemoPlayerController* PC=Cast<ADemoPlayerController>(World->GetGameInstance()->GetFirstLocalPlayerController())) PC->PublishMenuSnapshot(); // 只刷新报价，不保存开关/修改余额。
        Output.Logf(TEXT("%s：%s。仅本次单人编辑器试玩有效。"),Flag==0?TEXT("无限金币"):Flag==1?TEXT("无限银币"):TEXT("无敌"),bEnable?TEXT("开启"):TEXT("关闭"));
    }
    /** 三个控制台委托：Args/World/Output为引擎当前同步上下文，无对象捕获或跨线程生命周期。 */
    void InfiniteGold(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::InfiniteGold"));SetTestFlag(Args,World,Output,0); }
    void InfiniteSilver(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::InfiniteSilver"));SetTestFlag(Args,World,Output,1); }
    void God(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::God"));SetTestFlag(Args,World,Output,2); }
    /** Args须为空；统一关闭三项测试覆盖，不扣回测试收益，不重新锁定UnlockAll已授予的试玩权限。 */
    void ResetCheats(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::ResetCheats"));
        UDemoPlayerProfile* Profile=TestProfile(World,Output); // 同一GI所有权预检。
        if(!Profile||!Args.IsEmpty()){UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_TEST reset rejected context/arguments"));return;}
        for(int32 Flag=0;Flag<3;++Flag) Profile->SetDebugTestFlag(Flag,false); // 三个覆盖全部清零，不修改存档/解锁。
        if(ADemoPlayerController* PC=Cast<ADemoPlayerController>(World->GetGameInstance()->GetFirstLocalPlayerController())) PC->PublishMenuSnapshot();
        Output.Log(TEXT("无限金币、无限银币和无敌已全部关闭。"));
    }
    /** Args须为空；输出当前三个开关，不改变游戏状态。 */
    void TestStatus(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::TestStatus"));
        if(!TestProfile(World,Output)||!Args.IsEmpty()){UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_TEST status rejected context/arguments"));return;}
        const FString Message=DemoEditorCheats::Status(World); // 当前GI只读显示，无凭据。
        Output.Log(Message.IsEmpty()?TEXT("三个测试开关均已关闭。"):*Message);
    }
    FAutoConsoleCommandWithWorldArgsAndOutputDevice InfiniteGoldCommand(TEXT("Demo.Debug.InfiniteGold"),TEXT("Editor standalone only: 1 on, 0 off, no argument toggles unlimited gold purchases."),FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&InfiniteGold)); // 模块静态注册/自动注销，无异步捕获。
    FAutoConsoleCommandWithWorldArgsAndOutputDevice InfiniteSilverCommand(TEXT("Demo.Debug.InfiniteSilver"),TEXT("Editor standalone only: 1 on, 0 off, no argument toggles unlimited silver purchases."),FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&InfiniteSilver)); // 同上；实际余额不伪造。
    FAutoConsoleCommandWithWorldArgsAndOutputDevice GodCommand(TEXT("Demo.Debug.God"),TEXT("Editor standalone only: 1 on, 0 off, no argument toggles player damage immunity."),FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&God)); // 只保护本机玩家伤害，不保护敌人。
    FAutoConsoleCommandWithWorldArgsAndOutputDevice ResetCheatsCommand(TEXT("Demo.Debug.ResetCheats"),TEXT("Editor standalone only: disable infinite gold, infinite silver and god mode."),FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ResetCheats)); // 不删除测试进度/解锁。
    FAutoConsoleCommandWithWorldArgsAndOutputDevice TestStatusCommand(TEXT("Demo.Debug.Status"),TEXT("Editor standalone only: show test flags."),FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&TestStatus)); // 只读诊断。
    /**
     * 2026-11-19用户要求“没有增加金币的命令就加一条”：向本机玩家真实账本加金币(bGold)或银币。
     * Args须为单个正整数1..100000000；World/Output只在本次同步调用借用。走正式Progress->Commit（与购买同一校验）并刷新菜单，
     * 安全区再调用GameMode::SaveCheckpoint（战斗中该函数自身拒绝写半场状态）。Editor试玩存档仍为本次GI临时数据，停止试玩清空；
     * 不改变InfiniteGold/Silver免扣开关、不触发云同步、非Editor包体不编译。金币上限int32，银币上限沿用账本100000000。
     */
    void AddCoins(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output,bool bGold)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::AddCoins gold=%d"),bGold);
        const TCHAR* Name=bGold?TEXT("金币"):TEXT("银币"); // 输出文案，与HUD币种一致。
        if(!TestProfile(World,Output)) return;
        int64 Amount=0; // 先宽类型解析，拒绝小数/符号/溢出回绕。
        bool bDigits=Args.Num()==1&&!Args[0].IsEmpty()&&Args[0].Len()<=9;
        if(bDigits) for(TCHAR Digit:Args[0]) bDigits&=Digit>=TEXT('0')&&Digit<=TEXT('9');
        if(!bDigits||!LexTryParseString(Amount,*Args[0])||Amount<1||Amount>100000000)
        { Output.Logf(TEXT("用法：%s <1~100000000的整数>，例如 %s 5000"),bGold?TEXT("Demo.Debug.AddGold"):TEXT("Demo.Debug.AddSilver"),bGold?TEXT("Demo.Debug.AddGold"):TEXT("Demo.Debug.AddSilver")); UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_ADD_COINS rejected arguments")); return; }
        ADemoPlayerController* PC=Cast<ADemoPlayerController>(World->GetGameInstance()->GetFirstLocalPlayerController()); // 当前GI本机玩家，不枚举其他PIE/队友。
        ADemoPlayerState* PS=PC?PC->GetPlayerState<ADemoPlayerState>():nullptr; // PS拥有个人账本。
        UDemoPlayerProgressComponent* Progress=PS?PS->GetProgressComponent():nullptr;
        if(!Progress||!PS->HasAuthority())
        { Output.Log(TEXT("玩家尚未就绪，请进入单人编辑器试玩后重试。")); UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_ADD_COINS player not ready")); return; }
        FDemoPlayerProgressSnapshot Next=Progress->GetSnapshot(); // 原子候选；提交失败不改原余额。
        int32& Wallet=bGold?Next.Coins:Next.SilverCoins; // 只改所选币种。
        const int64 Cap=bGold?int64(MAX_int32):int64(100000000); // 与账本Commit上限一致。
        const int32 Before=Wallet;
        Wallet=int32(FMath::Min<int64>(Cap,int64(Wallet)+Amount));
        if(!Progress->Commit(Next)) { Output.Logf(TEXT("账本提交失败，%s未改变。"),Name); UE_LOG(LogFPSDemo,Warning,TEXT("DEBUG_ADD_COINS commit rejected")); return; }
        PC->PublishMenuSnapshot(); // 已打开的终端/HUD立即刷新余额。
        AFPSDemoGameMode* Mode=World->GetAuthGameMode<AFPSDemoGameMode>(); // 单人Standalone本机即权威。
        const bool bSaved=Mode&&Mode->SaveCheckpoint(); // 安全区写本次试玩临时检查点；战斗中由SaveCheckpoint自身拒绝。
        UE_LOG(LogFPSDemo,Display,TEXT("DEBUG_ADD_COINS gold=%d amount=%lld before=%d after=%d saved=%d"),bGold,Amount,Before,Wallet,bSaved);
        Output.Logf(TEXT("%s +%lld：%d → %d。%s仅本次编辑器试玩有效，停止后清空。"),Name,Amount,Before,Wallet,
            bSaved?TEXT("检查点已保存；"):TEXT("当前不在安全区，未写检查点（回安全区后会随下次保存记录）；"));
    }
    /** 两个控制台委托：只转发币种，不持有对象。 */
    void AddGold(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::AddGold")); AddCoins(Args,World,Output,true); }
    void AddSilver(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoEditorCommands::AddSilver")); AddCoins(Args,World,Output,false); }
    FAutoConsoleCommandWithWorldArgsAndOutputDevice AddGoldCommand(TEXT("Demo.Debug.AddGold"),TEXT("Editor standalone only: add N gold (1..100000000) to the local player's real wallet for this play session."),FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&AddGold)); // 模块静态注册/自动注销。
    FAutoConsoleCommandWithWorldArgsAndOutputDevice AddSilverCommand(TEXT("Demo.Debug.AddSilver"),TEXT("Editor standalone only: add N silver (1..100000000) to the local player's real wallet for this play session."),FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&AddSilver)); // 同上。
    /** Args须为空；World/Output为本次同步控制台上下文。授予地狱/无尽临时资格并刷新当前菜单，不启动关卡。 */
    void UnlockAllModes(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoEditorCommands::UnlockAllModes"));
        UDemoPlayerProfile* Profile = TestProfile(World, Output); // 复用单人游戏World门禁，拒绝联机及编辑器预览。
        if (!Profile) return;
        if (!Args.IsEmpty())
        {
            UE_LOG(LogFPSDemo, Warning, TEXT("DEBUG_UNLOCK_MODES rejected arguments"));
            Output.Log(TEXT("Demo.Debug.UnlockAllModes 不带参数；仅解锁本次试玩的地狱和无尽模式。"));
            return;
        }
        ADemoPlayerController* Player = Cast<ADemoPlayerController>(World->GetGameInstance()->GetFirstLocalPlayerController()); // 当前GI本机玩家，同步借用。
        ADemoPlayerState* State = Player ? Player->GetPlayerState<ADemoPlayerState>() : nullptr; // 模式入口读取此玩家PS而非UI私有标记。
        UDemoPlayerProgressComponent* Progress = State ? State->GetProgressComponent() : nullptr; // PS拥有；未就绪时不授予半份权限。
        if (!Progress || !State->HasAuthority())
        {
            UE_LOG(LogFPSDemo, Warning, TEXT("DEBUG_UNLOCK_MODES rejected player not ready"));
            Output.Log(TEXT("玩家尚未就绪，请进入单人编辑器试玩后重试。"));
            return;
        }
        Profile->DebugUnlockAllModesForSession();
        const FDemoPlayerProgressSnapshot Current = Progress->GetSnapshot(); // 同步值副本，Commit叠加GI覆盖，不改两币、成长或挑战纪录。
        if (!Progress->Commit(Current))
        {
            UE_LOG(LogFPSDemo, Warning, TEXT("DEBUG_UNLOCK_MODES progress refresh failed"));
            Output.Log(TEXT("本次试玩模式权限已开启，但玩家账本刷新失败；请查看日志并重试。"));
            return;
        }
        Player->PublishMenuSnapshot(); // 已打开的难度选择立即解除禁用，无需关闭后再次交互。
        Output.Log(TEXT("已解锁地狱和无尽模式；请在安全区下一关终端选择。仅本次编辑器试玩有效，停止后清空。"));
    }
    FAutoConsoleCommandWithWorldArgsAndOutputDevice UnlockAllModesCommand( // 模块加载注册、卸载自动注销；同步静态委托，无捕获或异步对象寿命。
        TEXT("Demo.Debug.UnlockAllModes"), TEXT("Editor standalone only: unlock Hell and Endless modes for this play session; no arguments."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&UnlockAllModes));

    /** Args必须为空；World由当前控制台传入，不搜索其他PIE实例；Output只在同步调用内借用。 */
    void UnlockAll(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoEditorCommands::UnlockAll"));
        UGameInstance* Instance = World ? World->GetGameInstance() : nullptr; // 只处理命令所属试玩，不访问编辑器预览World或其他会话。
        UDemoPlayerProfile* Profile = Instance ? Instance->GetSubsystem<UDemoPlayerProfile>() : nullptr; // GI持有直到本次试玩停止。
        if (!Args.IsEmpty() || !World || !World->IsGameWorld() || World->GetNetMode() != NM_Standalone || !Profile)
        {
            UE_LOG(LogFPSDemo, Warning, TEXT("DEBUG_UNLOCK_ALL rejected: requires standalone editor game world and no arguments"));
            Output.Log(TEXT("Demo.Debug.UnlockAll: 请在单人编辑器试玩的游戏控制台输入，不带参数。"));
            return;
        }
        Profile->DebugUnlockAllForSession();
        // 新拥有权账本只接收真实来源；临时权限由GI保有，菜单用同一有效权限查询，不提交伪造的持久解锁缓存。
        ADemoPlayerController* Owner = Cast<ADemoPlayerController>(Instance->GetFirstLocalPlayerController()); // 当前GI本机控制器，不枚举其他PIE或队员。
        if (Owner) Owner->PublishMenuSnapshot(); // 只刷新本人装备/弹药投影，不推进进度Revision或触发保存。
        Output.Log(TEXT("已解锁全部枪械和弹药；前往终端装配。仅本次编辑器试玩有效，停止后清空。"));
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice UnlockAllCommand( // 模块静态注册，卸载时自动注销；静态函数无对象捕获和异步生命周期。
        TEXT("Demo.Debug.UnlockAll"), TEXT("Editor only: unlock all weapons and ammo for the current standalone play session."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&UnlockAll));
}
#endif
