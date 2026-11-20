// 职责：安全区P0商品、交互、两屋作者布局及独立环境选择配置；Settings只选此DA。依赖原武器目录/CDO和原Casino Mode，不拥有钱包或第二套赔率；环境校验失败只供白模回退，不禁用商店。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DemoHubShopDefinition.generated.h"

class ADemoWeaponBase;
class UMaterialInterface;

/** 金币首购商品身份；基础伤害/射速/模型仍从WeaponId对应CDO读取。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoHubWeaponOffer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName OfferId; // 全目录唯一稳定键，移除后不能复用为别的商品。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName WeaponId; // 原目录主槽稳定ID，不接受存档或客户端资产路径。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 GoldPrice = 1; // 正int32金币，不能把非法价格回退成免费。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bEnabled = true; // 仅控制新购；已拥有装备不因此剥夺权限。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRobberyAllowed = true; // 只为后续P1预留；本批全局P1仍关闭。
};

/** 墙枪世界作者绑定；所有Transform相对于Hub冻结地面框，单位cm/度。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoHubDisplayBinding
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName DisplayId; // 本Hub唯一展示身份，不采用Actor自动名。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName OfferId; // 必须解析本目录商品。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName ShopId = TEXT("hub_gunshop_01"); // 所属商店稳定身份。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName AnchorId; // 真实作者锚点名，必须唯一且非空。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform Transform; // Hub相对位置，展示根即交互中心。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform ModelTransform; // 仅陈列模型偏移/旋转；R03按此旋转/缩放后以真mesh bounds居中贴墙，不改武器BP资产。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector InfoOffset = FVector(0,-50,35); // 本展示局部cm；R03相对真实mesh中心，旧布局相对根，浮签朝屋内避墙。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector FocusExtent = FVector(90,35,45); // 旧布局根查询盒半尺寸cm；R03以实际mesh bounds加2cm重建，无Pawn阻挡。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector2D SupportHeightOffsetsCm = FVector2D::ZeroVector; // R03托面相对真实模型中心Z的cm偏移：X=枪托侧(-X)，Y=护木/枪管侧(+X)，-30..30；按实际近景校准，不用全枪最低点猜接触。
};

/** 七设备只选既有Mode/页面；禁止在此新增概率、倍率、金额或第二钱包。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoHubCasinoBinding
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName DeviceId; // 同Hub唯一设备身份。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName GameId; // numbers/slots/mine/reactor/dice/arena/range唯一对应Mode。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Mode = INDEX_NONE; // 固定原Casino0..6，不是UI Choice。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName AnchorId; // 已作者化的设备位置稳定锚点。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform Transform; // Hub框中设备地面原点，X正向为操作面。
};

/** 两屋稳定功能锚点；P1/P2仅预留位置，本批无事件AI/伤害或设备开奖。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoHubShopAnchor
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FName AnchorId; // 本DA唯一，供后续明确边界/返回/复活配置定位。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FTransform Transform; // 相对Hub地面框的真实作者变换，cm/度。
};

UCLASS(BlueprintType)
class FPSDEMO_API UDemoHubShopDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    /** 填充P0初版作者布局/三商品/七设备；不生成World或加载玩家数据。 */
    UDemoHubShopDefinition();
    /** 启动/作者验收时验证稳定键、数值、主槽白名单及七Mode闭包；单商品CDO资源由ResolveWeaponCDO独立核验。 */
    UFUNCTION(BlueprintCallable, Category="Hub Shop") bool Validate(FString& Error) const;
    /** Blockout重建/TA作者时独立校验已作者环境配置；Error返回失败原因、成功清空。禁用时兼容旧DA并直接成功；仅校验配置，不证明World闭包存在，不接入原Validate或关闭商店。 */
    UFUNCTION(BlueprintCallable, Category="Hub Shop|Authored Environment") bool ValidateAuthoredEnvironment(FString& Error) const;
    /** 查目录拥有的商品；调用者不得跨异步持有返回指针。 */
    const FDemoHubWeaponOffer* FindOffer(FName OfferId) const;
    /** 查作者展示绑定；非法ID返回nullptr，不按数组索引猜测商品。 */
    const FDemoHubDisplayBinding* FindDisplay(FName DisplayId) const;
    /** 查作者设备绑定；未知设备拒绝，不回退为任意Casino Mode。 */
    const FDemoHubCasinoBinding* FindDevice(FName DeviceId) const;
    /** 本次内容报价键覆盖版本、ID、WeaponId、价格、启用/抢夺标记；CDO基础数值由交易层补充。 */
    uint32 MakeOfferKey(FName OfferId) const;
    /** 武器类/CDO只在加载/作者配置时读取；返回目录拥有的CDO，绝不Spawn射击Actor来做陈列。 */
    const ADemoWeaponBase* ResolveWeaponCDO(FName WeaponId, FString& Error) const;
    /** 七稳定GameId映射旧Mode；未知返回INDEX_NONE，不复制Choice转换。 */
    static int32 ModeForGameId(FName GameId);

    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 SpecRevision = 2; // 文档/配置合同版本，与磁盘/网络协议独立。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 CatalogRevision = 1; // 商品身份/价格变化递增，报价键包含它。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 LayoutRevision = 0; // 0=旧R02/未显式迁移，3=本R03；默认0防止旧DA差分省略被误认新布局，不参与报价/存档/网络协议。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector2D HubFloorSizeCm = FVector2D(3400,3400); // Hub地板完整X/Y尺寸cm，3400..10000；R03作者显式写5600，实际地形仍归原地图/Area服务。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bPurchaseRoomEnabled = true; // P0正式购买与实体入口开关。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bRobberyEnabled = false; // P1未实施不得启用，Validate在本批拒绝true。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bPhysicalCasinoEnabled = false; // P2未实施不得启用，P0仍直达旧页面。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float InteractionDistanceCm = 250.f; // 眼位到锚点cm，50..500，双端同上限。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float FocusHalfAngleDeg = 4.f; // 进入软焦点半角度，1..10。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ReleaseHalfAngleDeg = 6.f; // 同目标释放半角，>=进入且<=12。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float FocusStableSeconds = .15f; // 候选稳定秒，0...5，遮挡应立即取消。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ScanHz = 20.f; // 本机注册表扫描频率5..30Hz。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ServerAngleToleranceDeg = 2.f; // 服务端额外容忍0..3度，不接受上传相机点。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector2D CardSizePx = FVector2D(360,168); // 1080p逻辑尺寸，DPI由UI换算。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector CardAnchorOffsetCm = FVector(0,30,8); // 未单独标定InfoAnchor时的局部参考，绝对值<=100cm。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ScreenMarginPx = 24.f; // 屏幕边缘安全距8..80逻辑px。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FLinearColor HighlightColor = FLinearColor(.25f,.9f,.75f,1.f); // 旧CDO差分兼容值保持；当前作者显式存纯白，整体外沿PP固定白色，不复制选择状态。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float ErrorSeconds = 2.f; // 本人短错误提示显示秒，.5..5。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> HighlightMaterial; // 当前为本人Camera的纯白外沿PostProcess材质；旧背壳资源留历史，不改原枪材质。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> HighlightMaskMaterial; // 本人独立Capture的最终二值外沿PP（Python:highlight_mask_material）；由真实Editor配置，不给旧DA/CDO隐式资源回退。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float DeviceLeaseSeconds = 10.f; // 设备租约游戏秒5..30，由权威会话服务消费。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float DeviceHeartbeatSeconds = 2.f; // 心跳秒，>0且lease>=3×心跳，不存档。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector GunShopCenter = FVector(500,800,0); // Hub地面框相对中心cm，西向开口。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector GunShopInteriorSize = FVector(1800,1400,360); // 完整内尺寸cm，不含20cm墙厚。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector CasinoCenter = FVector(500,-800,0); // 同Hub相对中心cm，独立西向入口。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) FVector CasinoInteriorSize = FVector(1400,1200,360); // 完整内尺寸cm。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float EntranceClearWidthCm = 360.f; // 两屋入口净宽cm，墙柱不能侵占。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float GunShopMainLaneCm = 400.f; // 作者中央通行带目标宽cm；不等于四人实测。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) float CasinoMainLaneCm = 500.f; // 中央通行带目标宽cm。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FDemoHubWeaponOffer> WeaponOffers; // 唯一商品目录，不存个人购买状态。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FDemoHubDisplayBinding> Displays; // 当前三个墙枪真实作者变换。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FDemoHubCasinoBinding> Devices; // 七玩法各一实际入口。
    UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FDemoHubShopAnchor> Anchors; // 后续P1/返回/出生锚点，本批只作者化。

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hub Shop|Authored Environment") bool bUseAuthoredEnvironment = false; // DA级环境选择；默认false兼容未含新字段的旧DA。随作者包持久化，不写玩家/战役存档或复制为交易状态；失败回退仍由Blockout负责。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hub Shop|Authored Environment") FName AuthoredEnvironmentRootTag = TEXT("HubShopEnv.Root"); // TA持久环境根的可信固定Tag，只允许此精确值；与世界唯一根核验分开，不是Gameplay身份或经济复制字段。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hub Shop|Authored Environment") FName AuthoredEnvironmentMemberTag = TEXT("HubShopEnv.Member"); // TA持久环境成员的可信固定Tag，只允许此精确值；只用于当前环境闭包查询，不进入玩家存档/交易复制。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hub Shop|Authored Environment") FName AuthoredEnvironmentId = NAME_None; // TA作者冻结的稳定环境身份；启用时非空，须与真实根/成员匹配，不使用ActorLabel或数组顺序，不存玩家进度/交易复制。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hub Shop|Authored Environment") int32 AuthoredEnvironmentRevision = 0; // 作者修订，启用时1..MAX_int32；默认0表示旧/未选择环境，不改变网络/战役版本，不属于经济复制事实。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Hub Shop|Authored Environment") int32 AuthoredEnvironmentItemCount = 0; // 当前修订的完整持久成员数，启用时1..4096；由TA随闭包更新，不硬锁350。只描述资产内容，不存玩家进度/复制交易状态。
};

/** 项目仅选正式DA；不存在时禁商店并给作者错误，不在运行时伪造可免费购买目录。 */
UCLASS(Config=Game, DefaultConfig)
class FPSDEMO_API UDemoHubShopSettings : public UObject
{
    GENERATED_BODY()
public:
    /** 默认指向由Editor创建的独立P0目录；不覆盖武器/近战共享资产。 */
    UDemoHubShopSettings();
    /** 仅World初始化/作者构造时同步加载并验证；调用者以UPROPERTY保活，不在焦点Tick读盘。 */
    UDemoHubShopDefinition* LoadDefinition(FString& Error) const;
    UPROPERTY(Config, EditAnywhere, Category="Hub Shops") TSoftObjectPtr<UDemoHubShopDefinition> DefinitionAsset; // 仅项目可信配置路径，不能由客户端提交。
};
