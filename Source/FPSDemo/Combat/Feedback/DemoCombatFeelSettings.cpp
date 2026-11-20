// 职责：CDO预载四枪近枪声、V11专业库（Sonniss GDC）普通/头部命中、射手层与设施分档、V8实录摧毁爆炸、V5 Boss钢板/击杀确认；为开火/命中/确认提供只读选择。
// 依赖Editor作者资产（import_punch_v5_sfx.py、import_punch_v6_sfx.py）及固定并发，运行查询不读盘、不改变伤害或网络布局。
#include "Combat/Feedback/DemoCombatFeelSettings.h"
#include "Debug/DemoLog.h"
#include "HAL/IConsoleManager.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
#if WITH_EDITOR
// Editor兼容旧命令名，只切后续开火；ShotContext冻结在飞弹选择，不写进度、不进入非Editor包体。
TAutoConsoleVariable<int32> UseFeelV1(TEXT("Demo.Feel.UseV1"), 1, TEXT("0=legacy feedback; 1=current catalog (V3 pistol, V4 rifle, V5.1 shotgun/sniper, V5 hits), next shot only; command name retained for compatibility"), ECVF_Default);
#endif
/** 一组固定资源的作者合同：名称（审查接口用）、实有独立变体数、%02d对象路径格式与单条时长上限秒。 */
struct FFeelGroupSpec
{
    const TCHAR* Name; // 稳定组名，GetCandidateWaves与审查脚本共用。
    int32 Count; // 实际独立源数量，不复制素材凑数。
    const TCHAR* PathFormat; // 含一个%02d的包路径，变体序号从1开始。
    float MaxSeconds; // HasCompleteAssets时长上限；狙击0.62s单独放宽到0.7s，其余≤0.5s。
};
constexpr const TCHAR* PunchRoot = TEXT("/Game/Audio/SFX/Feedback/Combat/FeelPunch_V5"); // V5批导入根目录（开火/Boss钢板/击杀），仅日志参考。
constexpr const TCHAR* Gdc11Root = TEXT("/Game/Audio/SFX/Feedback/Combat/FeelGDC_V11"); // V11专业库（Sonniss GDC）批导入根目录（命中/射手层/设施分档），仅日志参考。
/** 顺序与构造函数Destinations、FindGroup一致；修改需同步Scripts/Audio/import_gdc_v11_sfx.py、import_real_v8_device_sfx.py与Tools/Unreal/audit_punch_v5_sfx.py。 */
const FFeelGroupSpec GroupSpecs[] = {
    {TEXT("Pistol"),      2, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelReal_V3/Pistol/Waves/SW_Real_Pistol_%02d_Short"), .5f},
    {TEXT("Rifle"),       3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelPunch_V5/Fire/Waves/SW_Punch_Rifle_Fire_%02d"), .5f},
    {TEXT("Shotgun"),     3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelPunch_V5/Fire/Waves/SW_Punch51_Shotgun_Fire_%02d"), .5f},
    {TEXT("Sniper"),      3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelPunch_V5/Fire/Waves/SW_Punch51_Sniper_Fire_%02d"), .7f},
    // 敌人身体/头部世界层与射手专属层、设施命中三档：2026-11-19用户否决V9（像拍巴掌/连射乱）、V8设施（像敲柜子/电弧怪）、V10（薄），
    // V11以Sonniss GDC专业素材（Gamemaster子弹打身体/打重金属等）为主体试听通过；V9/V8/V10资产保留。摧毁爆炸仍为V8。
    {TEXT("BodyLight"),   3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelGDC_V11/Impact/Waves/SW_GDC11_Hit_Body_Light_%02d"), .5f},
    {TEXT("BodyHeavy"),   3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelGDC_V11/Impact/Waves/SW_GDC11_Hit_Body_Heavy_%02d"), .5f},
    {TEXT("Head"),        3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelGDC_V11/Impact/Waves/SW_GDC11_Hit_Head_%02d"), .5f},
    {TEXT("BossPlate"),   3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelPunch_V5/Impact/Waves/SW_Punch5_Hit_BossPlate_%02d"), .5f},
    {TEXT("DeviceT1"),    3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelGDC_V11/Device/Waves/SW_GDC11_Device_T1_%02d"), .5f},
    {TEXT("DeviceT2"),    3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelGDC_V11/Device/Waves/SW_GDC11_Device_T2_%02d"), .5f},
    {TEXT("DeviceT3"),    3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelGDC_V11/Device/Waves/SW_GDC11_Device_T3_%02d"), .5f},
    {TEXT("DeviceExplode"), 2, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelReal_V8/Device/Waves/SW_Real8_Device_Explode_%02d"), 2.f},
    {TEXT("OwnerBody"),   3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelGDC_V11/Owner/Waves/SW_GDC11_Owner_Body_%02d"), .5f},
    {TEXT("OwnerHead"),   3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelGDC_V11/Owner/Waves/SW_GDC11_Owner_Head_%02d"), .5f},
    {TEXT("ConfirmKill"), 3, TEXT("/Game/Audio/SFX/Feedback/Combat/FeelPunch_V5/Confirm/Waves/SW_Punch5_Confirm_Kill_%02d"), .5f},
};
/** Values由CDO保活且在UseForWeapon核验组数；接受实有的1..4个资源（设施4变体），不在命中制造重复变体或读盘。 */
USoundWave* Pick(const TArray<TObjectPtr<USoundWave>>& Values)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    if (Values.IsEmpty() || Values.Num() > 4 || Values.Contains(nullptr))
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_PICK rejected incomplete group count=%d"), Values.Num());
        return nullptr;
    }
    return Values[FMath::RandRange(0, Values.Num() - 1)].Get();
}
/** Value为配置线性增益；有限且位于0..1才合法。 */
bool IsUnitGain(float Value) { return FMath::IsFinite(Value) && Value >= 0.f && Value <= 1.f; }
}

UDemoCombatFeelSettings::UDemoCombatFeelSettings()
{
    DEMO_LOG_CALL();
    LocalFireConcurrency = CreateDefaultSubobject<USoundConcurrency>(TEXT("FeelLocalFireConcurrency")); // CDO拥有只读组，不在开火时创建并发配置。
    LocalFireConcurrency->Concurrency.MaxCount = 8; // 步枪650RPM（0.092s间隔）叠射速加成时0.34s尾音可同时4～7声部，8声部避免连发截断且保持有界。
    LocalFireConcurrency->Concurrency.bLimitToOwner = true;
    LocalFireConcurrency->Concurrency.ResolutionRule = EMaxConcurrentResolutionRule::StopOldest;
    LocalFireConcurrency->Concurrency.VoiceStealReleaseTime = .015f; // 替换短淡出，避免硬切爆音；不授予玩法射速。
    // 固定路径仅在构造时读取；同步构造期间借用成员数组，无异步捕获。顺序必须与GroupSpecs一致。
    TArray<TObjectPtr<USoundWave>>* Destinations[] = {&Pistol,&Rifle,&Shotgun,&Sniper,&BodyLight,&BodyHeavy,&Head,&BossPlate,&DeviceT1,&DeviceT2,&DeviceT3,&DeviceExplode,&OwnerBody,&OwnerHead,&ConfirmKill};
    static_assert(UE_ARRAY_COUNT(Destinations) == UE_ARRAY_COUNT(GroupSpecs), "Feel catalog destinations must match GroupSpecs");
    for (int32 Group = 0; Group < UE_ARRAY_COUNT(GroupSpecs); ++Group)
        for (int32 Variant = 1; Variant <= GroupSpecs[Group].Count; ++Variant)
        {
            const FString AssetPath = FString(GroupSpecs[Group].PathFormat).Replace(TEXT("%02d"), *FString::Printf(TEXT("%02d"), Variant)); // UE5.4 Printf只接受字面量格式，故替换占位符；Optional延迟到Get才使用路径，本地FString必须活到Get返回。
            ConstructorHelpers::FObjectFinderOptional<USoundWave> Asset(*AssetPath, LOAD_Quiet | LOAD_NoWarn);
            Destinations[Group]->Add(Asset.Get());
            if (!Asset.Get()) UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CATALOG missing source=%s"), *AssetPath);
        }
    ConstructorHelpers::FObjectFinderOptional<USoundAttenuation> Attenuation(TEXT("/Game/Audio/SFX/Feedback/Combat/FeelReal_V3/ATT_Real_Impact"), LOAD_Quiet | LOAD_NoWarn);
    ContactAttenuation = Attenuation.Get(); // 沿V3已审查的120/2400cm球形衰减，新命中声不另建衰减。
    ConstructorHelpers::FObjectFinderOptional<USoundAttenuation> FireAttenuation(TEXT("/Game/Audio/SFX/Feedback/Combat/FeelReal_V3/ATT_Real_Fire"), LOAD_Quiet | LOAD_NoWarn);
    ExplosionAttenuation = FireAttenuation.Get(); // 摧毁爆炸复用V3开火衰减（4000cm），传得比命中远，不另建资产。
    OwnerAttenuation = FireAttenuation.Get(); // 射手专属层同用4000cm衰减：随距离变小但比世界层（2400cm）平缓。
    UE_LOG(LogFPSDemo, Log, TEXT("FEEL_CATALOG assembled pistol=V3/2 rifle=V4/3 shotgun,sniper=V5.1/3 body,head,owner,device_tiers=GDC11 root=%s explode=RealV8 boss,kill=V5 root=%s; completeness checked before route selection"), Gdc11Root, PunchRoot);
}

const TArray<TObjectPtr<USoundWave>>* UDemoCombatFeelSettings::FindGroup(FName Group) const
{
    DEMO_LOG_TICK();
    const TArray<TObjectPtr<USoundWave>>* Values[] = {&Pistol,&Rifle,&Shotgun,&Sniper,&BodyLight,&BodyHeavy,&Head,&BossPlate,&DeviceT1,&DeviceT2,&DeviceT3,&DeviceExplode,&OwnerBody,&OwnerHead,&ConfirmKill}; // 与GroupSpecs同序。
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(GroupSpecs); ++Index)
        if (Group == FName(GroupSpecs[Index].Name)) return Values[Index];
    return nullptr;
}

bool UDemoCombatFeelSettings::HasCompleteAssets() const
{
    DEMO_LOG_TICK();
    if (!ContactAttenuation || !LocalFireConcurrency || !ContactAttenuation->Attenuation.bAttenuate || !ContactAttenuation->Attenuation.bSpatialize
        || !ExplosionAttenuation || !ExplosionAttenuation->Attenuation.bAttenuate || !ExplosionAttenuation->Attenuation.bSpatialize
        || !OwnerAttenuation || !OwnerAttenuation->Attenuation.bAttenuate || !OwnerAttenuation->Attenuation.bSpatialize
        || !IsUnitGain(ContactVolume) || !IsUnitGain(ConfirmationVolume) || !IsUnitGain(HeadConfirmationVolume) || !IsUnitGain(OwnerBodyVolume) || !IsUnitGain(DeviceExplosionVolume)
        || !IsUnitGain(LocalFireVolume) || !IsUnitGain(HeavyLocalFireVolume))
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CATALOG invalid attenuation/concurrency/mix"));
        return false;
    }
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(GroupSpecs); ++Index)
    {
        const TArray<TObjectPtr<USoundWave>>* Group = FindGroup(GroupSpecs[Index].Name); // 本次同步检查借用固定CDO资源组。
        if (!Group || Group->Num() != GroupSpecs[Index].Count || Group->Contains(nullptr))
        {
            UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CATALOG incomplete group=%s actual=%d expected=%d"), GroupSpecs[Index].Name, Group ? Group->Num() : -1, GroupSpecs[Index].Count);
            return false;
        }
        for (const USoundWave* Wave : *Group)
            if (Wave->NumChannels != 1 || !FMath::IsFinite(Wave->Duration) || Wave->Duration <= 0.f || Wave->Duration > GroupSpecs[Index].MaxSeconds)
            {
                UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CATALOG invalid short wave=%s limit=%.2f"), *GetNameSafe(Wave), GroupSpecs[Index].MaxSeconds);
                return false;
            }
    }
    return true;
}

bool UDemoCombatFeelSettings::UseForWeapon(FName WeaponId) const
{
    DEMO_LOG_TICK();
#if WITH_EDITOR
    if (UseFeelV1.GetValueOnGameThread() == 0)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_ROUTE editor AB disabled"));
        return false;
    }
#endif
    const bool bSupportedWeapon = WeaponId == TEXT("pistol") || WeaponId == TEXT("rifle") || WeaponId == TEXT("shotgun") || WeaponId == TEXT("sniper"); // 原四个稳定身份，未知扩展ID不自动取得表现许可。
    if (!bEnabled || !WeaponIds.Contains(WeaponId) || !bSupportedWeapon)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_ROUTE disabled or unconfigured weapon=%s"), *WeaponId.ToString());
        return false;
    }
    if (HasCompleteAssets()) return true;
    if (!bLoggedInvalidCatalog) { bLoggedInvalidCatalog = true; UE_LOG(LogFPSDemo, Warning, TEXT("FEEL_CATALOG legacy fallback: incomplete V3/V4/V5 waves, attenuation or invalid gains; import assets then reload process")); }
    return false;
}

TArray<USoundWave*> UDemoCombatFeelSettings::GetCandidateWaves(FName Group) const
{
    DEMO_LOG_CALL();
    const TArray<TObjectPtr<USoundWave>>* Values = FindGroup(Group); // 只允许固定组，不反射修改CDO。
    TArray<USoundWave*> Result; // 此次诊断返回的指针副本由原CDO强引用保活，不持有Pawn/目标。
    if (!Values) { UE_LOG(LogFPSDemo, Warning, TEXT("FEEL_AUDIT unknown group=%s"),*Group.ToString()); return Result; }
    for (USoundWave* Wave : *Values) Result.Add(Wave);
    return Result;
}

bool UDemoCombatFeelSettings::IsHeavyWeapon(FName WeaponId)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs id=%s"), __FUNCTION__, *WeaponId.ToString()); // 静态函数无this，DEMO_LOG_TICK不可用；高频仅VeryVerbose。
    return WeaponId == TEXT("shotgun") || WeaponId == TEXT("sniper");
}

USoundWave* UDemoCombatFeelSettings::SelectImpact(EDemoImpactSurface Surface, FName WeaponId, bool bHead, float DeviceHealthFraction) const
{
    DEMO_LOG_TICK();
    // 剧本D03：普通敌人为穿厚工业服的人类（Flesh），Boss为外骨骼防爆钢板（Armor）；设施为大型供电设备，子弹打重金属随剩余血量加重（用户2026-11-17选择分档；11-19起素材为V11专业库，电弧仅极轻火花），不影响伤害分类。
    if (Surface == EDemoImpactSurface::Device)
    {
        const float Fraction = FMath::IsFinite(DeviceHealthFraction) ? DeviceHealthFraction : 1.f; // 非法值按完好处理，不因异常数值播放濒坏声。
        return Pick(Fraction > .66f ? DeviceT1 : Fraction > .33f ? DeviceT2 : DeviceT3);
    }
    if (Surface == EDemoImpactSurface::Armor) return Pick(BossPlate);
    if (bHead) return Pick(Head); // 头部2.0倍区替代身体声，不叠加。
    return Pick(IsHeavyWeapon(WeaponId) ? BodyHeavy : BodyLight); // 霰弹已由服务器按Shot+目标准入一次，不按弹丸数叠加。
}

USoundWave* UDemoCombatFeelSettings::SelectConfirmation(uint8 Kind) const
{
    DEMO_LOG_TICK();
    if (!bPlayConfirmationSounds)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_CONFIRM deliberately disabled kind=%u"), static_cast<uint32>(Kind));
        return nullptr; // 有意静音；调用方仍先消费等级/HUD，不能把静音当缺资源降级。
    }
    // 普通命中也有射手专属层：用户2026-11-17反馈远距3D命中被开火掩蔽、听不清，选择“只有射手听得到的命中层”；11-18起命中点3D随距离变小，11-19起素材为V11专业库。
    return Pick(Kind >= 2 ? ConfirmKill : Kind == 1 ? OwnerHead : OwnerBody);
}

USoundWave* UDemoCombatFeelSettings::SelectDeviceExplosion() const
{
    DEMO_LOG_CALL();
    if (!bEnabled || !HasCompleteAssets())
    {
        UE_LOG(LogFPSDemo, Log, TEXT("FEEL_DEVICE_EXPLODE skipped enabled=%d catalog_incomplete"), bEnabled); // 设施摧毁一场至多两次，Log级不刷屏。
        return nullptr; // 静默不播，不回退旧资源；击碎事实与视觉不受影响。
    }
    return Pick(DeviceExplode);
}

float UDemoCombatFeelSettings::GetConfirmationVolume(uint8 Kind) const
{
    DEMO_LOG_TICK();
    return FMath::Clamp(Kind >= 2 ? ConfirmationVolume : Kind == 1 ? HeadConfirmationVolume : OwnerBodyVolume, 0.f, 1.f);
}

USoundWave* UDemoCombatFeelSettings::SelectFire(FName WeaponId) const
{
    DEMO_LOG_TICK();
    if (WeaponId == TEXT("pistol")) return Pick(Pistol);
    if (WeaponId == TEXT("rifle")) return Pick(Rifle);
    if (WeaponId == TEXT("shotgun")) return Pick(Shotgun);
    if (WeaponId == TEXT("sniper")) return Pick(Sniper);
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("FEEL_FIRE use weapon Cue for id=%s"), *WeaponId.ToString());
    return nullptr; // 未知扩展武器沿各自Cue，不借用四枪身份。
}

float UDemoCombatFeelSettings::GetLocalFireVolume(FName WeaponId) const
{
    DEMO_LOG_TICK();
    return FMath::Clamp(IsHeavyWeapon(WeaponId) ? HeavyLocalFireVolume : LocalFireVolume, 0.f, 1.f); // 口径阶梯：重武器本人层更响，参考射击游戏常规混音层级。
}
