// 职责：CommonUI默认确认/返回按键契约。使用：CommonInputSettings指向此原生类，无游戏经济权限。
#pragma once
#include "CoreMinimal.h"
#include "CommonInputBaseTypes.h"
#include "DemoCommonInputData.generated.h"
/** UI使用CommonUI数据表动作，战斗继续使用现有EnhancedInput映射。 */
UCLASS()
class FPSDEMO_API UDemoCommonInputData : public UCommonUIInputData
{
    GENERATED_BODY()
public:
    /** 创建本类持有的只读动作表；Esc/B返回、Enter/A确认，不写磁盘资产。 */
    UDemoCommonInputData();
private:
    UPROPERTY() TObjectPtr<UDataTable> Actions; // CDO默认子对象，只包含键位，运行时CommonUI弱引用查询。
};
