#include "Combat/DemoEnemyHitZones.h"
#include "AI/DemoEnemy.h"
#include "Components/SkeletalMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"

UDemoEnemyHitProfile::UDemoEnemyHitProfile()
{
    DEMO_LOG_CALL();
    // 这些规则是缺失资产时的完整回退；来源JSON和生成后的DataAsset必须保持同样的骨名约定。
    FDemoEnemyHitZoneRule Body; // 机身装甲、镜头、推进器及Boss护板/背环承受机身倍率。
    Body.Region = EDemoEnemyHitRegion::Body;
    Body.DamageMultiplier = 1.f;
    Body.BoneNames = {TEXT("body"), TEXT("aim_pitch"), TEXT("armor_l"), TEXT("armor_r"), TEXT("fin_l"), TEXT("fin_r"),
        TEXT("thruster_l"), TEXT("thruster_r"), TEXT("shutter_top"), TEXT("shutter_right"), TEXT("shutter_bottom"), TEXT("shutter_left"),
        TEXT("halo"), TEXT("halo_segment_01"), TEXT("halo_segment_02"), TEXT("halo_segment_03"), TEXT("halo_segment_04"), TEXT("halo_segment_05"), TEXT("halo_segment_06")};
    FDemoEnemyHitZoneRule Arm; // 肩部到末端的所有可命中机械臂骨；左右镜像使用同一倍率。
    Arm.Region = EDemoEnemyHitRegion::Arm;
    Arm.DamageMultiplier = .65f;
    Arm.BoneNames = {TEXT("arm_l"), TEXT("arm_r"), TEXT("forearm_l"), TEXT("forearm_r"), TEXT("weapon_l"), TEXT("weapon_r"),
        TEXT("shoulder_l"), TEXT("shoulder_r"), TEXT("wrist_l"), TEXT("wrist_r"), TEXT("claw_l_01"), TEXT("claw_l_02"), TEXT("claw_l_03"),
        TEXT("claw_r_01"), TEXT("claw_r_02"), TEXT("claw_r_03")};
    FDemoEnemyHitZoneRule Core; // 只匹配实际core刚体，不把其父骨或外部护板算成核心。
    Core.Region = EDemoEnemyHitRegion::Core;
    Core.DamageMultiplier = 2.f;
    Core.BoneNames = {TEXT("core")};
    Regions = {Body, Arm, Core};
    NonHittableBones = {TEXT("root"), TEXT("aim_yaw")};
}

bool UDemoEnemyHitProfile::Validate() const
{
    DEMO_LOG_CALL();
    if (Regions.Num() != 3 || !NonHittableBones.Contains(TEXT("root")) || !NonHittableBones.Contains(TEXT("aim_yaw")))
    {
        UE_LOG(LogFPSDemo, Error, TEXT("HIT_ZONE_CONFIG requires exactly Body/Arm/Core and explicit root/aim_yaw exclusions"));
        return false;
    }
    TSet<FName> ClaimedBones; // 当前验证内占用的全部骨名，拒绝同区和跨区重复及禁用冲突。
    TSet<EDemoEnemyHitRegion> ClaimedRegions; // 当前验证内出现的稳定区域，必须恰好三种。
    for (const FName Bone : NonHittableBones) // 禁用骨同样不能是None或重复，否则配置无法被完整审计。
    {
        if (Bone.IsNone() || ClaimedBones.Contains(Bone))
        {
            UE_LOG(LogFPSDemo, Error, TEXT("HIT_ZONE_CONFIG invalid/duplicate excluded bone=%s"), *Bone.ToString());
            return false;
        }
        ClaimedBones.Add(Bone);
    }
    for (const FDemoEnemyHitZoneRule& Rule : Regions) // 倍率只验证有限值和硬上限；0作为明确免疫值允许保存。
    {
        if ((Rule.Region != EDemoEnemyHitRegion::Body && Rule.Region != EDemoEnemyHitRegion::Arm && Rule.Region != EDemoEnemyHitRegion::Core)
            || ClaimedRegions.Contains(Rule.Region) || Rule.BoneNames.IsEmpty()
            || !FMath::IsFinite(Rule.DamageMultiplier) || Rule.DamageMultiplier < 0.f || Rule.DamageMultiplier > 100.f)
        {
            UE_LOG(LogFPSDemo, Error, TEXT("HIT_ZONE_CONFIG invalid/duplicate region=%d multiplier=%f bones=%d"), static_cast<int32>(Rule.Region), Rule.DamageMultiplier, Rule.BoneNames.Num());
            return false;
        }
        ClaimedRegions.Add(Rule.Region);
        for (const FName Bone : Rule.BoneNames) // 一个骨只能产生一个确定倍率，不允许先匹配者隐式获胜。
        {
            if (Bone.IsNone() || ClaimedBones.Contains(Bone))
            {
                UE_LOG(LogFPSDemo, Error, TEXT("HIT_ZONE_CONFIG invalid/duplicate mapped bone=%s"), *Bone.ToString());
                return false;
            }
            ClaimedBones.Add(Bone);
        }
    }
    return true;
}

bool UDemoEnemyHitProfile::ValidateSkeleton(const USkeletalMesh* Mesh) const
{
    DEMO_LOG_CALL();
    if (!Mesh || !Validate())
    {
        UE_LOG(LogFPSDemo, Error, TEXT("HIT_ZONE_SKELETON_REJECT mesh=%s invalid profile or mesh"), *GetNameSafe(Mesh));
        return false;
    }
    const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton(); // 只借用资产参考骨架，本函数不改骨骼或PhysicsAsset。
    for (int32 Index = 0; Index < Reference.GetNum(); ++Index) // 全骨检查使新增/改名骨在初始化时明确报错。
    {
        const FName Bone = Reference.GetBoneName(Index); // 当前参考骨名，含无蒙皮的变换骨。
        bool bKnown = NonHittableBones.Contains(Bone); // 显式禁用也算已审计，不能默认忽略未知骨。
        for (const FDemoEnemyHitZoneRule& Rule : Regions) bKnown |= Rule.BoneNames.Contains(Bone); // 先Validate保证不会一骨多区。
        if (!bKnown)
        {
            UE_LOG(LogFPSDemo, Error, TEXT("HIT_ZONE_SKELETON_REJECT mesh=%s unmappedBone=%s"), *GetNameSafe(Mesh), *Bone.ToString());
            return false;
        }
    }
    UE_LOG(LogFPSDemo, Log, TEXT("HIT_ZONE_SKELETON_VALID mesh=%s bones=%d"), *GetNameSafe(Mesh), Reference.GetNum());
    return true;
}

bool UDemoEnemyHitProfile::ResolveBone(FName Bone, EDemoEnemyHitRegion& OutRegion, float& OutMultiplier) const
{
    DEMO_LOG_CALL();
    OutRegion = EDemoEnemyHitRegion::Body;
    OutMultiplier = 0.f; // 失败输出强制零；没有所谓未知骨的默认机身倍率。
    if (Bone.IsNone() || Bone == TEXT("root") || Bone == TEXT("aim_yaw") || NonHittableBones.Contains(Bone))
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("HIT_ZONE_BONE_REJECT bone=%s missing or explicitly disabled"), *Bone.ToString());
        return false;
    }
    for (const FDemoEnemyHitZoneRule& Rule : Regions) // 精确白名单匹配，不搜索父骨，不把未知骨默认当作机身。
    {
        if (Rule.BoneNames.Contains(Bone))
        {
            if (!FMath::IsFinite(Rule.DamageMultiplier) || Rule.DamageMultiplier < 0.f || Rule.DamageMultiplier > 100.f)
            {
                UE_LOG(LogFPSDemo, Error, TEXT("HIT_ZONE_BONE_REJECT bone=%s invalid multiplier=%f"), *Bone.ToString(), Rule.DamageMultiplier);
                return false;
            }
            OutRegion = Rule.Region;
            OutMultiplier = Rule.DamageMultiplier;
            return true;
        }
    }
    UE_LOG(LogFPSDemo, Error, TEXT("HIT_ZONE_BONE_REJECT bone=%s unmapped bone"), *Bone.ToString());
    return false;
}

bool UDemoEnemyHitProfile::ResolveHit(const FHitResult& Hit, EDemoEnemyHitRegion& OutRegion, float& OutMultiplier) const
{
    DEMO_LOG_CALL();
    OutRegion = EDemoEnemyHitRegion::Body;
    OutMultiplier = 0.f; // 所有早退也保留零输出，不因未命中而给敌人整身伤害。
    const USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(Hit.GetComponent()); // 必须是真骨骼查询，根球和其他组件不会获得部位伤害。
    const UPhysicsAsset* Physics = Mesh ? Mesh->GetPhysicsAsset() : nullptr; // 只借用当前组件实际使用的刚体资产，支持显式override。
    if (!Hit.bBlockingHit || !Cast<ADemoEnemy>(Hit.GetActor()) || !Mesh || Mesh->GetOwner() != Hit.GetActor()
        || Hit.BoneName.IsNone() || Mesh->GetBoneIndex(Hit.BoneName) == INDEX_NONE
        || !Physics || Physics->FindBodyIndex(Hit.BoneName) == INDEX_NONE
        || Mesh->GetCollisionResponseToChannel(DemoEnemyHitZones::TraceChannel) != ECR_Block)
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("HIT_ZONE_REJECT actor=%s component=%s bone=%s missing/disabled skeletal enemy query body"),
            *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), *Hit.BoneName.ToString());
        return false;
    }
    return ResolveBone(Hit.BoneName, OutRegion, OutMultiplier);
}

const UDemoEnemyHitProfile* DemoEnemyHitZones::SelectValidProfile(const UDemoEnemyHitProfile* Configured)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs asset=%s"), __FUNCTION__, *GetNameSafe(Configured));
    if (Configured && Configured->Validate()) return Configured;
    // 初始化碰撞与逐枪结算必须选择同一回退，避免射击宣称回退而网格已被禁用。
    const UDemoEnemyHitProfile* Fallback = GetDefault<UDemoEnemyHitProfile>(); // 原生CDO由引擎保活，仅借用且不修改。
    UE_LOG(LogFPSDemo, Warning, TEXT("HIT_ZONE_CONFIG_FALLBACK asset=%s using complete native bone whitelist"), *GetNameSafe(Configured));
    return Fallback->Validate() ? Fallback : nullptr;
}

const TCHAR* DemoEnemyHitZones::RegionName(EDemoEnemyHitRegion Region)
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs region=%d"), __FUNCTION__, static_cast<int32>(Region));
    switch (Region)
    {
        case EDemoEnemyHitRegion::Body: return TEXT("Body");
        case EDemoEnemyHitRegion::Arm: return TEXT("Arm");
        case EDemoEnemyHitRegion::Core: return TEXT("Core");
        default: return TEXT("Invalid");
    }
}
