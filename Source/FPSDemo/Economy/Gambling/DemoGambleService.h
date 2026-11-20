// 职责：抽取报价及权威交易规则。使用：商店调用，同步校验钱包/目录/序号，存档失败回滚，不由动画发奖。
#pragma once
#include "CoreMinimal.h"
#include "Contracts/DemoGameplayContracts.h"
#include "Economy/DemoShopComponent.h"
class ADemoCharacter;
namespace DemoGambleService
{
    /** Shop提供本人当前终端权限；Choice为固定策略索引；只读报价不生成随机数。 */
    FPSDEMO_API FDemoGambleOffer Quote(const UDemoShopComponent& Shop, ADemoCharacter* Player, int32 Choice);

}
