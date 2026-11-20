// 职责：P0商品/布局默认与作者校验，并独立校验已作者环境选择供Blockout回退；只复用现目录及CDO，不产生库存、概率或经济事实。依赖DemoWeaponCatalog和原武器Config。
#include "Interaction/HubShops/DemoHubShopDefinition.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/DemoWeaponBase.h"
#include "Debug/DemoLog.h"
#include "Debug/DemoStaticLog.h"
#include "Misc/Crc.h"

UDemoHubShopDefinition::UDemoHubShopDefinition()
{
    DEMO_LOG_CALL();
    // R02已保存DA可能省略与CDO相同的布局值；保留原默认/数组，R03只能由独立作者脚本显式迁移并保存LayoutRevision=3。
    HighlightMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Data/HubShops/M_HubShopHighlight.M_HubShopHighlight"))); // 固定项目作者资源，由Editor脚本真实创建。
    const FName WeaponIds[] = {TEXT("rifle"),TEXT("shotgun"),TEXT("sniper")}; // 原主槽身份。
    const int32 Prices[] = {500,750,1000}; // R02金币首购初值，后续只在DA调价。
    for (int32 Index=0; Index<3; ++Index)
    {
        FDemoHubWeaponOffer Offer; // 本次作者值随后由DA数组拥有。
        Offer.WeaponId=WeaponIds[Index]; Offer.OfferId=FName(*FString::Printf(TEXT("gun_%s"),*WeaponIds[Index].ToString())); Offer.GoldPrice=Prices[Index];
        WeaponOffers.Add(Offer);
        FDemoHubDisplayBinding Display; // 三把无武器逻辑的陈列模型，前方留站位。
        Display.DisplayId=FName(*FString::Printf(TEXT("gunshop_%s_01"),*WeaponIds[Index].ToString())); Display.OfferId=Offer.OfferId;
        Display.AnchorId=FName(*FString::Printf(TEXT("GunWall_%s"),*WeaponIds[Index].ToString()));
        Display.Transform=FTransform(FRotator::ZeroRotator,FVector(-120.f+Index*550.f,1430.f,155.f));
        Displays.Add(Display);
    }
    const FName Games[] = {TEXT("numbers"),TEXT("slots"),TEXT("mine"),TEXT("reactor"),TEXT("dice"),TEXT("arena"),TEXT("range")}; // 对应固定原Mode0..6。
    const FVector Locations[] = {FVector(250,-370,0),FVector(600,-370,0),FVector(950,-370,0),FVector(-20,-1100,0),FVector(1050,-800,0),FVector(280,-1240,0),FVector(900,-1240,0)}; // 真实Hub局部锚点cm，留中央500cm通道。
    const float Yaws[] = {-90,-90,-90,0,180,90,90}; // 设备X正向朝操作站位。
    for (int32 Index=0; Index<7; ++Index)
    {
        FDemoHubCasinoBinding Device; // 只定义身份/位置，不持有赔率。
        Device.GameId=Games[Index]; Device.Mode=Index; Device.DeviceId=FName(*FString::Printf(TEXT("casino_%s_01"),*Games[Index].ToString()));
        Device.AnchorId=FName(*FString::Printf(TEXT("Casino_%s"),*Games[Index].ToString()));
        Device.Transform=FTransform(FRotator(0,Yaws[Index],0),Locations[Index]); Devices.Add(Device);
    }
    const TCHAR* Names[] = {TEXT("GunShop_Interior"),TEXT("GunShop_EntranceClearance"),TEXT("GunShop_MainDoor"),TEXT("GunShop_Keeper"),TEXT("GunShop_KeeperLeash"),TEXT("GunShop_KeeperMove_A"),TEXT("GunShop_KeeperMove_B"),TEXT("GunShop_EscapePlane"),TEXT("GunShop_OutsideWait"),TEXT("GunShop_VolunteerSign"),TEXT("GunShop_Respawn_A"),TEXT("GunShop_Respawn_B"),TEXT("Casino_Interior"),TEXT("Casino_EntranceClearance"),TEXT("Casino_RangeReturn_A"),TEXT("Casino_RangeReturn_B")}; // R02稳定位置身份；无事件执行。
    const FVector Points[] = {FVector(500,800,180),FVector(-410,800,100),FVector(-410,800,180),FVector(1160,1120,100),FVector(1100,1100,0),FVector(1030,1080,0),FVector(1130,1260,0),FVector(-610,800,0),FVector(-850,800,100),FVector(-520,1100,100),FVector(-850,500,100),FVector(-850,1050,100),FVector(500,-800,180),FVector(-210,-800,100),FVector(-600,-800,100),FVector(-600,-1050,100)}; // 真实局部cm，与两个西入口及既有庭院相容。
    for (int32 Index=0; Index<UE_ARRAY_COUNT(Names); ++Index)
    {
        FDemoHubShopAnchor Anchor; // DA持有的纯作者值，后续模式按明确ID查询。
        Anchor.AnchorId=Names[Index]; Anchor.Transform=FTransform(Points[Index]); Anchors.Add(Anchor);
    }
}

const FDemoHubWeaponOffer* UDemoHubShopDefinition::FindOffer(FName OfferId) const
{ DEMO_LOG_TICK(); for (const FDemoHubWeaponOffer& Offer:WeaponOffers) if(Offer.OfferId==OfferId) return &Offer; return nullptr; }
const FDemoHubDisplayBinding* UDemoHubShopDefinition::FindDisplay(FName DisplayId) const
{ DEMO_LOG_TICK(); for (const FDemoHubDisplayBinding& Display:Displays) if(Display.DisplayId==DisplayId) return &Display; return nullptr; }
const FDemoHubCasinoBinding* UDemoHubShopDefinition::FindDevice(FName DeviceId) const
{ DEMO_LOG_TICK(); for (const FDemoHubCasinoBinding& Device:Devices) if(Device.DeviceId==DeviceId) return &Device; return nullptr; }

int32 UDemoHubShopDefinition::ModeForGameId(FName GameId)
{
    DEMO_LOG_STATIC_TICK(nullptr);
    const FName Ids[] = {TEXT("numbers"),TEXT("slots"),TEXT("mine"),TEXT("reactor"),TEXT("dice"),TEXT("arena"),TEXT("range")}; // 只映射GameId到Mode，不复制原Choice转换。
    for(int32 Index=0;Index<UE_ARRAY_COUNT(Ids);++Index) if(Ids[Index]==GameId) return Index;
    return INDEX_NONE;
}

const ADemoWeaponBase* UDemoHubShopDefinition::ResolveWeaponCDO(FName WeaponId,FString& Error) const
{
    DEMO_LOG_CALL(); Error.Reset();
    const FDemoWeaponDefinition* Weapon=DemoWeaponCatalog::FindDefinition(WeaponId); // 目录拥有，不跨异步持有。
    UClass* Class=Weapon&&Weapon->AllowedSlot==1?Weapon->WeaponClass.LoadSynchronous():nullptr; // 作者/初始化阶段加载，焦点与交易使用已加载CDO。
    const ADemoWeaponBase* CDO=Class?Class->GetDefaultObject<ADemoWeaponBase>():nullptr; // 绝不Spawn武器Actor作陈列。
    if(!CDO || !CDO->Config.Validate(Error) || (!CDO->Config.Mesh&&!CDO->Config.StaticMesh))
    {
        if(Error.IsEmpty()) Error=TEXT("商品枪类/CDO或陈列网格缺失");
        UE_LOG(LogFPSDemo,Warning,TEXT("HUB_SHOP_WEAPON_INVALID weapon=%s reason=%s"),*WeaponId.ToString(),*Error); return nullptr;
    }
    return CDO;
}

uint32 UDemoHubShopDefinition::MakeOfferKey(FName OfferId) const
{
    DEMO_LOG_TICK();
    const FDemoHubWeaponOffer* Offer=FindOffer(OfferId); // 本次同步读取，不持有外部报价。
    if(!Offer) return 0;
    const FString Canonical=FString::Printf(TEXT("%d|%d|%s|%s|%d|%d|%d"),SpecRevision,CatalogRevision,*Offer->OfferId.ToString(),*Offer->WeaponId.ToString(),Offer->GoldPrice,Offer->bEnabled,Offer->bRobberyAllowed); // 明确字段，不对结构内存/填充字节Hash。
    const uint32 Hash=FCrc::StrCrc32(*Canonical); // 内容变化拒绝旧报价；非安全密钥。
    return Hash?Hash:1u;
}

bool UDemoHubShopDefinition::Validate(FString& Error) const
{
    DEMO_LOG_CALL(); Error.Reset();
    const bool bLayoutR03=LayoutRevision==3; // 只改变作者几何校验；0继续支持旧R02读回，不从坐标猜迁移状态。
    if(SpecRevision!=2||CatalogRevision<=0||bRobberyEnabled||bPhysicalCasinoEnabled)
        Error=TEXT("Hub商店R02/P0版本非法，抢夺与实体开奖尚未实施不得开启");
    else if((LayoutRevision!=0&&!bLayoutR03)||HubFloorSizeCm.ContainsNaN()||HubFloorSizeCm.X<3400||HubFloorSizeCm.Y<3400||HubFloorSizeCm.X>10000||HubFloorSizeCm.Y>10000)
        Error=TEXT("Hub布局修订或地板完整尺寸非法，旧布局0与显式R03布局3独立验证");
    else if(!FMath::IsFinite(InteractionDistanceCm)||InteractionDistanceCm<50||InteractionDistanceCm>500||!FMath::IsFinite(FocusHalfAngleDeg)||FocusHalfAngleDeg<1||FocusHalfAngleDeg>10||!FMath::IsFinite(ReleaseHalfAngleDeg)||ReleaseHalfAngleDeg<FocusHalfAngleDeg||ReleaseHalfAngleDeg>12||!FMath::IsFinite(FocusStableSeconds)||FocusStableSeconds<0||FocusStableSeconds>.5f||!FMath::IsFinite(ScanHz)||ScanHz<5||ScanHz>30||!FMath::IsFinite(ServerAngleToleranceDeg)||ServerAngleToleranceDeg<0||ServerAngleToleranceDeg>3)
        Error=TEXT("Hub商店交互距离/角度/稳定时间/扫描频率越界");
    else if(CardSizePx.ContainsNaN()||CardSizePx.X<280||CardSizePx.X>480||CardSizePx.Y<120||CardSizePx.Y>220||CardAnchorOffsetCm.ContainsNaN()||CardAnchorOffsetCm.GetAbsMax()>100||!FMath::IsFinite(ScreenMarginPx)||ScreenMarginPx<8||ScreenMarginPx>80||!FMath::IsFinite(ErrorSeconds)||ErrorSeconds<.5f||ErrorSeconds>5||!FMath::IsFinite(HighlightColor.R)||!FMath::IsFinite(HighlightColor.G)||!FMath::IsFinite(HighlightColor.B)||!FMath::IsFinite(HighlightColor.A)||HighlightColor.R<0||HighlightColor.R>1||HighlightColor.G<0||HighlightColor.G>1||HighlightColor.B<0||HighlightColor.B>1||HighlightColor.A<0||HighlightColor.A>1)
        Error=TEXT("Hub商店浮签/颜色配置越界");
    else if(!FMath::IsFinite(DeviceLeaseSeconds)||DeviceLeaseSeconds<5||DeviceLeaseSeconds>30||!FMath::IsFinite(DeviceHeartbeatSeconds)||DeviceHeartbeatSeconds<=0||DeviceLeaseSeconds<DeviceHeartbeatSeconds*3)
        Error=TEXT("娱乐设备租约/心跳配置非法");
    else if(GunShopCenter.ContainsNaN()||CasinoCenter.ContainsNaN()||GunShopInteriorSize.ContainsNaN()||CasinoInteriorSize.ContainsNaN()||GunShopInteriorSize.X<(bLayoutR03?1600:1800)||GunShopInteriorSize.Y<(bLayoutR03?1200:1400)||GunShopInteriorSize.Z<(bLayoutR03?400:360)||CasinoInteriorSize.X<(bLayoutR03?2600:1400)||CasinoInteriorSize.Y<(bLayoutR03?2000:1200)||CasinoInteriorSize.Z<(bLayoutR03?400:360)||!FMath::IsFinite(EntranceClearWidthCm)||EntranceClearWidthCm<(bLayoutR03?500:360)||EntranceClearWidthCm>FMath::Min(GunShopInteriorSize.Y,CasinoInteriorSize.Y)-100||!FMath::IsFinite(GunShopMainLaneCm)||GunShopMainLaneCm<400||!FMath::IsFinite(CasinoMainLaneCm)||CasinoMainLaneCm<(bLayoutR03?600:500))
        Error=TEXT("两屋尺寸/入口净宽/通行带非法，不满足对应布局修订的净尺寸");
    else if(bLayoutR03&&(FMath::Abs(GunShopCenter.X)+GunShopInteriorSize.X*.5+20>HubFloorSizeCm.X*.5||FMath::Abs(GunShopCenter.Y)+GunShopInteriorSize.Y*.5+20>HubFloorSizeCm.Y*.5||FMath::Abs(CasinoCenter.X)+CasinoInteriorSize.X*.5+20>HubFloorSizeCm.X*.5||FMath::Abs(CasinoCenter.Y)+CasinoInteriorSize.Y*.5+20>HubFloorSizeCm.Y*.5))
        Error=TEXT("R03两屋外墙超出声明Hub地板；实际地图扩大仍需独立作者核验");
    TSet<FName> Ids; // 一次验证的商品身份集合，不保存状态。
    for(const FDemoHubWeaponOffer& Offer:WeaponOffers)
    {
        const FDemoWeaponDefinition* Weapon=DemoWeaponCatalog::FindDefinition(Offer.WeaponId); // 只核稳定目录，不使一个损坏CDO阻断其他娱乐设备。
        if(Offer.OfferId.IsNone()||Ids.Contains(Offer.OfferId)||Offer.GoldPrice<=0||!Weapon||Weapon->AllowedSlot!=1||Offer.WeaponId==TEXT("pistol")) Error=TEXT("商品ID重复/未知主枪/价格非正");
        Ids.Add(Offer.OfferId);
    }
    if(WeaponOffers.Num()!=3) Error=TEXT("P0正式目录需要步枪/散弹/狙击三个商品");
    TSet<FName> Weapons; for(const FDemoHubWeaponOffer& Offer:WeaponOffers) Weapons.Add(Offer.WeaponId); // 不能同一枪重复报价伪造三项。
    if(!Weapons.Contains(TEXT("rifle"))||!Weapons.Contains(TEXT("shotgun"))||!Weapons.Contains(TEXT("sniper"))) Error=TEXT("正式三个主枪身份不全");
    Ids.Reset(); TSet<FName> DisplayOffers; // 展示与商品一对一，缺商品只禁该展示的资源读取。
    for(const FDemoHubDisplayBinding& Display:Displays)
    {
        if(Display.DisplayId.IsNone()||Display.AnchorId.IsNone()||Display.ShopId!=TEXT("hub_gunshop_01")||Ids.Contains(Display.DisplayId)||DisplayOffers.Contains(Display.OfferId)||!FindOffer(Display.OfferId)||Display.Transform.ContainsNaN()||Display.ModelTransform.ContainsNaN()||Display.InfoOffset.ContainsNaN()||Display.InfoOffset.GetAbsMax()>100||Display.FocusExtent.ContainsNaN()||Display.FocusExtent.GetMin()<=0||Display.FocusExtent.GetMax()>150) Error=TEXT("枪墙绑定/唯一ID/变换/焦点盒非法");
        if(bLayoutR03&&(!Display.Transform.GetRotation().Equals(FQuat::Identity,.001f)||!Display.Transform.GetScale3D().Equals(FVector::OneVector,.001f)||!Display.ModelTransform.GetRotation().Equals(FRotator(0,-90,0).Quaternion(),.001f)||Display.ModelTransform.GetScale3D().GetMin()<=0||Display.SupportHeightOffsetsCm.ContainsNaN()||Display.SupportHeightOffsetsCm.GetAbsMax()>30.)) Error=TEXT("R03枪根/模型朝向缩放或独立托面高度非法");
        Ids.Add(Display.DisplayId); DisplayOffers.Add(Display.OfferId);
    }
    if(Displays.Num()!=3) Error=TEXT("正式枪墙需要三个展示位");
    Ids.Reset(); TSet<int32> Modes; // 七设备Mode必须完整且互异。
    for(const FDemoHubCasinoBinding& Device:Devices)
    {
        if(Device.DeviceId.IsNone()||Device.AnchorId.IsNone()||Ids.Contains(Device.DeviceId)||Modes.Contains(Device.Mode)||ModeForGameId(Device.GameId)!=Device.Mode||Device.Mode<0||Device.Mode>6||Device.Transform.ContainsNaN()) Error=TEXT("娱乐设备ID/GameId/Mode/变换非法");
        Ids.Add(Device.DeviceId); Modes.Add(Device.Mode);
    }
    if(Devices.Num()!=7||Modes.Num()!=7) Error=TEXT("娱乐屋七玩法入口不全");
    Ids.Reset(); // 作者锚点名与展示/设备锚点共用唯一命名域。
    for(const FDemoHubDisplayBinding& Display:Displays) { if(Ids.Contains(Display.AnchorId)) Error=TEXT("作者锚点重复"); Ids.Add(Display.AnchorId); }
    for(const FDemoHubCasinoBinding& Device:Devices) { if(Ids.Contains(Device.AnchorId)) Error=TEXT("作者锚点重复"); Ids.Add(Device.AnchorId); }
    for(const FDemoHubShopAnchor& Anchor:Anchors) { if(Anchor.AnchorId.IsNone()||Ids.Contains(Anchor.AnchorId)||Anchor.Transform.ContainsNaN()) Error=TEXT("关键作者锚点重复或变换非法"); Ids.Add(Anchor.AnchorId); }
    const FName Required[] = {TEXT("GunShop_Interior"),TEXT("GunShop_EntranceClearance"),TEXT("GunShop_MainDoor"),TEXT("GunShop_Keeper"),TEXT("GunShop_KeeperLeash"),TEXT("GunShop_KeeperMove_A"),TEXT("GunShop_KeeperMove_B"),TEXT("GunShop_EscapePlane"),TEXT("GunShop_OutsideWait"),TEXT("GunShop_VolunteerSign"),TEXT("GunShop_Respawn_A"),TEXT("GunShop_Respawn_B"),TEXT("Casino_Interior"),TEXT("Casino_EntranceClearance"),TEXT("Casino_RangeReturn_A"),TEXT("Casino_RangeReturn_B")}; // P0作者预留完整集合，P1仍无权限。
    for(FName Name:Required) if(!Ids.Contains(Name)) Error=FString::Printf(TEXT("关键作者锚点缺失：%s"),*Name.ToString());
    if(!Error.IsEmpty()) UE_LOG(LogFPSDemo,Warning,TEXT("HUB_SHOP_CONFIG_REJECT %s"),*Error);
    return Error.IsEmpty();
}

bool UDemoHubShopDefinition::ValidateAuthoredEnvironment(FString& Error) const
{
    DEMO_LOG_CALL(); Error.Reset();
    // 环境只是原外观的可选替换；旧DA默认关闭时不校验占位字段，不让环境错误关闭原商店/交互。
    if (!bUseAuthoredEnvironment)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("HUB_AUTHORED_ENV_CONFIG_DISABLED legacy_blockout_allowed"));
        return true;
    }
    // 配置固定Tag使用显式大小写精确文本合同；World唯一根、成员完整性与资源检查由Blockout另行执行。
    if (!AuthoredEnvironmentRootTag.ToString().Equals(TEXT("HubShopEnv.Root"),ESearchCase::CaseSensitive))
        Error=TEXT("已作者环境RootTag必须精确为HubShopEnv.Root");
    else if (!AuthoredEnvironmentMemberTag.ToString().Equals(TEXT("HubShopEnv.Member"),ESearchCase::CaseSensitive))
        Error=TEXT("已作者环境MemberTag必须精确为HubShopEnv.Member");
    else if (AuthoredEnvironmentId.IsNone())
        Error=TEXT("启用已作者环境时稳定EnvironmentId不能为空");
    else if (AuthoredEnvironmentRevision<1) // 声明类型int32已限制上界MAX_int32，不作恒假的超上界比较。
        Error=TEXT("启用已作者环境时Revision必须在1..MAX_int32");
    else if (AuthoredEnvironmentItemCount<1||AuthoredEnvironmentItemCount>4096)
        Error=TEXT("启用已作者环境时ItemCount必须在1..4096，并与当前修订完整闭包一致");
    if (!Error.IsEmpty())
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("HUB_AUTHORED_ENV_CONFIG_REJECT id=%s revision=%d items=%d reason=%s"),
            *AuthoredEnvironmentId.ToString(),AuthoredEnvironmentRevision,AuthoredEnvironmentItemCount,*Error);
        return false;
    }
    UE_LOG(LogFPSDemo,Log,TEXT("HUB_AUTHORED_ENV_CONFIG_ACCEPT id=%s revision=%d items=%d"),
        *AuthoredEnvironmentId.ToString(),AuthoredEnvironmentRevision,AuthoredEnvironmentItemCount);
    return true;
}

UDemoHubShopSettings::UDemoHubShopSettings()
{ DEMO_LOG_CALL(); DefinitionAsset=TSoftObjectPtr<UDemoHubShopDefinition>(FSoftObjectPath(TEXT("/Game/Data/HubShops/DA_HubShops_R02.DA_HubShops_R02"))); }
UDemoHubShopDefinition* UDemoHubShopSettings::LoadDefinition(FString& Error) const
{
    DEMO_LOG_CALL(); Error.Reset();
    UDemoHubShopDefinition* Definition=DefinitionAsset.IsNull()?nullptr:DefinitionAsset.LoadSynchronous(); // 只从项目可信Settings载入。
    if(!Definition) { Error=TEXT("Hub商店DA缺失，请由Editor执行Tools/Unreal/author_hub_shops.py"); UE_LOG(LogFPSDemo,Warning,TEXT("HUB_SHOP_LOAD_REJECT %s"),*Error); return nullptr; }
    return Definition->Validate(Error)?Definition:nullptr;
}
