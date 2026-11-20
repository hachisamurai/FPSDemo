// 职责：DefaultGame.ini金币娱乐配置入口。使用：服务器读取CDO，菜单复制Rules；不承载随机状态或玩家钱包。
#pragma once
#include "CoreMinimal.h"
#include "Economy/Gambling/DemoCasinoTypes.h"
#include "DemoCasinoSettings.generated.h"
UCLASS(Config=Game,DefaultConfig)
class FPSDEMO_API UDemoCasinoSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config) FDemoCasinoRules Rules; // 三种玩法的下注、权重、倍率及上限，定义和单位见类型声明。
    UPROPERTY(Config) TArray<int32> RelicGoldCosts={1200,1500,1800,1500}; // 四遗物永久直购价；已拥有免费装配。
};
