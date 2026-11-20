// 职责：P0枪墙/七设备/两屋作者Actor，仅创建陈列、碰撞与稳定交互锚点；依赖HubShop DA和World注册，绝不扣款、发枪、开奖或开Hub伤害。
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DemoHubShopActors.generated.h"

class UDemoHubShopDefinition;
class USceneComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UPrimitiveComponent;
class UTextRenderComponent;
class UBoxComponent;
class UStaticMesh;

/** 枪墙上没有武器逻辑的真实网格展示；身份由服务器/作者DA配置，本机选中由Focus组件另建层。 */
UCLASS(BlueprintType)
class FPSDEMO_API ADemoGunDisplay : public AActor
{
    GENERATED_BODY()
public:
    /** 创建无Pawn阻挡查询盒、两种网格与InfoAnchor；不载个人数据或生成可射击Actor。 */
    ADemoGunDisplay();
    /** 作者/服务器生成后填入可信DA及DisplayId，不从客户端任意路径配置。 */
    UFUNCTION(BlueprintCallable,Category="Hub Shop") bool Configure(UDemoHubShopDefinition* InDefinition,FName InDisplayId);
    /** 编辑器重建仅更新陈列，实际运行注册在BeginPlay进行。 */
    virtual void OnConstruction(const FTransform& Transform) override;
    /** 启动校准网格后注册到本World白名单，不拥有经济权限。 */
    virtual void BeginPlay() override;
    /** 卸载先撤销白名单，迟到意图不能使用已销毁展示。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** 同步稳定作者身份及可信DA；选择/钱包/拥有权不复制在展示上。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    /** 世界cm焦点中心，本机扫描和服务器LOS共用，不能由客户端自报。 */
    FVector GetFocusPoint() const;
    /** 世界cm浮签锚点，UI负责DPI与屏幕边缘避让。 */
    FVector GetInfoPoint() const;
    /** 只读当前实际可见Primitive，供本人无碰撞高亮复制网格；不修改共享材质。 */
    UPrimitiveComponent* GetDisplayMesh() const;
    /** 作者测量选中mesh资产图形bounds经当前组件变换的世界AABB（cm）；不含Focus/托钩/物理资产，不是顶点窄相，失败输出零并返回false。 */
    UFUNCTION(BlueprintPure,Category="Hub Shop") bool GetDisplayModelBounds(FVector& OutMin,FVector& OutMax) const;
    /** 查询盒只用于直接准星候选，不参与伤害/玩家阻挡。 */
    UBoxComponent* GetFocusBounds() const;
    /** 只有有效CDO及网格成功安装的展示可参与交互。 */
    bool IsDisplayReady() const;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,ReplicatedUsing=OnRep_Config) FName DisplayId; // 世界注册稳定身份。
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,ReplicatedUsing=OnRep_Config) FName OfferId; // 可信DA派生商品键，不能本机改价。
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,ReplicatedUsing=OnRep_Config) FName ShopId; // 当前预留唯一枪店，不是全局钱包。
private:
    /** 身份/DA晚到时重建并补注册，空或未知字段保持不可用。 */
    UFUNCTION() void OnRep_Config();
    /** 同步安装CDO网格；R03以真实mesh bounds居中、贴北墙背板并收紧焦点，旧0保持原作者表现。 */
    bool RefreshDisplay();
    /** 仅撤销本展示的临时无碰撞托钩；重建/失效时调用，不触碰原武器mesh。 */
    void ClearSupports();
    /** Center/Size为展示根局部cm；创建引擎Cube托钩段，无碰撞且不参与真实mesh bounds。 */
    void AddSupport(const FVector& Center,const FVector& Size);
    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_Config) TObjectPtr<UDemoHubShopDefinition> Definition; // 可信项目DA，由Actor保活。
    UPROPERTY() TObjectPtr<USceneComponent> Root; // 真实展示根，随Actor销毁。
    UPROPERTY() TObjectPtr<UBoxComponent> FocusBounds; // 无Pawn碰撞，Visibility查询候选。
    UPROPERTY() TObjectPtr<USceneComponent> InfoAnchor; // Actor拥有的局部作者点。
    UPROPERTY() TObjectPtr<UStaticMeshComponent> StaticDisplay; // 仅陈列，可空。
    UPROPERTY() TObjectPtr<USkeletalMeshComponent> SkeletalDisplay; // 仅陈列，无AnimBP/武器/GAS。
    UPROPERTY() TObjectPtr<UStaticMesh> SupportCube; // Engine基础Cube，R03托钩硬引用，无第三方依赖。
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Supports; // 本Actor每次Refresh替换的两个L形托钩，共四段；无碰撞/无复制经济事实。
    bool bDisplayReady=false; // 本机资源就绪值，不代表购买权限。
};

/** 七玩法入口柜体；Mode只映射既有Casino，经济和租约由原事务及会话服务处理。 */
UCLASS(BlueprintType)
class FPSDEMO_API ADemoCasinoDevice : public AActor
{
    GENERATED_BODY()
public:
    /** 创建有碰撞柜体和可识别静态面板，不生成未来随机结果。 */
    ADemoCasinoDevice();
    /** 作者/服务器只选择可信DA中的稳定设备ID，Mode从DA派生。 */
    UFUNCTION(BlueprintCallable,Category="Hub Shop") bool Configure(UDemoHubShopDefinition* InDefinition,FName InDeviceId);
    /** 编辑器重建几何，不注册经济会话。 */
    virtual void OnConstruction(const FTransform& Transform) override;
    /** 资源/身份验证后注册白名单，七玩法状态仍归原个人账本。 */
    virtual void BeginPlay() override;
    /** 离开World撤销注册；个人持续局不因设备销毁删除。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** 只复制设备身份/Mode/可信DA，未开雷位、余额、Seed不在此Actor。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    /** 面前世界cm交互点，用于原设备页面授权的距角/LOS。 */
    FVector GetFocusPoint() const;
    /** 设备公开提示世界cm锚点，不包含私人局面。 */
    FVector GetInfoPoint() const;
    /** 柜体查询盒，供直接准星命中判断。 */
    UBoxComponent* GetFocusBounds() const;
    /** 身份/Mode已与目录一致，几何创建成功；不表示空闲或可下注。 */
    bool IsDeviceReady() const;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,ReplicatedUsing=OnRep_Config) FName DeviceId; // 世界内唯一键。
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,ReplicatedUsing=OnRep_Config) int32 Mode=INDEX_NONE; // 原Casino Mode0..6，不是UI Choice。
private:
    /** 延迟复制后重建可识别设备并补注册；未知身份保持禁用。 */
    UFUNCTION() void OnRep_Config();
    /** 正式实测Profile或旧P0静态提示独立重建；正式缺件禁用，无开奖/概率副本。 */
    bool RefreshDevice();
    /** Piece/Position/Size为本机静态作者几何；Collision仅柜体阻挡，所有小面板无碰撞。 */
    UStaticMeshComponent* AddPiece(UStaticMesh* Piece,const FVector& Position,const FVector& Size);
    /** 精确销毁本Actor动态面板，重复编辑不累积对象。 */
    void ClearPieces();
    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_Config) TObjectPtr<UDemoHubShopDefinition> Definition; // 可信DA强资源。
    UPROPERTY(VisibleAnywhere) TObjectPtr<class UDemoCasinoControlsComponent> PhysicalControls; // 正式Profile/显式CP01创建稳定Query与中性件；只改本Casino，不移动R03根。
    UPROPERTY() TObjectPtr<USceneComponent> Root; // 设备地面框。
    UPROPERTY() TObjectPtr<UBoxComponent> Collision; // 真实柜体根范围，不挡主通道。
    UPROPERTY() TObjectPtr<USceneComponent> FocusAnchor; // 本设备交互点。
    UPROPERTY() TObjectPtr<UTextRenderComponent> Label; // 离线字体可读英文玩法牌，无经济数据。
    UPROPERTY() TObjectPtr<UStaticMesh> Cube; // 引擎基础几何硬引用保证Cook。
    UPROPERTY() TObjectPtr<UStaticMesh> Cylinder; // 反应堆/转轴等静态轮廓。
    UPROPERTY() TObjectPtr<UStaticMesh> Sphere; // 斗兽/靶场静态识别标记。
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Pieces; // 本Actor有界面板，由Refresh完全替换。
    int32 BuiltMode=INDEX_NONE; // 幂等重建标记，不存档/复制。
};

/** 两屋逻辑根；完整持久TA闭包替代纯外观，失败恢复旧白模，始终保留锚点/注册。 */
UCLASS(BlueprintType)
class FPSDEMO_API ADemoHubShopBlockout : public AActor
{
    GENERATED_BODY()
public:
    /** 创建复制根、引擎Cube及无复制Hub氛围消费者；纯Editor构造不应用PP。 */
    ADemoHubShopBlockout();
    /** 明确作者DA与服务器World代数；可在纯Editor调用生成实际待保存Actor结构。 */
    UFUNCTION(BlueprintCallable,Category="Hub Shop") bool Configure(UDemoHubShopDefinition* InDefinition,int32 InWorldEpoch);
    /** Editor构建与运行同路，重建本Actor组件，并经专用环境门选择/抑制TA成员。 */
    virtual void OnConstruction(const FTransform& Transform) override;
    /** 晚到复制时重建并同步World代数，客户端无生成库存/事件权限。 */
    virtual void BeginPlay() override;
    /** 根卸载撤销服务代数，Actor拥有的几何随Root销毁。 */
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** 只同步可信定义及WorldEpoch，不复制事件/个人数据。 */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    /** 服务注册变化后只更新代数，不重建碰撞或资源。 */
    void SetWorldEpoch(uint32 InWorldEpoch);
    /** 作者检查返回真实本Actor构建的静态几何数，不代表导航/游戏验收。 */
    UFUNCTION(BlueprintPure,Category="Hub Shop") int32 GetGeometryCount() const;
    /** 纯Editor保存读回的版本检查；Blueprint使用int32，运行注册接口仍是uint32。 */
    UFUNCTION(BlueprintPure,Category="Hub Shop") int32 GetAuthorEpoch() const;
    /** 作者读回本次是否实际选中完整TA闭包；不把DA开关值当已启用，不代表运行/艺术验收。 */
    UFUNCTION(BlueprintPure,Category="Hub Shop|Authored Environment") bool IsUsingAuthoredEnvironment() const;
    /** 最近环境选择的明确回退原因；成功为空，仅本World瞬态，不复制/存档。 */
    UFUNCTION(BlueprintPure,Category="Hub Shop|Authored Environment") FString GetEnvironmentSelectionError() const;
    /** 作者检查稳定锚点世界变换；未知ID返回false，不寻找同名任意Actor。 */
    UFUNCTION(BlueprintCallable,Category="Hub Shop") bool GetAnchorTransform(FName AnchorId,FTransform& OutTransform) const;
private:
    /** 可信DA到达时只重建对应白模，失效时不给出可用假几何。 */
    UFUNCTION() void OnRep_Definition();
    /** 代数复制晚到时只协调注册服务。 */
    UFUNCTION() void OnRep_WorldEpoch();
    /** 先完整校验持久环境；成功跳旧房/柜/牌/灯，失败先抑制专用TA再回白模；锚点始终重建，无玩法副本。 */
    bool RebuildGeometry();
    /** Center/Size为局部cm；基础100cmCube换算，bCollision只用于明确建筑件。 */
    void AddBlock(const FVector& Center,const FVector& Size,bool bCollision=true);
    /** 一屋四墙，西侧分两段留净宽入口；Center/Size为Hub局部cm。 */
    void AddRoom(const FVector& Center,const FVector& Size);
    /** HUB-INTERIOR-EXPOSURE-20261119：仅游戏World且非专用服务器，按Hub局部Center/Size(cm)生成无碰撞Box+有界PP，只覆盖AutoExposureBias；Label仅用于日志。 */
    void AddInteriorExposure(const FVector& Center,const FVector& Size,float Bias,const TCHAR* Label);
    UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> ExposurePieces; // 室内曝光Box/PP，不计入Pieces/GetGeometryCount的16逻辑锚点；重建时完全撤销，不复制不存档。
    UPROPERTY(VisibleAnywhere, Category="Hub Atmosphere") TObjectPtr<class UDemoHubAtmosphereComponent> HubAtmosphere; // 默认子对象独立于白模重建，两店Bias/动态终端仍归原系统；本组件不复制。
    UPROPERTY(EditAnywhere,ReplicatedUsing=OnRep_Definition) TObjectPtr<UDemoHubShopDefinition> Definition; // 数据资产强引用，作者保存/网络可解析。
    UPROPERTY(ReplicatedUsing=OnRep_WorldEpoch) uint32 WorldEpoch=0; // 仅本World会话版本，服务器非零，永不存档。
    UPROPERTY() TObjectPtr<USceneComponent> Root; // Hub冻结地面框，无自身碰撞。
    UPROPERTY() TObjectPtr<UStaticMesh> Cube; // 明确引擎资源，无外部插件。
    UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> Pieces; // 只由本Actor创建，重建时完全撤销。
    TMap<FName,TWeakObjectPtr<USceneComponent>> AnchorComponents; // 本Actor子组件的稳定ID索引，随重建清空。
    bool bUsingAuthoredEnvironment=false; // 本World最近解析结果，仅完整闭包成功才true；不复制配置/交易事实。
    FString EnvironmentSelectionError; // 本次未选中原因，供TA新进程读回；成功清空。
};
