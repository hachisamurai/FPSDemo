// 职责：稳定ID天赋目录及可实例化的临时命名空间。用法：DefaultGame.ini注册内容；FGCObject保活，PS独立持有玩家运行句柄。
#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GAS/Talents/DemoTalentModifiers.h"
#include "DemoTalentCatalog.generated.h"
class UDemoTalentDefinition;
class UAbilitySystemComponent;
struct FDemoGambleProgress;
struct FDemoTalentCatalogCache;
/** 内置定义的具体命名空间类型；UObject基类为Abstract，非Editor包禁止直接NewObject<UObject>。 */
UCLASS(Transient)
class FPSDEMO_API UDemoTalentDefinitionScope final : public UObject
{
    GENERATED_BODY()
public:
    /** 首次创建目录缓存时构造，只提供Outer身份；由FGCObject持有，不引用目录/玩家/World或保存业务数据。 */
    UDemoTalentDefinitionScope();
};
/** UHT生成的构造函数只能看到缓存前置声明；把删除实现放到cpp以保证正确调用FGCObject析构。 */
struct FPSDEMO_API FDemoTalentCatalogCacheDeleter
{
    /** Cache为目录独占缓存；在类型完整的cpp中销毁并注销GC引用器，允许空指针。 */
    void operator()(FDemoTalentCatalogCache* Cache) const;
};
/** 本地可信内容配置；不接受远端资产路径，保留旧15项顺序与存档ID。 */
UCLASS(Config=Game,DefaultConfig)
class FPSDEMO_API UDemoTalentCatalog : public UObject
{
    GENERATED_BODY()
public:
    /** 原生构造只记录调用，首次Entries再构造回退，避免CDO初始化时同步加载。 */
    UDemoTalentCatalog();
    /** UObject销毁末期释放原生缓存；定义在cpp以确保缓存类型完整。 */
    virtual ~UDemoTalentCatalog() override;
    /** 目录进入销毁时解除FGCObject引用；缓存不得持有目录/玩家/World形成反向保活。 */
    virtual void BeginDestroy() override;
    /** 内置ID的定义覆盖；缺失/身份不匹配/无效实现时日志并回退原生。 */
    UPROPERTY(Config,EditAnywhere,Category="Talent") TMap<FName,TSoftObjectPtr<UDemoTalentDefinition>> DefinitionOverrides;
    /** 新天赋本地定义资产；ID/Tag必须唯一，最多64种为协议资源边界，新增ID需同步API白名单。 */
    UPROPERTY(Config,EditAnywhere,Category="Talent") TArray<TSoftObjectPtr<UDemoTalentDefinition>> AdditionalDefinitions;
    /** 游戏线程首次构建稳定有序定义，返回缓存借用视图；FGCObject保活，CDO不持有反射强引用。 */
    const TArray<TObjectPtr<UDemoTalentDefinition>>& Entries() const;
    /** Id为稳定白名单ID；未知返回空，不动态创建任何Tag或对象路径。 */
    const UDemoTalentDefinition* Find(FName Id) const;
    /** Editor内容工具以Id读取原生回退并复制到新资产；返回只读共享对象，禁止修改默认对象。 */
    UFUNCTION(BlueprintCallable,Category="Talent") const UDemoTalentDefinition* GetNativeDefinition(FName Id) const;
    /** ASC为本人已初始化GAS；只聚合账本和权限GE同时有效的项，供武器冻结。 */
    FDemoTalentModifiers Evaluate(const UAbilitySystemComponent* ASC) const;
    /** Data为已验证账本，返回无副作用聚合值；不要求GA存在。 */
    FDemoTalentModifiers EvaluateProgress(const FDemoGambleProgress& Data) const;
private:
    /** 游戏线程惰性创建原生GC引用器；不在CDO构造期加载资产，返回值仅在目录生命周期内有效。 */
    FDemoTalentCatalogCache& GetRuntimeCache() const;
    /** DefinitionsOuter是缓存持有的临时命名空间；OutDefinitions接收原生回退，不以CDO作为Outer。 */
    void BuildDefaults(UObject* DefinitionsOuter,TArray<TObjectPtr<UDemoTalentDefinition>>& OutDefinitions) const;
    /** 非反射原生缓存；其FGCObject独立向GC报告引用，避免DisregardForGC的CDO引用普通运行时对象。 */
    mutable TUniquePtr<FDemoTalentCatalogCache,FDemoTalentCatalogCacheDeleter> RuntimeCache;
};
