// 职责：正式战役资产的项目配置入口。使用：服务器Flow初始化/新轮读取；依赖DeveloperSettings和已由Editor创建的DataAsset。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DemoCampaignSettings.generated.h"

class UDemoCampaignDefinition;

/** 配置只指定可信作者资产；游戏存档冻结纯值，恢复时不再读取此资产。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Demo Campaign"))
class FPSDEMO_API UDemoCampaignSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    /** 只设置默认作者路径，不在CDO构造时加载资源或访问World。 */
    UDemoCampaignSettings();
    UPROPERTY(Config, EditAnywhere, Category="Campaign", meta=(AllowedClasses="DemoCampaignDefinition"))
    TSoftObjectPtr<UDemoCampaignDefinition> CampaignAsset; // 新轮读取的正式资产；/Game/Data已纳入Cook，GM显式覆盖优先由Flow处理。
    /** 同步加载并全量校验；仅初始化/新轮调用，缺失/错类/非法配置返回nullptr及含路径Error，不静默回退。 */
    UDemoCampaignDefinition* LoadCampaign(FString& Error) const;
};
