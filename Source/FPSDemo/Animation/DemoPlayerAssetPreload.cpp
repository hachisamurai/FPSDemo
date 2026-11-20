// 职责：实现玩家依赖加载的GI生命周期；与Pawn展示共用路径，避免连接之后才首次同步加载大型动画库。
#include "Animation/DemoPlayerAssetPreload.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Debug/DemoLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#if WITH_EDITOR
FSoftObjectPath DemoAppearanceAssets::EditorOverridePath()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoAppearanceAssets::EditorOverridePath"));
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoStableOwnerLowerPoseCandidate")))
        return FSoftObjectPath(TEXT("/Game/Player/StableView20261114/A07/DA_PlayerAppearance_OwnerFP_A07.DA_PlayerAppearance_OwnerFP_A07")); // 独立下身窄图候选，原A05网格/A06FP深度不改；只在Editor创建前选择。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoStableOwnerFPDepthCandidate")))
        return FSoftObjectPath(TEXT("/Game/Player/StableView20261114/A06/DA_PlayerAppearance_OwnerFP_A06.DA_PlayerAppearance_OwnerFP_A06")); // 独立同FOV深度候选，复用A05闭合下身；A05失败证据不覆盖，非Editor无此入口。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoOwnerFPBallisticsCandidate"))
        && FParse::Param(FCommandLine::Get(),TEXT("DemoProjectileTest")) && FParse::Param(FCommandLine::Get(),TEXT("DemoProjectilePresentationAudit")))
        return FSoftObjectPath(TEXT("/Game/Tests/OwnerFP20261114/DA_OwnerFPBallisticsAudit.DA_OwnerFPBallisticsAudit")); // 仅既有显式Editor弹道夹具：用已导入下身作占位，不能把此资产误当下身艺术候选或正式外观。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoStableOwnerFPCandidate")))
        return FSoftObjectPath(TEXT("/Game/Player/StableView20261114/A05/DA_PlayerAppearance_OwnerFP_A05.DA_PlayerAppearance_OwnerFP_A05")); // 仅Editor白名单候选，GI/Pawn在创建前共同选择，避免运行中切策略遗留递归Hidden标记；不保存正式外观。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowR07")))
        return FSoftObjectPath(TEXT("/Game/Player/Candidates/GunHoldElbow20261109/R07JoinedComplete/DA_PlayerAppearance.DA_PlayerAppearance")); // 独立完整源/实际Hold参考候选；仅源审门及导入通过后用于Editor，不改变正式或打包路径。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowAimOnly")))
        return FSoftObjectPath(TEXT("/Game/Player/Candidates/GunHoldElbow20261109/R05AimOnlyReuseOperations/DA_PlayerAppearance.DA_PlayerAppearance")); // 最窄对照仅替换方向库，所有原操作/后坐/机械引用保留，不能混称整套R05新动作。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoGunElbowCandidate")))
        return FSoftObjectPath(TEXT("/Game/Player/Candidates/GunHoldElbow20261109/R05ActualAttachment/DA_PlayerAppearance.DA_PlayerAppearance")); // 仅本地Editor长枪候选，与Pawn消费同一路径，不改变正式资产或包体选择。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoFireRecoilCandidate")))
        return FSoftObjectPath(TEXT("/Game/Player/Candidates/FireRecoil20261108/DA_PlayerAppearance.DA_PlayerAppearance")); // 固定两枪候选闭包；仅Editor，不能由联网请求拼接资产路径。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoFullBodyCandidate")))
        return FSoftObjectPath(TEXT("/Game/Player/FullBodyView20261029/DA_PlayerAppearance_Candidate.DA_PlayerAppearance_Candidate")); // 渲染架构隔离验收；打包由正式Appearance数据决定。
    // 独立低头回归候选；仅Editor命令行可选，正式引用在验收后单独切换。
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoLowAimCandidate")))
        return FSoftObjectPath(TEXT("/Game/Player/AuthoredAimLowV2/DA_PlayerAppearance_Candidate.DA_PlayerAppearance_Candidate"));
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoAuthoredAimCandidate")))
        return FSoftObjectPath(TEXT("/Game/Player/AuthoredAim/DA_PlayerAppearance_Candidate.DA_PlayerAppearance_Candidate"));
    if(FParse::Param(FCommandLine::Get(),TEXT("DemoLyraNativeCandidate")))
        return FSoftObjectPath(TEXT("/Game/Player/LyraNative/DA_PlayerAppearance_Candidate.DA_PlayerAppearance_Candidate"));
    return FSoftObjectPath(); // 没有候选时保留Pawn的可配置外观；不把默认正式路径误作覆盖命令。
}
#endif

FSoftObjectPath DemoAppearanceAssets::DefaultPath()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoAppearanceAssets::DefaultPath"));
#if WITH_EDITOR
    const FSoftObjectPath Override=EditorOverridePath(); // GI与Pawn共用同一白名单解析，避免只预载候选而实际使用正式。
    if(Override.IsValid())return Override;
#endif
    return FSoftObjectPath(TEXT("/Game/Player/DA_PlayerAppearance.DA_PlayerAppearance"));
}

void UDemoPlayerAssetPreload::Initialize(FSubsystemCollectionBase& Collection)
{
    DEMO_LOG_CALL(); Super::Initialize(Collection);
    const double Started=FPlatformTime::Seconds(); // 诊断用墙钟，不作为技能/动画时钟。
    const TSoftObjectPtr<UDemoPlayerAppearance> Asset(DemoAppearanceAssets::DefaultPath()); // 默认资源白名单，不从玩家网络请求拼接路径。
    Appearance=Asset.LoadSynchronous();
    if(Appearance) { UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_ASSET_PRELOAD_READY asset=%s seconds=%.3f"),*Asset.ToString(),FPlatformTime::Seconds()-Started); }
    else { UE_LOG(LogFPSDemo,Warning,TEXT("PLAYER_ASSET_PRELOAD_MISSING asset=%s"),*Asset.ToString()); }
}

void UDemoPlayerAssetPreload::Deinitialize()
{
    DEMO_LOG_CALL(); Appearance=nullptr; Super::Deinitialize();
}
