// 职责：在本机PostUpdate读取真实最终骨姿，批量提交正常骨长的身体/武器阴影；原TP仍用于队友、物理与HWRT。
// 使用：由DemoCharacter默认子对象驱动；Presentation统一原caster门，失败幂等回退，绝不修改源动画或强制重求值。
#include "Animation/DemoOwnerShadowComponent.h"
#include "Animation/DemoPlayerPresentationComponent.h"
#include "Animation/DemoWeaponAnimationComponent.h"
#include "Characters/DemoCharacter.h"
#include "Components/MeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/DemoLog.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TwoBoneIK.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/Melee/DemoMeleeComponent.h"

FBoxSphereBounds UDemoOwnerShadowMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
    DEMO_LOG_TICK();
    const FBoxSphereBounds ReferenceWorldBounds=Super::CalcBounds(LocalToWorld); // 保护原生网格参考形体，骨点扩展不能把静态头/躯干几何缩小。
    FBox ComponentBonePoints(ForceInit); // 本组件CS厘米；只累计有限已求值点，不求源Pose或读取CPU蒙皮顶点。
    const TArray<FTransform>& ComponentPose=GetComponentSpaceTransforms();
    for (const FTransform& BoneTransform : ComponentPose)
    {
        const FVector Point=BoneTransform.GetTranslation();
        if (!Point.ContainsNaN()) ComponentBonePoints+=Point;
    }
    if (!ComponentBonePoints.IsValid || LocalToWorld.ContainsNaN()) return ReferenceWorldBounds;
    constexpr double SkinMarginCm=20.; // 保守覆盖骨间/手指/衣物附近蒙皮；不是对所有资产证明的精确形变上限。
    const FBoxSphereBounds AnimatedWorldBounds=FBoxSphereBounds(ComponentBonePoints.ExpandBy(SkinMarginCm)).TransformBy(LocalToWorld);
    return ReferenceWorldBounds+AnimatedWorldBounds;
}

UDemoOwnerShadowComponent::UDemoOwnerShadowComponent()
{
    DEMO_LOG_CALL();
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    SetIsReplicatedByDefault(false);
}

void UDemoOwnerShadowComponent::BeginPlay()
{
    DEMO_LOG_CALL();
    Super::BeginPlay();
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner()); // 本组件只消费自己Avatar，不寻找World其他Pawn。
    if (!Pawn || GetNetMode()==NM_DedicatedServer)
    {
        SetComponentTickEnabled(false);
        UE_LOG(LogFPSDemo,Log,TEXT("OWNER_SHADOW_DISABLED owner=%s reason=NoLocalRendering"),*GetNameSafe(GetOwner()));
        return;
    }
    Presentation=Pawn->FindComponentByClass<UDemoPlayerPresentationComponent>();
    if (Presentation.IsValid()) AddTickPrerequisiteComponent(Presentation.Get());
    if (UDemoMeleeComponent* Melee=Pawn->FindComponentByClass<UDemoMeleeComponent>()) AddTickPrerequisiteComponent(Melee);
    if (UDemoWeaponAnimationComponent* Animation=Pawn->FindComponentByClass<UDemoWeaponAnimationComponent>()) AddTickPrerequisiteComponent(Animation);
    RefreshSourcePrerequisites(Pawn->GetMesh(),Pawn->GetMesh1P(),Presentation.IsValid()?Presentation->GetFirstPersonBodyMesh():nullptr);
}

void UDemoOwnerShadowComponent::RefreshSourcePrerequisites(USkeletalMeshComponent* Body, USkeletalMeshComponent* Arms, USkeletalMeshComponent* Lower)
{
    DEMO_LOG_TICK();
    // 三个槽可能指向同一源；先去旧再加全体去重源，不因单槽替换误撤销其他槽仍需的依赖。
    if (PrerequisiteBody.Get()==Body && PrerequisiteArms.Get()==Arms && PrerequisiteLower.Get()==Lower) return;
    if (PrerequisiteBody.IsValid()) RemoveTickPrerequisiteComponent(PrerequisiteBody.Get());
    if (PrerequisiteArms.IsValid()) RemoveTickPrerequisiteComponent(PrerequisiteArms.Get());
    if (PrerequisiteLower.IsValid()) RemoveTickPrerequisiteComponent(PrerequisiteLower.Get());
    PrerequisiteBody=Body; PrerequisiteArms=Arms; PrerequisiteLower=Lower;
    if (Body) AddTickPrerequisiteComponent(Body);
    if (Arms && Arms!=Body) AddTickPrerequisiteComponent(Arms);
    if (Lower && Lower!=Body && Lower!=Arms) AddTickPrerequisiteComponent(Lower);
}

bool UDemoOwnerShadowComponent::RefreshBoneMaps(USkeletalMeshComponent* Body, USkeletalMeshComponent* Arms, USkeletalMeshComponent* Lower)
{
    DEMO_LOG_TICK();
    USkeletalMesh* BodyMesh=Body?Body->GetSkeletalMeshAsset():nullptr; // 缓存仅静态参考数据；最终动画姿态在下方逐帧读。
    USkeletalMesh* ArmsMesh=Arms?Arms->GetSkeletalMeshAsset():nullptr;
    USkeletalMesh* LowerMesh=Lower?Lower->GetSkeletalMeshAsset():nullptr;
    const int32 ArmsLOD=Arms?Arms->GetPredictedLODLevel():INDEX_NONE; // 换LOD重新检查兼容性，不能假定骨数组大小恒等于可见LOD。
    if (CachedBodyMesh.Get()==BodyMesh && CachedArmsMesh.Get()==ArmsMesh && CachedLowerMesh.Get()==LowerMesh && CachedArmsLOD==ArmsLOD) return bMapsValid;
    UE_LOG(LogFPSDemo,Log,TEXT("OWNER_SHADOW_MAP owner=%s body=%s fp=%s lower=%s lod=%d"),*GetNameSafe(GetOwner()),*GetNameSafe(BodyMesh),*GetNameSafe(ArmsMesh),*GetNameSafe(LowerMesh),ArmsLOD);
    CachedBodyMesh=BodyMesh; CachedArmsMesh=ArmsMesh; CachedLowerMesh=LowerMesh; CachedArmsLOD=ArmsLOD;
    bMapsValid=false; ArmBoneMap.Reset(); LowerBoneMap.Reset(); ArmKinds.Reset();
    SpineBoneIndex=LeftUpperArmIndex=LeftLowerArmIndex=LeftHandIndex=RightHandIndex=INDEX_NONE;
    if (!BodyMesh || !ArmsMesh || !LowerMesh) return false;
    const FReferenceSkeleton& BodyRef=BodyMesh->GetRefSkeleton();
    const FReferenceSkeleton& ArmsRef=ArmsMesh->GetRefSkeleton();
    const FReferenceSkeleton& LowerRef=LowerMesh->GetRefSkeleton();
    const int32 Spine=BodyRef.FindBoneIndex(TEXT("spine_01"));
    const int32 Left=BodyRef.FindBoneIndex(TEXT("upperarm_l"));
    const int32 LeftLower=BodyRef.FindBoneIndex(TEXT("lowerarm_l"));
    const int32 LeftHand=BodyRef.FindBoneIndex(TEXT("hand_l"));
    const int32 Right=BodyRef.FindBoneIndex(TEXT("upperarm_r"));
    const int32 RightHand=BodyRef.FindBoneIndex(TEXT("hand_r"));
    if (Spine==INDEX_NONE || Left==INDEX_NONE || LeftLower==INDEX_NONE || LeftHand==INDEX_NONE || Right==INDEX_NONE || RightHand==INDEX_NONE) return false;
    SpineBoneIndex=Spine; LeftUpperArmIndex=Left; LeftLowerArmIndex=LeftLower; LeftHandIndex=LeftHand; RightHandIndex=RightHand;
    ArmBoneMap.Init(INDEX_NONE,BodyRef.GetNum()); LowerBoneMap.Init(INDEX_NONE,BodyRef.GetNum()); ArmKinds.Init(0,BodyRef.GetNum());
    for (int32 Bone=0;Bone<BodyRef.GetNum();++Bone)
    {
        const FName Name=BodyRef.GetBoneName(Bone); // 参考父名比较适用于不同Skeleton对象但同一Manny参考坐标，不靠指针推断重定向。
        const int32 Parent=BodyRef.GetParentIndex(Bone);
        const FName ParentName=Parent==INDEX_NONE?NAME_None:BodyRef.GetBoneName(Parent);
        const bool bLeftArmRoot=Bone==Left;
        const bool bRightArmRoot=Bone==Right;
        const bool bLeftArm=bLeftArmRoot || BodyRef.BoneIsChildOf(Bone,Left);
        const bool bRightArm=bRightArmRoot || BodyRef.BoneIsChildOf(Bone,Right);
        const bool bArm=bLeftArm || bRightArm;
        const bool bLower=Bone!=Spine && !BodyRef.BoneIsChildOf(Bone,Spine); // 根/骨盆/腿/脚IK从实际本人下身；自然TP躯干保持自己的局部骨长与姿态。
        const FTransform& BodyRest=BodyRef.GetRefBonePose()[Bone];
        if (bArm)
        {
            const int32 Source=ArmsRef.FindBoneIndex(Name);
            const int32 SourceParent=Source!=INDEX_NONE?ArmsRef.GetParentIndex(Source):INDEX_NONE;
            const FName SourceParentName=SourceParent==INDEX_NONE?NAME_None:ArmsRef.GetBoneName(SourceParent);
            if (Source==INDEX_NONE || SourceParentName!=ParentName || !BodyRest.Equals(ArmsRef.GetRefBonePose()[Source],0.02f))
            {
                UE_LOG(LogFPSDemo,Warning,TEXT("OWNER_SHADOW_MAP_REJECT owner=%s bone=%s reason=ArmReferenceMismatch; original_TP_shadow_retained"),*GetNameSafe(GetOwner()),*Name.ToString());
                return false;
            }
            ArmBoneMap[Bone]=Source;
            ArmKinds[Bone]=bLeftArmRoot?1:(bLeftArm?2:(bRightArmRoot?3:4));
        }
        if (bLower)
        {
            const int32 Source=LowerRef.FindBoneIndex(Name);
            const int32 SourceParent=Source!=INDEX_NONE?LowerRef.GetParentIndex(Source):INDEX_NONE;
            const FName SourceParentName=SourceParent==INDEX_NONE?NAME_None:LowerRef.GetBoneName(SourceParent);
            if (Source==INDEX_NONE || SourceParentName!=ParentName || !BodyRest.Equals(LowerRef.GetRefBonePose()[Source],0.02f))
            {
                UE_LOG(LogFPSDemo,Warning,TEXT("OWNER_SHADOW_MAP_REJECT owner=%s bone=%s reason=LowerReferenceMismatch; original_TP_shadow_retained"),*GetNameSafe(GetOwner()),*Name.ToString());
                return false;
            }
            LowerBoneMap[Bone]=Source;
        }
    }
    bMapsValid=true;
    return true;
}

bool UDemoOwnerShadowComponent::ReadLocalPose(USkeletalMeshComponent* Source, TArray<FTransform>& OutLocal) const
{
    DEMO_LOG_TICK();
    if (!Source || !Source->GetSkeletalMeshAsset() || Source->IsRunningParallelEvaluation()) return false;
    const FReferenceSkeleton& Ref=Source->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform>& ComponentPose=Source->GetComponentSpaceTransforms(); // 此处不调用会阻塞/重求值的BoneSpace getter，依赖已完成的Tick缓存。
    if (ComponentPose.Num()!=Ref.GetNum() || ComponentPose.IsEmpty()) return false;
    OutLocal.SetNumUninitialized(ComponentPose.Num());
    for (int32 Bone=0;Bone<ComponentPose.Num();++Bone)
    {
        const int32 Parent=Ref.GetParentIndex(Bone);
        if (ComponentPose[Bone].ContainsNaN()) return false;
        OutLocal[Bone]=Parent==INDEX_NONE?ComponentPose[Bone]:ComponentPose[Bone].GetRelativeTransform(ComponentPose[Parent]);
        OutLocal[Bone].NormalizeRotation();
        if (OutLocal[Bone].ContainsNaN()) return false;
    }
    return true;
}

bool UDemoOwnerShadowComponent::BuildBodyPose(USkeletalMeshComponent* Body, USkeletalMeshComponent* Arms, USkeletalMeshComponent* Lower, bool bMelee)
{
    DEMO_LOG_TICK();
    if (!bMapsValid || !BodyProxy || !ReadLocalPose(Body,BodyLocalPose) || !ReadLocalPose(Arms,ArmsLocalPose)) return false;
    // Leader分区的渲染直接用Body骨缓冲，自己的数组可能为空；此时实际可见下身恰好就是TP，不能假报姿态未就绪。
    const bool bLowerUsesBodyLeader=Lower->LeaderPoseComponent.Get()==Body;
    if (!bLowerUsesBodyLeader && !ReadLocalPose(Lower,LowerLocalPose)) return false;
    const FReferenceSkeleton& Ref=Body->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform>& BodyComponentPose=Body->GetComponentSpaceTransforms();
    const TArray<FTransform>& FpComponentPose=Arms->GetComponentSpaceTransforms();
    if (ArmKinds.Num()!=Ref.GetNum() || ArmBoneMap.Num()!=Ref.GetNum() || LowerBoneMap.Num()!=Ref.GetNum() || BodyComponentPose.Num()!=Ref.GetNum()) return false;
    MergedLocalPose=BodyLocalPose; MergedComponentPose.SetNumUninitialized(Ref.GetNum());
    const FQuat ProxyWorldRotation=Body->GetComponentQuat(); // 正常物理网格世界框，而非压缩FP组件世界框。
    const FQuat FpWorldRotation=Arms->GetComponentQuat();
    // 手枪等单手枪械待机/射击在FP中隐藏upperarm_l；此时代理左臂保留TP自然垂臂/跑动摆臂，不投射被隐藏的悬空FP左臂。
    const bool bSkipLeftFpArm=!bMelee && Arms->IsBoneHiddenByName(TEXT("upperarm_l"));
    for (int32 Bone=0;Bone<Ref.GetNum();++Bone)
    {
        const int32 Parent=Ref.GetParentIndex(Bone);
        if (!bLowerUsesBodyLeader && LowerBoneMap[Bone]!=INDEX_NONE)
        {
            if (!LowerLocalPose.IsValidIndex(LowerBoneMap[Bone])) return false;
            MergedLocalPose[Bone]=LowerLocalPose[LowerBoneMap[Bone]];
        }
        else if (!bLowerUsesBodyLeader && Bone==SpineBoneIndex)
        {
            // 下身取自独立OwnerLowerBody时，pelvis无TP旋转；对齐spine_01组件空间朝向以消除跨源嫁接带来的胸肩整体偏转。
            const FQuat ParentRotation=Parent==INDEX_NONE?FQuat::Identity:MergedComponentPose[Parent].GetRotation();
            MergedLocalPose[Bone].SetRotation((ParentRotation.Inverse()*BodyComponentPose[Bone].GetRotation()).GetNormalized());
        }
        const uint8 ArmKind=ArmKinds[Bone];
        const bool bUseFpArm=ArmKind!=0 && !(bSkipLeftFpArm && (ArmKind==1 || ArmKind==2));
        if (bUseFpArm && (ArmKind==1 || ArmKind==3))
        {
            const int32 Source=ArmBoneMap[Bone];
            if (!FpComponentPose.IsValidIndex(Source)) return false;
            const FQuat ParentRotation=Parent==INDEX_NONE?FQuat::Identity:MergedComponentPose[Parent].GetRotation();
            const FQuat DesiredComponent=ProxyWorldRotation.Inverse()*(FpWorldRotation*FpComponentPose[Source].GetRotation());
            MergedLocalPose[Bone].SetRotation((ParentRotation.Inverse()*DesiredComponent).GetNormalized());
        }
        else if (bUseFpArm && (ArmKind==2 || ArmKind==4))
        {
            if (!ArmsLocalPose.IsValidIndex(ArmBoneMap[Bone])) return false;
            MergedLocalPose[Bone].SetRotation(ArmsLocalPose[ArmBoneMap[Bone]].GetRotation()); // 只借Q；T/S保持正常基底，绝不把相机艺术缩放/肩位带到全身。
        }
        MergedComponentPose[Bone]=Parent==INDEX_NONE?MergedLocalPose[Bone]:MergedLocalPose[Bone]*MergedComponentPose[Parent];
        MergedComponentPose[Bone].NormalizeRotation();
        if (MergedComponentPose[Bone].ContainsNaN()) return false;
    }
    if (!bMelee && !bSkipLeftFpArm)
    {
        if (!MergedComponentPose.IsValidIndex(LeftUpperArmIndex) || !MergedComponentPose.IsValidIndex(LeftLowerArmIndex)
            || !MergedComponentPose.IsValidIndex(LeftHandIndex) || !MergedComponentPose.IsValidIndex(RightHandIndex)) return false;
        const int32 SourceHandL=ArmBoneMap[LeftHandIndex], SourceHandR=ArmBoneMap[RightHandIndex];
        if (!FpComponentPose.IsValidIndex(SourceHandL) || !FpComponentPose.IsValidIndex(SourceHandR)) return false;
        // 枪械挂于代理右手；按FP未缩放组件空间的左腕相对右腕刚体变换重建左腕目标，再用定长TwoBoneIK闭环，消除TP/FP肩位差引起的左手脱靶。
        const FTransform LeftRelativeToRight=FpComponentPose[SourceHandL].GetRelativeTransform(FpComponentPose[SourceHandR]);
        const FTransform TargetHandL=LeftRelativeToRight*MergedComponentPose[RightHandIndex];
        const FTransform Before[3]={MergedComponentPose[LeftUpperArmIndex],MergedComponentPose[LeftLowerArmIndex],MergedComponentPose[LeftHandIndex]};
        FTransform After[3]={Before[0],Before[1],Before[2]};
        const FVector ArmDir=(Before[2].GetLocation()-Before[0].GetLocation()).GetSafeNormal();
        const FVector ElbowOffset=Before[1].GetLocation()-Before[0].GetLocation();
        FVector BendDir=(ElbowOffset-ArmDir*FVector::DotProduct(ElbowOffset,ArmDir)).GetSafeNormal();
        if (BendDir.IsNearlyZero()) BendDir=ElbowOffset.GetSafeNormal();
        const FVector JointTarget=Before[1].GetLocation()+BendDir*30.f;
        AnimationCore::SolveTwoBoneIK(After[0],After[1],After[2],JointTarget,TargetHandL.GetLocation(),false,1.f,1.f);
        After[2].SetRotation(TargetHandL.GetRotation());
        const int32 UpperParent=Ref.GetParentIndex(LeftUpperArmIndex);
        const FQuat UpperParentRotation=UpperParent==INDEX_NONE?FQuat::Identity:MergedComponentPose[UpperParent].GetRotation();
        MergedLocalPose[LeftUpperArmIndex].SetRotation((UpperParentRotation.Inverse()*After[0].GetRotation()).GetNormalized());
        MergedLocalPose[LeftLowerArmIndex].SetRotation((After[0].GetRotation().Inverse()*After[1].GetRotation()).GetNormalized());
        MergedLocalPose[LeftHandIndex].SetRotation((After[1].GetRotation().Inverse()*After[2].GetRotation()).GetNormalized());
        for (int32 Bone=0;Bone<Ref.GetNum();++Bone)
        {
            if (ArmKinds[Bone]!=1 && ArmKinds[Bone]!=2) continue;
            const int32 Parent=Ref.GetParentIndex(Bone);
            MergedComponentPose[Bone]=Parent==INDEX_NONE?MergedLocalPose[Bone]:MergedLocalPose[Bone]*MergedComponentPose[Parent];
            MergedComponentPose[Bone].NormalizeRotation();
            if (MergedComponentPose[Bone].ContainsNaN()) return false;
        }
    }
    BodyProxy->SetWorldTransform(Body->GetComponentTransform());
    BodyProxy->BoneSpaceTransforms=MergedLocalPose;
    BodyProxy->MarkRefreshTransformDirty(); BodyProxy->RefreshBoneTransforms(); // 引擎统一Fill/Finalize/Bounds；每帧一次，不逐骨反复刷新。
    return true;
}

void UDemoOwnerShadowComponent::ConfigureShadowPrimitive(UPrimitiveComponent* Primitive)
{
    DEMO_LOG_CALL();
    if (!Primitive) return;
    Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision); Primitive->SetGenerateOverlapEvents(false);
    Primitive->SetCanEverAffectNavigation(false);
    Primitive->SetHiddenInGame(true); Primitive->SetVisibility(true);
    Primitive->SetOnlyOwnerSee(false); Primitive->SetOwnerNoSee(false); // HiddenShadow绕过OwnerSee，不能用它假装每View隔离。
    Primitive->SetRenderInMainPass(false); Primitive->SetRenderInDepthPass(false);
    Primitive->SetHiddenInSceneCapture(true); Primitive->SetVisibleInRayTracing(false);
    Primitive->SetAffectDistanceFieldLighting(false); Primitive->SetAffectDynamicIndirectLighting(false);
    Primitive->SetCastShadow(false); Primitive->SetCastHiddenShadow(false); // 首个完整Pose成功提交前不投参考姿态或原点影。
    Primitive->SetComponentTickEnabled(false);
}

void UDemoOwnerShadowComponent::MirrorMaterials(UMeshComponent* Source, UMeshComponent* Target)
{
    DEMO_LOG_TICK();
    if (!Source || !Target) return;
    for (int32 Slot=0;Slot<Source->GetNumMaterials();++Slot)
        if (Target->GetMaterial(Slot)!=Source->GetMaterial(Slot)) Target->SetMaterial(Slot,Source->GetMaterial(Slot));
}

bool UDemoOwnerShadowComponent::PrepareBodyMeshes(ADemoCharacter* Pawn, USkeletalMeshComponent* Body, USkeletalMeshComponent* Head)
{
    DEMO_LOG_TICK();
    if (!Pawn || !Body || !Body->GetSkeletalMeshAsset()) return false;
    if (!BodyProxy)
    {
        BodyProxy=NewObject<UDemoOwnerShadowMeshComponent>(Pawn,TEXT("OwnerShadowBody")); Pawn->AddInstanceComponent(BodyProxy);
        ConfigureShadowPrimitive(BodyProxy); BodyProxy->RegisterComponent();
    }
    if (BodyProxy->GetSkinnedAsset()!=Body->GetSkeletalMeshAsset())
    {
        BodyProxy->SetSkinnedAssetAndUpdate(Body->GetSkeletalMeshAsset()); BodyProxy->EmptyOverrideMaterials();
    }
    MirrorMaterials(Body,BodyProxy);
    if (Head && Head->GetSkeletalMeshAsset())
    {
        if (Head->GetSkeletalMeshAsset()->GetSkeleton()!=Body->GetSkeletalMeshAsset()->GetSkeleton()) return false;
        if (!HeadProxy)
        {
            HeadProxy=NewObject<UPoseableMeshComponent>(Pawn,TEXT("OwnerShadowHead")); Pawn->AddInstanceComponent(HeadProxy);
            ConfigureShadowPrimitive(HeadProxy); HeadProxy->RegisterComponent();
        }
        if (HeadProxy->GetSkinnedAsset()!=Head->GetSkeletalMeshAsset())
        {
            HeadProxy->SetLeaderPoseComponent(nullptr);
            HeadProxy->SetSkinnedAssetAndUpdate(Head->GetSkeletalMeshAsset()); HeadProxy->EmptyOverrideMaterials();
            HeadProxy->SetLeaderPoseComponent(BodyProxy,true); HeadProxy->bUseBoundsFromLeaderPoseComponent=true;
        }
        MirrorMaterials(Head,HeadProxy); HeadProxy->SetWorldTransform(Body->GetComponentTransform());
    }
    else if (HeadProxy)
    {
        HeadProxy->SetCastShadow(false); HeadProxy->SetCastHiddenShadow(false); HeadProxy->SetSkinnedAssetAndUpdate(nullptr);
    }
    return true;
}

bool UDemoOwnerShadowComponent::UpdateWeaponMeshes(ADemoCharacter* Pawn, USkeletalMeshComponent* Arms, const FTransform& SourceHand, bool bMelee)
{
    DEMO_LOG_TICK();
    USkeletalMeshComponent* SkeletalSource=nullptr; // 当前唯一持有的FP机械枪，不读取其它库存Actor。
    UStaticMeshComponent* StaticSource=nullptr;
    UStaticMeshComponent* OpticSource=nullptr;
    if (bMelee)
    {
        const UDemoMeleeComponent* Melee=Pawn->FindComponentByClass<UDemoMeleeComponent>();
        StaticSource=Melee?Melee->GetFirstPersonWeaponMesh():nullptr;
        const UStaticMeshComponent* ThirdPerson=Melee?Melee->GetThirdPersonWeaponMesh():nullptr;
        if (!StaticSource || !ThirdPerson || !StaticSource->GetStaticMesh() || StaticSource->GetStaticMesh()!=ThirdPerson->GetStaticMesh()) return false;
    }
    else
    {
        const UDemoWeaponComponent* Inventory=Pawn->GetWeaponComponent();
        ADemoWeaponBase* Weapon=Inventory?Inventory->GetActiveWeapon():nullptr;
        if (!Weapon || !Presentation.IsValid()) return false;
        StaticSource=Weapon->GetWeaponStaticMesh();
        SkeletalSource=Weapon->GetWeaponSkeletalMesh();
        OpticSource=Weapon->GetWeaponOpticMesh();
        if (StaticSource && StaticSource->GetStaticMesh())
        {
            const UStaticMeshComponent* ThirdPerson=Presentation->GetThirdPersonStaticWeaponMesh();
            if (!ThirdPerson || ThirdPerson->bHiddenInGame || ThirdPerson->GetStaticMesh()!=StaticSource->GetStaticMesh()) return false;
            SkeletalSource=nullptr;
        }
        else
        {
            StaticSource=nullptr;
            const USkeletalMeshComponent* ThirdPerson=Presentation->GetThirdPersonWeaponMesh();
            if (!SkeletalSource || !SkeletalSource->GetSkeletalMeshAsset() || !ThirdPerson || ThirdPerson->bHiddenInGame || ThirdPerson->GetSkeletalMeshAsset()!=SkeletalSource->GetSkeletalMeshAsset()) return false;
        }
    }
    if (!Arms || SourceHand.ContainsNaN() || SourceHand.GetScale3D().GetAbsMin()<KINDA_SMALL_NUMBER) return false;
    const FTransform ProxyHand=BodyProxy->GetSocketTransform(TEXT("hand_r"),RTS_World); // 正常骨长的同名实际右手，不借TP旧IK枪框。
    if (SkeletalSource)
    {
        TArray<FTransform> MechanicalLocal; // 枪仅数十骨，逐帧临时值不保活实例；身体大缓冲另缓存。
        if (!ReadLocalPose(SkeletalSource,MechanicalLocal)) return false;
        if (!SkeletalWeaponProxy)
        {
            SkeletalWeaponProxy=NewObject<UPoseableMeshComponent>(Pawn,TEXT("OwnerShadowWeapon")); Pawn->AddInstanceComponent(SkeletalWeaponProxy);
            ConfigureShadowPrimitive(SkeletalWeaponProxy); SkeletalWeaponProxy->RegisterComponent();
        }
        if (SkeletalWeaponProxy->GetSkinnedAsset()!=SkeletalSource->GetSkeletalMeshAsset())
        {
            SkeletalWeaponProxy->SetSkinnedAssetAndUpdate(SkeletalSource->GetSkeletalMeshAsset()); SkeletalWeaponProxy->EmptyOverrideMaterials();
        }
        const FTransform Relative=SkeletalSource->GetComponentTransform().GetRelativeTransform(SourceHand);
        const FTransform WeaponWorld=Relative*ProxyHand; // 先核完整重建框，再提交到渲染线程，坏配置只回退而不发布NaN/零比例。
        if (WeaponWorld.ContainsNaN() || WeaponWorld.GetScale3D().GetAbsMin()<KINDA_SMALL_NUMBER) return false;
        SkeletalWeaponProxy->SetWorldTransform(WeaponWorld); SkeletalWeaponProxy->BoneSpaceTransforms=MoveTemp(MechanicalLocal);
        SkeletalWeaponProxy->MarkRefreshTransformDirty(); SkeletalWeaponProxy->RefreshBoneTransforms(); MirrorMaterials(SkeletalSource,SkeletalWeaponProxy);
        bUsesSkeletalWeapon=true;
    }
    else if (StaticSource)
    {
        if (!StaticWeaponProxy)
        {
            StaticWeaponProxy=NewObject<UStaticMeshComponent>(Pawn,TEXT("OwnerShadowStaticWeapon")); Pawn->AddInstanceComponent(StaticWeaponProxy);
            ConfigureShadowPrimitive(StaticWeaponProxy); StaticWeaponProxy->RegisterComponent();
        }
        if (StaticWeaponProxy->GetStaticMesh()!=StaticSource->GetStaticMesh())
        {
            StaticWeaponProxy->SetStaticMesh(StaticSource->GetStaticMesh()); StaticWeaponProxy->EmptyOverrideMaterials();
        }
        const FTransform Relative=StaticSource->GetComponentTransform().GetRelativeTransform(SourceHand);
        const FTransform WeaponWorld=Relative*ProxyHand;
        if (WeaponWorld.ContainsNaN() || WeaponWorld.GetScale3D().GetAbsMin()<KINDA_SMALL_NUMBER) return false;
        StaticWeaponProxy->SetWorldTransform(WeaponWorld); MirrorMaterials(StaticSource,StaticWeaponProxy);
        bUsesSkeletalWeapon=false;
    }
    else return false;
    bUsesOptic=false;
    if (OpticSource && OpticSource->GetStaticMesh())
    {
        const UStaticMeshComponent* ThirdPerson=Presentation.IsValid()?Presentation->GetThirdPersonOpticMesh():nullptr;
        if (!ThirdPerson || ThirdPerson->GetStaticMesh()!=OpticSource->GetStaticMesh()) return false;
        if (!OpticProxy)
        {
            OpticProxy=NewObject<UStaticMeshComponent>(Pawn,TEXT("OwnerShadowOptic")); Pawn->AddInstanceComponent(OpticProxy);
            ConfigureShadowPrimitive(OpticProxy); OpticProxy->RegisterComponent();
        }
        if (OpticProxy->GetStaticMesh()!=OpticSource->GetStaticMesh())
        {
            OpticProxy->SetStaticMesh(OpticSource->GetStaticMesh()); OpticProxy->EmptyOverrideMaterials();
        }
        const FTransform OpticWorld=OpticSource->GetComponentTransform().GetRelativeTransform(SourceHand)*ProxyHand;
        if (OpticWorld.ContainsNaN() || OpticWorld.GetScale3D().GetAbsMin()<KINDA_SMALL_NUMBER) return false;
        OpticProxy->SetWorldTransform(OpticWorld);
        MirrorMaterials(OpticSource,OpticProxy); bUsesOptic=true;
    }
    return true;
}

void UDemoOwnerShadowComponent::SetProxyState(bool bNewActive, FName Reason)
{
    DEMO_LOG_TICK();
    if (bActive!=bNewActive || LastReason!=Reason)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("OWNER_SHADOW_STATE owner=%s active=%d reason=%s body=%s weapon=%s"),*GetNameSafe(GetOwner()),bNewActive,*Reason.ToString(),*GetNameSafe(BodyProxy),*GetNameSafe(bUsesSkeletalWeapon?static_cast<UPrimitiveComponent*>(SkeletalWeaponProxy.Get()):static_cast<UPrimitiveComponent*>(StaticWeaponProxy.Get())));
        bActive=bNewActive; LastReason=Reason;
    }
    // Weapon种类/Head可在active不变时切换；原生setter只在值改变时Dirty，无两个Tick写相反值。
    if (BodyProxy) { BodyProxy->SetCastShadow(bNewActive); BodyProxy->SetCastHiddenShadow(bNewActive); }
    const bool bHead=bNewActive && HeadProxy && HeadProxy->GetSkinnedAsset();
    if (HeadProxy) { HeadProxy->SetCastShadow(bHead); HeadProxy->SetCastHiddenShadow(bHead); }
    if (SkeletalWeaponProxy) { SkeletalWeaponProxy->SetCastShadow(bNewActive && bUsesSkeletalWeapon); SkeletalWeaponProxy->SetCastHiddenShadow(bNewActive && bUsesSkeletalWeapon); }
    if (StaticWeaponProxy) { StaticWeaponProxy->SetCastShadow(bNewActive && !bUsesSkeletalWeapon); StaticWeaponProxy->SetCastHiddenShadow(bNewActive && !bUsesSkeletalWeapon); }
    if (OpticProxy) { OpticProxy->SetCastShadow(bNewActive && bUsesOptic); OpticProxy->SetCastHiddenShadow(bNewActive && bUsesOptic); }
    if (Presentation.IsValid()) Presentation->SetOwnerShadowProxyActive(bNewActive); // Root中央门立即切Body/Head/TP枪；Melee唯一写入口读取同一门。
}

void UDemoOwnerShadowComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    DEMO_LOG_TICK(); Super::TickComponent(DeltaTime,TickType,TickFunction);
    ADemoCharacter* Pawn=Cast<ADemoCharacter>(GetOwner());
    APlayerController* Controller=Pawn?Cast<APlayerController>(Pawn->GetController()):nullptr;
    if (!bEnabled || !Pawn || !Controller || !Controller->IsLocalController() || !Pawn->IsLocallyControlled() || Pawn->IsCoopEliminated()) { SetProxyState(false,TEXT("InactiveOwner")); return; }
    const UGameInstance* Instance=GetWorld()?GetWorld()->GetGameInstance():nullptr;
    if (!Instance || Instance->GetLocalPlayers().Num()!=1 || !Controller->GetLocalPlayer() || Controller->GetViewTarget()!=Pawn) { SetProxyState(false,TEXT("UnsupportedView")); return; }
    if (!Presentation.IsValid())
    {
        Presentation=Pawn->FindComponentByClass<UDemoPlayerPresentationComponent>();
        if (Presentation.IsValid()) AddTickPrerequisiteComponent(Presentation.Get());
    }
    if (!Presentation.IsValid() || !Presentation->UsesFullBodyFirstPerson() || !Presentation->UsesIndependentFirstPersonWeapons()) { SetProxyState(false,TEXT("PresentationPolicy")); return; }
    UDemoMeleeComponent* Melee=Pawn->FindComponentByClass<UDemoMeleeComponent>();
    const bool bMelee=Melee && Melee->IsEquipped();
    USkeletalMeshComponent* Body=Pawn->GetMesh();
    USkeletalMeshComponent* Arms=bMelee?Melee->GetFirstPersonArmsMesh():Pawn->GetMesh1P();
    USkeletalMeshComponent* Lower=Presentation->GetFirstPersonBodyMesh();
    RefreshSourcePrerequisites(Body,Arms,Lower);
    if (!Body || !Arms || !Lower || Body->bHiddenInGame || !Body->IsVisible() || Body->GetComponentTransform().ContainsNaN() || Arms->GetComponentTransform().ContainsNaN()) { SetProxyState(false,TEXT("SourceNotReady")); return; }
    // 首批只接受实际LOD0源：全长CS数组不等于高LOD叶骨仍在求值，未知裁骨不能拿旧手指姿态冒充最终Pose。
    if (Body->GetPredictedLODLevel()!=0 || Arms->GetPredictedLODLevel()!=0 || Lower->GetPredictedLODLevel()!=0) { SetProxyState(false,TEXT("UnsupportedSourceLOD")); return; }
    if (!RefreshBoneMaps(Body,Arms,Lower)) { SetProxyState(false,TEXT("IncompatibleReference")); return; }
    if (!PrepareBodyMeshes(Pawn,Body,Presentation->GetOwnerShadowHeadMesh()) || !BuildBodyPose(Body,Arms,Lower,bMelee)) { SetProxyState(false,TEXT("PoseNotReady")); return; }
    const int32 RightHand=Arms->GetBoneIndex(TEXT("hand_r"));
    if (RightHand==INDEX_NONE || !UpdateWeaponMeshes(Pawn,Arms,Arms->GetSocketTransform(TEXT("hand_r"),RTS_World),bMelee)) { SetProxyState(false,TEXT("WeaponNotReady")); return; }
    SetProxyState(true,TEXT("FinalPose"));
}

void UDemoOwnerShadowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    SetProxyState(false,TEXT("EndPlay"));
    if (OpticProxy) OpticProxy->DestroyComponent();
    if (StaticWeaponProxy) StaticWeaponProxy->DestroyComponent();
    if (SkeletalWeaponProxy) SkeletalWeaponProxy->DestroyComponent();
    if (HeadProxy) HeadProxy->DestroyComponent();
    if (BodyProxy) BodyProxy->DestroyComponent();
    OpticProxy=nullptr; StaticWeaponProxy=nullptr; SkeletalWeaponProxy=nullptr; HeadProxy=nullptr; BodyProxy=nullptr;
    Presentation.Reset();
    Super::EndPlay(EndPlayReason);
}

bool UDemoOwnerShadowComponent::IsProxyActive() const { DEMO_LOG_TICK(); return bActive; }
UPoseableMeshComponent* UDemoOwnerShadowComponent::GetBodyProxy() const { DEMO_LOG_TICK(); return BodyProxy; }
UPrimitiveComponent* UDemoOwnerShadowComponent::GetWeaponProxy() const
{
    DEMO_LOG_TICK();
    return bUsesSkeletalWeapon?static_cast<UPrimitiveComponent*>(SkeletalWeaponProxy.Get()):static_cast<UPrimitiveComponent*>(StaticWeaponProxy.Get());
}
