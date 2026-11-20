// 职责：加载并拒绝无效的作者战役配置。使用：Flow在准备新轮前调用；只加载项目配置的路径，不接受客户端或存档资源路径。
#include "Game/Content/DemoCampaignSettings.h"
#include "Game/Content/DemoCampaignDefinition.h"
#include "Debug/DemoLog.h"

UDemoCampaignSettings::UDemoCampaignSettings()
{
    DEMO_LOG_CALL();
    CampaignAsset = TSoftObjectPtr<UDemoCampaignDefinition>(FSoftObjectPath(TEXT("/Game/Data/Demo/Campaigns/DA_DemoCampaign.DA_DemoCampaign")));
}

UDemoCampaignDefinition* UDemoCampaignSettings::LoadCampaign(FString& Error) const
{
    DEMO_LOG_CALL();
    Error.Reset();
    const FString Path = CampaignAsset.ToSoftObjectPath().ToString(); // 仅用于配置加载及明确诊断，不来自联网请求。
    UDemoCampaignDefinition* Definition = CampaignAsset.IsNull() ? nullptr : CampaignAsset.LoadSynchronous(); // 由作者资产包拥有，Flow须以UPROPERTY保存引用。
    if (!Definition)
    {
        Error = FString::Printf(TEXT("战役配置资产缺失或类型不符：%s；请在Editor执行Tools/Unreal/create_demo_campaign.py并检查Cook目录"), Path.IsEmpty() ? TEXT("<未配置>") : *Path);
        UE_LOG(LogFPSDemo, Error, TEXT("CAMPAIGN_ASSET_LOAD_FAILED %s"), *Error);
        return nullptr;
    }
    FString ValidationError; // 保留具体字段原因，同时附上作者资产位置。
    if (!Definition->Validate(ValidationError))
    {
        Error = FString::Printf(TEXT("战役配置资产无效：%s；%s"), *Path, *ValidationError);
        UE_LOG(LogFPSDemo, Error, TEXT("CAMPAIGN_ASSET_INVALID %s"), *Error);
        return nullptr;
    }
    UE_LOG(LogFPSDemo, Log, TEXT("CAMPAIGN_ASSET_LOADED path=%s revision=%d"), *Path, Definition->ContentRevision);
    return Definition;
}
