// 职责：实体赌场控件的类型化绑定/本人请求和独立原型开关；不包含钱包、随机结果或第二套赔率。CP-01仅启用老虎机，未来Mode必须另行实现并验收。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DemoCasinoControlTypes.generated.h"
class UWorld;
class UDemoCasinoDeviceProfile;

/** 稳定控件语义；本地编辑不发经济RPC，唯一PullSlots由服务器绑定重建原Mode1/Op0。 */
UENUM(BlueprintType)
enum class EDemoCasinoControlAction : uint8
{
    None, AmountDigit, AmountClear, AmountBackspace, AmountCommit, PullSlots, Exit
};

/** 一个不可由客户端改写的控件绑定；位置是设备局部cm，查询件不随表现动画移动。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoCasinoControlBinding
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere) FName ControlId; // 单设备稳定唯一身份，不以动态组件名/数组索引授权。
    UPROPERTY(EditAnywhere) EDemoCasinoControlAction Action=EDemoCasinoControlAction::None; // 只由可信绑定提供，正式资产还核固定语义。
    UPROPERTY(EditAnywhere) int32 Index=INDEX_NONE; // AmountDigit的0..9；其它动作无索引。
    UPROPERTY(EditAnywhere) FVector Position=FVector::ZeroVector; // 设备局部查询中心cm，正式从实际模型测量。
    UPROPERTY(EditAnywhere) FRotator Rotation=FRotator::ZeroRotator; // 根局部查询框朝向，支持倾斜面板，+X为表面法线。
    UPROPERTY(EditAnywhere) FVector Extent=FVector(1.5,9,6); // 查询盒半尺寸cm；旧原型默认不约束正式实测键面。
    UPROPERTY(EditAnywhere) FString Label; // 静态公开控件名，不放本人金额或结果。
};

/** 本人复制组件接受的下注意图；Mode/Operation由ControlId重建，没有客户端开奖结果或任意目标玩家。 */
USTRUCT()
struct FPSDEMO_API FDemoCasinoControlRequest
{
    GENERATED_BODY()
    UPROPERTY() int32 RequestId=0; // 本PC该组件单调序号，服务端执行前消费，和旧PC请求号分域。
    UPROPERTY() FName DeviceId; // 当前注册设备稳定键。
    UPROPERTY() FGuid Token; // 原服务签发本人私有租约，日志禁止输出。
    UPROPERTY() uint32 WorldEpoch=0; // 当前服务器注册代数，旅行失效。
    UPROPERTY() uint32 BindingRevision=0; // CP-01可信控件布局版本，旧控件点击拒绝。
    UPROPERTY() FName ControlId; // 唯一服务器经济动作slots.pull，服务器须真实首次命中该绑定。
    UPROPERTY() int32 ExpectedSerial=INDEX_NONE; // 原个人GambleSerial；重复/过期不得再扣款。
    UPROPERTY() int32 Stake=0; // 金币正int32，权威Quote重算余额/全部可达奖金溢出。
    UPROPERTY() uint32 RulesKey=0; // 玩家确认的服务器公开规则指纹。
};

/** 独立CP-01原型开关，默认不替换正式P0，也不修改R03 DA/地图。 */
UCLASS(Config=Game,DefaultConfig)
class FPSDEMO_API UDemoCasinoPhysicalSettings : public UObject
{
    GENERATED_BODY()
public:
    // 正式配置显式允许一台Mode1原型；其它Mode仍原页面。只影响输入/表现，不改变经济规则/存档。
    UPROPERTY(Config,EditAnywhere) bool bSlotsPrototypeEnabled=false;
    // 独立正式模型Profile；空时保持原P0，只有上方显式原型/Editor夹具才用旧硬编码。指定但非法时禁物理，不伪造旧模型。
    UPROPERTY(Config,EditAnywhere) TSoftObjectPtr<UDemoCasinoDeviceProfile> SlotsModelProfile;
};

namespace DemoCasinoControls
{
    /** World必须是游戏World；正式读独立配置，Editor显式夹具参数仅本次运行启用，无云或永久解锁旁路。 */
    FPSDEMO_API bool IsSlotsPrototypeEnabled(const UWorld* World);
    /** 仅旧CP-01原型版本2；正式设备读Controls.GetBindingRevision，不能给新Profile签旧常量。 */
    FPSDEMO_API uint32 BindingRevision();
    /** 固定15语义映射；仅作者验证/可信服务使用，未知ID不能增加Mode/Operation权限。 */
    FPSDEMO_API bool ResolveSemantic(FName ControlId,EDemoCasinoControlAction& Action,int32& Index);
}
