// 职责：GUNFEEL声音/表面配置及CDO资源保活。使用：四枪发射冻结反馈选择、命中服务器只读选Wave；手枪沿FeelReal_V3，步枪V4、霰弹/狙击V5.1（GUNFEEL-PUNCH-20261117）；
// 2026-11-19起普通身体/头部世界命中、射手专属层（命中点3D随距离变小）与设施三档命中为V11专业库（GDC-V11，Sonniss GDC素材，用户试听通过），
// 设施分档/爆炸逻辑见DEVICE-V7、摧毁爆炸素材为V8 CC0实录，Boss钢板与击杀确认沿V5；依赖Editor导入资产，无伤害或存档权限。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DemoCombatFeelSettings.generated.h"

class USoundWave;
class USoundAttenuation;
class USoundConcurrency;

/** 纯表现表面，不影响既有部位伤害倍率；由目标显式声明，不能根据显示名猜测。 */
UENUM(BlueprintType)
enum class EDemoImpactSurface : uint8 { Flesh, Armor, Device };

/** 四枪共用显式表面反馈与本人近枪层。CDO硬引用参与Cook，配置不承载伤害或客户端网络权限。 */
UCLASS(BlueprintType, Config=Game, DefaultConfig, meta=(DisplayName="Demo Combat Feel"))
class FPSDEMO_API UDemoCombatFeelSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    /** CDO阶段加载固定目录（15组，见cpp GroupSpecs）；缺任一资源HasCompleteAssets失败回旧路由，不在开火/命中路径读盘。 */
    UDemoCombatFeelSettings();
    /** WeaponId为发射身份；资源完整、配置/Editor AB开关均允许时返回true，由ShotContext冻结。 */
    bool UseForWeapon(FName WeaponId) const;
    /** 只读检查全部候选资源、时长上限与空间衰减；失败不抛弃原正式声音。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Feedback|Diagnostics") bool HasCompleteAssets() const;
    /** Editor作者审查：Group为Pistol/Rifle/Shotgun/Sniper/BodyLight/BodyHeavy/Head/BossPlate/DeviceT1/DeviceT2/DeviceT3/DeviceExplode/OwnerBody/OwnerHead/ConfirmKill；仅复制CDO保活指针，不暴露写入，未知组返回空并告警。 */
    UFUNCTION(BlueprintCallable, Category="Demo|Feedback|Diagnostics") TArray<USoundWave*> GetCandidateWaves(FName Group) const;
    /** 服务器在实际扣血后调用：Surface为冻结表面，WeaponId为原发射武器，bHead为本颗命中head弱点，DeviceHealthFraction为设施扣血后剩余HP比例（非设施忽略）。
     * Flesh→头部替代身体；否则霰弹/狙击取重、其他取轻（V11专业库）。Armor（敌人钢板）→V5 Boss钢板；Device→V11子弹打重金属，按>66%/33～66%/<33%选T1/T2/T3。返回CDO保活只读Wave，不创建音频组件。 */
    USoundWave* SelectImpact(EDemoImpactSurface Surface, FName WeaponId, bool bHead, float DeviceHealthFraction = 1.f) const;
    /** 设施摧毁时各客户端本地调用：返回V8实录爆炸Wave（氢氧爆鸣/山地炸药2变体随机），目录不完整或bEnabled=false返回nullptr（静默不播）。 */
    USoundWave* SelectDeviceExplosion() const;
    /** Kind为普通0/头部1/致死2；仅bPlayConfirmationSounds开启才返回射手专属层：普通→V9 OwnerBody（湿毛巾/拍打实录短版），头部→V9 OwnerHead（骨折＋人体抽打实录），击杀→V5闷击。 */
    USoundWave* SelectConfirmation(uint8 Kind) const;
    /** Kind同上；返回该等级本人2D线性音量（普通OwnerBodyVolume、头部HeadConfirmationVolume、击杀ConfirmationVolume），越界钳到0..1。 */
    float GetConfirmationVolume(uint8 Kind) const;
    /** 已通过UseForWeapon的稳定ID挑一次本人近枪声：pistol V3×2、rifle V4×3、shotgun/sniper V5.1×3；未知ID返回nullptr沿原武器Cue。 */
    USoundWave* SelectFire(FName WeaponId) const;
    /** WeaponId为发射身份；返回本人近枪线性音量：霰弹/狙击HeavyLocalFireVolume，其余LocalFireVolume，均钳到0..1。 */
    float GetLocalFireVolume(FName WeaponId) const;
    /** 霰弹/狙击属于重型开火与重命中层级；纯表现分类，不影响伤害或射速。 */
    static bool IsHeavyWeapon(FName WeaponId);

    UPROPERTY(Config, EditAnywhere, Category="Candidate") bool bEnabled = true; // 新进程默认使用完整反馈目录；false恢复旧音画路由。
    UPROPERTY(Config, EditAnywhere, Category="Candidate") TArray<FName> WeaponIds = {TEXT("pistol"), TEXT("rifle"), TEXT("shotgun"), TEXT("sniper")}; // 四枪共用新目录；近战仍由独立定义控制。
    UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0",ClampMax="1")) float ContactVolume = .65f; // 世界命中声部的线性增益，非伤害强度。
    UPROPERTY(Config, EditAnywhere, Category="Mix") bool bPlayConfirmationSounds = true; // 2026-11-17起播放V5本人头部/击杀确认；false仍消费等级/HUD但静音。
    UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0",ClampMax="1")) float ConfirmationVolume = .4f; // 本人击杀确认线性增益（V5组合试听0.8×本人开火0.5）。
    UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0",ClampMax="1")) float HeadConfirmationVolume = .55f; // 射手头部层线性增益（3D随距离；11-19起V11专业库）；2026-11-17用户反馈头身分不出，由0.15提高并明显高于身体层。
    UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0",ClampMax="1")) float OwnerBodyVolume = .35f; // 射手普通身体层线性增益（命中点3D经OwnerAttenuation随距离变小；11-19起V11专业库），解决远距世界命中被开火掩蔽。
    UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0",ClampMax="1")) float LocalFireVolume = .5f; // 手枪/步枪本人近枪干声，替代同发旧3D主枪响。
    UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0",ClampMax="1")) float HeavyLocalFireVolume = .63f; // 霰弹/狙击本人近枪干声，约比轻武器高2dB（用户2026-11-17“需要大声一点”）。
    UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0",ClampMax="1")) float DeviceExplosionVolume = .8f; // 设施摧毁爆炸3D线性增益（用户2026-11-17“加一个爆炸的声音”）；衰减用ExplosionAttenuation。
    UPROPERTY() TObjectPtr<USoundAttenuation> ContactAttenuation; // Editor创建的空间衰减，所有世界命中声部复用，不能将Mono原始Wave当2D世界声。
    UPROPERTY() TObjectPtr<USoundAttenuation> ExplosionAttenuation; // 复用V3已审查的ATT_Real_Fire（4000cm线性），摧毁爆炸比命中传得更远。
    UPROPERTY() TObjectPtr<USoundAttenuation> OwnerAttenuation; // 射手专属普通/头部层3D衰减：复用ATT_Real_Fire（4000cm），比世界层2400cm平缓——用户2026-11-17要求“随距离变小”但远处仍听得清。
    UPROPERTY() TObjectPtr<USoundConcurrency> LocalFireConcurrency; // 原生8声部近枪组，匹配步枪650RPM连发与射速加成，不截断0.34s尾音且不无界叠加。
private:
    /** Group为固定组名；返回对应CDO数组，未知组nullptr。只在本类同步借用。 */
    const TArray<TObjectPtr<USoundWave>>* FindGroup(FName Group) const;
    UPROPERTY() TArray<TObjectPtr<USoundWave>> Pistol; // 两个V3手枪短版（用户2026-11-17确认不换）。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> Rifle; // 三个V4步枪开火（用户同意替换V1）。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> Shotgun; // 三个V5.1霰弹开火（V5切反射后加密度），本人2D。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> Sniper; // 三个V5.1狙击开火，0.62s，允许≤0.7s。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> BodyLight; // 三个V11普通敌人轻命中（手枪/步枪）：专业子弹打身体＋刺肉湿层，0.18s。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> BodyHeavy; // 三个V11普通敌人重命中（霰弹按目标合并一次/狙击），降调＋身体撞击，0.24s。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> Head; // 三个V11头部命中（子弹打身体＋骨裂＋头部碎响），替代身体声不叠加，0.20s。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> BossPlate; // 三个V5 Boss防爆钢板，敌人Armor表面使用。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> DeviceT1; // 三个V11设施命中·完好（>66%）：专业子弹打重金属＋少量重击，无电弧，0.20s。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> DeviceT2; // 三个V11设施命中·受损（33～66%）：加重击＋极轻电焊火花，0.22s。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> DeviceT3; // 三个V11设施命中·濒坏（<33%）：最重分量＋轻火花，0.26s。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> DeviceExplode; // 两个V8实录设施摧毁爆炸（氢氧爆鸣/山地炸药＋废金属），1.6s，上限放宽到2.0s。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> OwnerBody; // 三个V11射手身体层（0.10s，专业子弹打身体短版），只播给射手，命中点3D随距离变小。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> OwnerHead; // 三个V11射手头部层（0.10s，子弹打身体＋骨裂短版）。
    UPROPERTY() TArray<TObjectPtr<USoundWave>> ConfirmKill; // 三个V5本人击杀确认2D闷击。
    mutable bool bLoggedInvalidCatalog = false; // 本进程最多一次缺资源/数值诊断，禁止每次开火刷同一警告。
};
