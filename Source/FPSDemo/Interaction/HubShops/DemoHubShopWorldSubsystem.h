// 职责：本World安全区实体注册/代数/近邻与初次距角LOS校验、双端实体设备存续几何；首次Hub准备后生成白模，不持有钱包/随机/伤害许可。
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoHubShopWorldSubsystem.generated.h"
class UDemoHubShopDefinition;
class ADemoGunDisplay;
class ADemoCasinoDevice;
class ADemoHubShopBlockout;
class ADemoCharacter;

UCLASS()
class FPSDEMO_API UDemoHubShopWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 只Game/PIE建立，不在Editor预览自动生成或注册交互。 */
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    /** 加载/验证唯一DA并依赖原区域服务；不读玩家进度、不创建云实例。 */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    /** 服务端每.5游戏秒等待原区域Ready和逻辑Hub，成功后停止轮询；客户端只收复制Actor。 */
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    /** 卸载停止自有Timer、失效白名单/代数并清自有Actor，不操作新World或玩家存档。 */
    virtual void Deinitialize() override;
    /** World持有的已验证目录，只读借用；缺失返回nullptr，不能即时免费回退。 */
    const UDemoHubShopDefinition* GetDefinition() const;
    /** 本World服务器分配的非零注册代数；客户端尚未收到作者根时为0。 */
    uint32 GetWorldEpoch() const;
    /** 查当前唯一注册且已就绪展示；重复ID一律拒绝，不采用最近任意Actor。 */
    ADemoGunDisplay* FindDisplay(FName DisplayId) const;
    /** 查当前唯一注册设备，未结局与设备生命周期独立。 */
    ADemoCasinoDevice* FindDevice(FName DeviceId) const;
    /** Pawn须服务器当前Avatar；沿共享眼位/原控制视向复核Hub/生命/准备/距角LOS/代数，不取客户端原点；Error说明、OutCode可选稳定码，无经济更改。 */
    bool ValidateUse(const ADemoCharacter* Pawn,const AActor* Target,uint32 ExpectedEpoch,FString& Error,FName* OutCode=nullptr) const;
    /** 已获物理租约的双端存续检查：Pawn/Device须本World当前注册对象，沿共享眼位核Hub/生命/代数/距离/前侧/LOS，不要求仍瞄整机中心。
     * ExpectedEpoch来自原批准快照，Error/可选OutCode为失败原因；本函数不签发或续租Token，真实下注仍须精确首次命中控件。 */
    bool ValidateCasinoMaintain(const ADemoCharacter* Pawn,const ADemoCasinoDevice* Device,uint32 ExpectedEpoch,FString& Error,FName* OutCode=nullptr) const;
    /** 本机扫描已注册至多3+7目标；Position/Radius世界cm，输出清空后填近邻，不每帧遍历World。 */
    void GatherNearby(const FVector& Position,float Radius,TArray<ADemoGunDisplay*>& OutDisplays,TArray<ADemoCasinoDevice*>& OutDevices) const;
    /** Actor BeginPlay/复制就绪时调用；可信DA身份一致才登记，重复通知幂等。 */
    void RegisterDisplay(ADemoGunDisplay* Display);
    /** Actor卸载撤销，并提升服务器代数令迟到请求失效。 */
    void UnregisterDisplay(ADemoGunDisplay* Display);
    /** 仅登记固定七Mode设备，不签发占用租约或下注。 */
    void RegisterDevice(ADemoCasinoDevice* Device);
    /** 释放注册不清个人持续局；会话服务另行释放租约。 */
    void UnregisterDevice(ADemoCasinoDevice* Device);
    /** 只接受本World唯一作者根的服务器复制代数；0表示作者根卸载。 */
    void ReceiveWorldEpoch(ADemoHubShopBlockout* Source,uint32 Epoch);
private:
    /** 两类几何检查共用的本World/Avatar/阶段/生命门；bRequireAuthority仅旧初次准入为true，物理存续可供客户端预测但不授服务器权限。
     * Target/ExpectedEpoch为本次可信注册身份，Error/Code仅同步输出，成功均清空，不读客户端上传位置或Token。 */
    bool ValidateInteractionContext(const ADemoCharacter* Pawn,const AActor* Target,uint32 ExpectedEpoch,bool bRequireAuthority,FString& Error,FName& Code) const;
    /** 等待依赖后一次生成；循环入口仅Trace，状态/失败另记有界业务日志。 */
    void TryCreateBlockout();
    /** 注册变化令旧意图失效；当前根复制新代数，非网络结构/持久字段。 */
    void BumpEpoch();
    /** 失败仅撤销本服务自有生成物；不清地图Actor、旧区域或存档。 */
    void ClearGenerated();
    UPROPERTY(Transient) TObjectPtr<UDemoHubShopDefinition> Definition; // World保活已载入DA/软依赖。
    TMap<FName,TWeakObjectPtr<ADemoGunDisplay>> Displays; // Actor由World拥有，至多三正式绑定。
    TMap<FName,TWeakObjectPtr<ADemoCasinoDevice>> Devices; // 至多七正式设备。
    TSet<FName> ConflictedDisplays; // 重复身份整项禁用，不静默任选一个。
    TSet<FName> ConflictedDevices; // 同上；重新准备World才解除作者歧义。
    TWeakObjectPtr<ADemoHubShopBlockout> Blockout; // 唯一本World作者根，不强持有旧Level。
    TArray<TWeakObjectPtr<AActor>> Generated; // 本服务只清自己的有界生成集。
    FTimerHandle PollTimer; // 仅首次Hub等待依赖阶段，成功/失败/卸载停止。
    uint32 WorldEpoch=0; // 非零服务器代数，客户端从唯一复制根接收，不写档。
    bool bCreated=false; // 成功生成或接管作者根后幂等，不随普通关间重建。
    bool bStopping=false; // World卸载后拒绝注册/重建和代数回调。
};
