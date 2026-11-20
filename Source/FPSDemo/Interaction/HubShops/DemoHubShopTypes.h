// 职责：枪墙拥有者报价/意图/回执值。使用：PC复制组件可靠RPC与权威Shop；不接收钱包、伤害、随机或任意资源路径。
#pragma once
#include "CoreMinimal.h"
#include "Contracts/DemoGameplayContracts.h"
#include "InputCoreTypes.h"
#include "DemoHubShopTypes.generated.h"

/** 服务端从当前展示/目录CDO生成；每次提交重验，客户端仅格式化三行浮签。 */
USTRUCT()
struct FPSDEMO_API FDemoHubGunQuote
{
    GENERATED_BODY()
    UPROPERTY() FName DisplayId; // 当前World注册展示稳定ID。
    UPROPERTY() FName OfferId; // 商品稳定ID，不用目录数组下标报价。
    UPROPERTY() FName WeaponId; // 真实主枪目录ID。
    UPROPERTY() uint32 WorldEpoch = 0; // 注册表服务端签发的当前World代数。
    UPROPERTY() FGuid SlotEpoch; // 本PC当前槽会话代数，不是持久物品身份。
    UPROPERTY() FString QuoteKey; // sha1内容指纹，覆盖报价及真实CDO基础展示。
    UPROPERTY() int32 Price = 0; // 正常首购报价金币；已拥有显示免费装备。
    UPROPERTY() int32 ShopSerial = 0; // 个人成功操作高水位，旧报价拒绝。
    UPROPERTY() bool bOwned = false; // 本槽本人有效枪权，不能以其他成员购买改变。
    UPROPERTY() FString Name; // 服务器真实Config显示名。
    UPROPERTY() FString Type; // 稳定枪型＋真实FireMode。
    UPROPERTY() float Damage = 0.f; // CDO每丸基础HP，不含个人成长。
    UPROPERTY() int32 Pellets = 1; // 实际射击类型有效丸数，普通枪固定1。
    UPROPERTY() float RPM = 0.f; // CDO每分钟射击次数。
    UPROPERTY() FName Code; // 拒绝原因；None为可提交，不解析中文授权。
    UPROPERTY() FString Message; // 本人可见原因，无账号密钥或其他成员私值。
};

/** 单个用户明确按键；身份只由RPC所属PC解析，价格只作比较。 */
USTRUCT()
struct FPSDEMO_API FDemoHubGunIntent
{
    GENERATED_BODY()
    UPROPERTY() FString OperationId; // 规范GUID，跨可靠重试查持久收据。
    UPROPERTY() FName DisplayId; // 注册展示白名单ID。
    UPROPERTY() FName OfferId; // 冻结报价商品，错配拒绝。
    UPROPERTY() uint32 WorldEpoch = 0; // 旧World/展示卸载后拒绝。
    UPROPERTY() FGuid SlotEpoch; // PC签发的当前槽会话，不接受客户端自行授权。
    UPROPERTY() FString QuoteKey; // 客户端带回服务端原报价指纹。
    UPROPERTY() int32 ExpectedPrice = INDEX_NONE; // 金币报价，不能作为实际扣款来源。
    UPROPERTY() int32 ExpectedShopSerial = INDEX_NONE; // 防双击/较早请求重放。
};

/** 耐久结果与新报价，只送拥有者；无火花/声音或临时抢夺授权。 */
USTRUCT()
struct FPSDEMO_API FDemoHubGunResult
{
    GENERATED_BODY()
    UPROPERTY() FString OperationId; // 与本人等待意图关联。
    UPROPERTY() FDemoCommandResult Result; // Committed只在完整本地保存成功后返回。
    UPROPERTY() FDemoHubGunQuote Quote; // 已变化的个人高水位/拥有状态。
    UPROPERTY() int32 ActiveSlot = 2; // 当前实际装备槽；已耐久但表现失败可为旧槽。
};

/** 输入单一来源；正式作者Action/Context由Editor创建，缺资源时旧交互保留可配置键回退。 */
UCLASS(Config=Game, DefaultConfig)
class FPSDEMO_API UDemoHubShopInputSettings : public UObject
{
    GENERATED_BODY()
public:
    /** 固定可信项目软引用，不允许从RPC或存档传入任意Action路径。 */
    UDemoHubShopInputSettings();
    UPROPERTY(Config, EditAnywhere) TSoftObjectPtr<class UInputAction> InteractAction; // Started离散交互，不将按住转为重复购买。
    UPROPERTY(Config, EditAnywhere) TSoftObjectPtr<class UInputMappingContext> Mapping; // 每本地PC安装一次，Owner卸载撤销。
    UPROPERTY(Config, EditAnywhere) FKey FallbackKey; // 仅作者资产缺失时旧交互回退，UI从实际映射读取按键。
};
