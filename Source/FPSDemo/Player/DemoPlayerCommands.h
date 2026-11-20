// 职责：可靠业务请求与拥有者菜单投影。使用：客户端仅提交意图，服务器校验；所有进度投影必须移除货箱私有雷位。
#pragma once
#include "CoreMinimal.h"
#include "Contracts/DemoGameplayContracts.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Progression/Summary/DemoRunSummaryComponent.h"
#include "DemoPlayerCommands.generated.h"
class ADemoInteractable;

/** 拥有者PC可提交的业务意图；服务器从PC解析玩家，客户端不能提交钱包或成长值。 */
UENUM()
enum class EDemoGameplayCommand : uint8
{
    RefreshView, BeginInteraction, EndInteraction, ChooseReward, UpgradeStat, EquipWeapon,
    EquipAmmo, PurchaseAmmo, StartLevel, ReturnHub, RestartRun, SaveCheckpoint, ReturnLobby,
    SelectSaveSlot, CreateSave, Gamble, Casino, SelectCore, SetReady, SelectDeparture,
    CloseCasinoDevice // 只追加设备会话退出意图，旧命令序号保持不变；枪墙报价/购买走本人复制组件RPC。
};

/** 单个可靠RPC请求；索引、报价和终端仅为意图，服务端仍重新校验。 */
USTRUCT()
struct FPSDEMO_API FDemoGameplayCommandRequest
{
    GENERATED_BODY()
    UPROPERTY() EDemoGameplayCommand Command = EDemoGameplayCommand::RefreshView; // 固定白名单，不反射任意函数。
    UPROPERTY() int32 RequestId = 0; // 当前PC本地递增序号；服务器拒绝已处理的重放。
    UPROPERTY() int32 Choice = INDEX_NONE; // 属性0..2、武器/弹药0..3、存档0..2，按Command解释。
    UPROPERTY() int32 ExpectedPrice = INDEX_NONE; // 弹药确认报价；服务器与最新配置重新比较。
    UPROPERTY() int32 ReadyRevision = INDEX_NONE; // GS准备版本，换人/模式/阶段后拒绝旧意图。
    UPROPERTY() bool bReady = false; // 显式准备/取消，不用toggle避免重放反转。
    UPROPERTY() int32 BuildRevision = INDEX_NONE; // 核心/分支匹配本人选择版本，拒绝旧页重放。
    UPROPERTY() int32 ExpectedLevel = INDEX_NONE; // 防止第3关旧点击误领第7关分支。
    UPROPERTY() int32 GambleSerial = INDEX_NONE; // 赌博确认时的个人序号；抵御双击、重连和不同请求ID重放。
    UPROPERTY() FDemoCasinoIntent Casino; // 金币玩法意图；随机值和收益不允许客户端指定。
    UPROPERTY() EDemoDifficulty Difficulty = EDemoDifficulty::Normal; // Hub出发选择，不直接写GameState。
    UPROPERTY() bool bEndless = false; // Hub出发模式选择，权限从本人服务器进度读取。
    UPROPERTY() bool bConfirmed = false; // 明确放弃本轮的确认意图，不能跳过服务器阶段检查。
    UPROPERTY() TObjectPtr<ADemoInteractable> Terminal = nullptr; // 网络Actor引用，仅当前World会话可用。
    UPROPERTY() uint64 TerminalGeneration = 0; // 最近服务端回执的终端代数；0仅用于建立交互。
    UPROPERTY() FName CasinoDeviceId; // 设备页专属稳定ID，不伪造远处旧Shop终端。
    UPROPERTY() FGuid CasinoDeviceToken; // 服务端签发本人设备租约；旧租约不得继续下注。
    UPROPERTY() uint32 HubWorldEpoch = 0; // 设备注册World代数，旅行/卸载作废。
};

/** 服务器产生的单项商品投影；客户端只格式化，不重做价格/可购买规则。 */
USTRUCT()
struct FPSDEMO_API FDemoShopOfferSnapshot
{
    GENERATED_BODY()
    UPROPERTY() int32 Cost = 0; // 权威报价，币种由当前战局阶段决定。
    UPROPERTY() FName Id; // 稳定商品/奖励ID；UI不以显示名猜测策略。
    UPROPERTY() FString Title; // 服务器定义中的展示标题，客户端不读取本机不同配置覆盖它。
    UPROPERTY() FString Detail; // 根据本次真实增量生成的说明，避免UI写死旧数值。
    UPROPERTY() float CurrentValue = 0.f; // 生成投影时的当前个人属性。
    UPROPERTY() float Gain = 0.f; // 此项实际收益，免费奖励达到上限时可为0。
    UPROPERTY() bool bUnlocked = false; // 弹药解锁状态；属性行不使用。
    UPROPERTY() bool bSelected = false; // 弹药当前装配状态；属性行不使用。
    UPROPERTY() FString BlockReason = TEXT("等待服务器状态"); // 空表示此商品当前允许操作，提交时仍重验。
};

/** 仅向拥有者发送的菜单业务投影；与公开GameState分开，禁止泄漏其他队员钱包。 */
USTRUCT()
struct FPSDEMO_API FDemoPlayerMenuSnapshot
{
    GENERATED_BODY()
    UPROPERTY() FDemoPlayerProgressSnapshot Progress; // 本人服务器进度的只读副本，PS OnChanged可更新数值。
    UPROPERTY() FDemoRunBuildSnapshot Build; // 本人核心选择值，队友只读公开CoreId。
    UPROPERTY() FDemoRunSummarySnapshot Summary; // 本人结束前冻结的战报；只经拥有者可靠RPC，不公开经济或写存档。
    UPROPERTY() TArray<FDemoShopOfferSnapshot> CoreOffers; // 三核心菜单，由服务器生成。
    UPROPERTY() bool bBranchReward = false; // 核心二选一替代当关原奖励。
    UPROPERTY() FName PrimaryWeaponId; // 权威主武器稳定ID，回执先于Actor复制抵达也能保存本机选择偏好。
    UPROPERTY() TArray<FDemoShopOfferSnapshot> UpgradeOffers; // 固定三行；未就绪为空，UI禁用购买。
    UPROPERTY() TArray<FDemoShopOfferSnapshot> RewardOffers; // 三个兼容策略的服务器投影，UI不再复制奖励数值。
    UPROPERTY() TArray<FDemoShopOfferSnapshot> AmmoOffers; // 固定四行；由弹药目录和本人库存共同投影。
    UPROPERTY() TArray<FDemoGambleReward> OwnedTalentViews; // 本人天赋名称、等级和实际收益说明；服务器生成，仅展示，不写档。
    UPROPERTY() TArray<FDemoGambleOffer> GambleOffers; // 权威概率/价格/理由，按阶段选择显示银币或金币行。
    UPROPERTY() FDemoCasinoRules CasinoRules; // 服务端公开配置；本机预览/确认后服务器按指纹重验。
    UPROPERTY() FString CasinoConfigError; // 配置错误禁用新下注，不阻止已有货箱按冻结金额结算。
    UPROPERTY() FString TerminalBlockReason = TEXT("请与终端交互"); // 当前服务器交互会话的公共拒绝原因。
    UPROPERTY() bool bCanChooseReward = false; // 本关本人尚未选择免费奖励；队友选择不消耗此权限。
    UPROPERTY() bool bHasRunUpgrades = false; // 返回安全区确认提示，交易仍在服务器复核。
    UPROPERTY() bool bCanLeadSession = false; // 仅房主可选择战局档位、启动和全队返回。
    UPROPERTY() TObjectPtr<ADemoInteractable> Terminal = nullptr; // 当前被批准的交互Actor，销毁后页面失效。
    UPROPERTY() uint64 TerminalGeneration = 0; // 当前已批准会话的代数，提交时带回。
    UPROPERTY() bool bNextLevel = false; // 当前交互是出发终端，false为升级终端。
    UPROPERTY() FName CasinoDeviceId; // 本人当前批准的实体设备，None表示无会话。
    UPROPERTY() FGuid CasinoDeviceToken; // 本人可靠快照私有租约，不给其他玩家。
    UPROPERTY() uint32 HubWorldEpoch = 0; // 当前设备World代数。
    UPROPERTY() int32 CasinoDeviceMode = INDEX_NONE; // 原Mode0..6，页面只直达这一项，不另开总列表。
    UPROPERTY() uint32 CasinoControlsRevision=0; // 服务器冻结的物理控件上下文版本；0原页面，OwnerOnly快照，旧绑定不得提交。
};
