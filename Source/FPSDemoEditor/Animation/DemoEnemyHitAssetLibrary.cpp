#include "Animation/DemoEnemyHitAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Chaos/Convex.h"
#include "Debug/DemoLog.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    /** 已校验的单个凸体；仅当前生成调用拥有，顶点始终为模型参考空间厘米。 */
    struct FDemoHitSourceShape
    {
        FName Bone; // UE 稳定骨名，禁止使用不存在的控制骨或显示标签。
        FString Part; // 可编辑源零件名，写入形状名便于 Physics Asset 编辑器定位来源。
        TArray<FVector> Vertices; // 至少四个有限点，转换骨空间后交给 Chaos 烹制凸包。
    };

    /** Mesh/Index 为借用网格及已验证骨索引；父链按 Local*Parent 得到参考组件变换，无动画采样。 */
    FTransform GetReferenceComponentTransform(const USkeletalMesh* Mesh, int32 Index)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs bone=%d"), __FUNCTION__, Index);
        FTransform Result = FTransform::Identity; // 当前父链累计，无缓存避免重导入后旧轴污染。
        const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton(); // 网格拥有的真实参考姿势。
        for (int32 Parent = Index; Parent != INDEX_NONE; Parent = Reference.GetParentIndex(Parent)) // Parent 向根回溯。
        {
            Result = Result * Reference.GetRefBonePose()[Parent];
        }
        return Result;
    }

    /** Convex 为借用的已烹制零件，BoneName/ShapeIndex 用于错误定位；验证真实 Chaos 几何并执行一条穿心局部射线。 */
    bool ValidateCookedConvex(const FKConvexElem& Convex, FName BoneName, int32 ShapeIndex)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs bone=%s shape=%d"), __FUNCTION__, *BoneName.ToString(), ShapeIndex);
        // bCreatedPhysicsMeshes 在失败时也可能为 true；必须逐形状检查 Chaos 对象，不能只信源点或完成标志。
        const auto& Cooked = Convex.GetChaosConvexMesh(); // BodySetup 拥有的不可变烹制凸包，只在当前同步检查中借用。
        if (Convex.VertexData.Num() < 4 || !Convex.ElemBox.IsValid || Cooked == nullptr || !Cooked->IsValidGeometry()
            || Cooked->NumVertices() < 4 || Cooked->NumPlanes() < 4 || !FMath::IsFinite(Cooked->GetVolume()) || Cooked->GetVolume() <= 1.e-6)
        {
            UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_INVALID_COOKED_CONVEX bone=%s shape=%d part=%s"), *BoneName.ToString(), ShapeIndex, *Convex.GetName().ToString());
            return false;
        }
        // 调试绘制必须使用真实凸包三角面；索引缺失时不能退回 AABB，否则环孔与护板开口会产生误导。
        if (Convex.IndexData.Num() < 12 || Convex.IndexData.Num() % 3 != 0)
        {
            UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_INVALID_CONVEX_TRIANGLES bone=%s shape=%d indices=%d"), *BoneName.ToString(), ShapeIndex, Convex.IndexData.Num());
            return false;
        }
        for (const int32 VertexIndex : Convex.IndexData) // 每个三角点引用原始凸体点，不能拿 cooked 点序替代源点序。
        {
            if (!Convex.VertexData.IsValidIndex(VertexIndex))
            {
                UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_INVALID_CONVEX_INDEX bone=%s shape=%d index=%d vertices=%d"), *BoneName.ToString(), ShapeIndex, VertexIndex, Convex.VertexData.Num());
                return false;
            }
        }
        Chaos::FVec3 Centroid(0., 0., 0.); // 凸包顶点均值严格落在非退化实体内，不能用包围盒中心猜测内部点。
        for (int32 VertexIndex = 0; VertexIndex < Cooked->NumVertices(); ++VertexIndex) // 遍历烹制后的真实顶点，源点可能已被简化。
        {
            Centroid += Chaos::FVec3(Cooked->GetVertex(VertexIndex));
        }
        Centroid /= Cooked->NumVertices();
        const Chaos::FReal Reach = Convex.ElemBox.GetSize().Size() + 10.; // 厘米；包围盒对角线再加余量，保证起点在凸体外。
        const Chaos::FVec3 Start = Centroid + Chaos::FVec3(Reach, 0., 0.); // 所有输入位于凸体局部空间，不混入骨或世界变换。
        const Chaos::FVec3 Direction(-1., 0., 0.); // 单位射线方向，终点越过实体中心到另一侧。
        Chaos::FReal HitTime = -1.; // 输出沿射线的厘米距离，必须处于本次有限线段内。
        Chaos::FVec3 HitPosition(0., 0., 0.); // Chaos 输出局部交点；只验证有限数值，不进入游戏伤害结算。
        Chaos::FVec3 HitNormal(0., 0., 0.); // Chaos 输出碰撞法线；用于排除无效查询结果。
        int32 FaceIndex = INDEX_NONE; // 查询输出面索引；GJK 路径允许 INDEX_NONE，不把它当失败。
        if (Centroid.ContainsNaN() || !FMath::IsFinite(Reach)
            || !Cooked->Raycast(Start, Direction, 2. * Reach, 0., HitTime, HitPosition, HitNormal, FaceIndex)
            || !FMath::IsFinite(HitTime) || HitTime <= 0. || HitTime >= 2. * Reach || HitPosition.ContainsNaN() || HitNormal.ContainsNaN())
        {
            UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_INVALID_COOKED_RAYCAST bone=%s shape=%d part=%s distance=%.6f"), *BoneName.ToString(), ShapeIndex, *Convex.GetName().ToString(), HitTime);
            return false;
        }
        return true;
    }
}

UPhysicsAsset* UDemoEnemyHitAssetLibrary::BuildEnemyHitAsset(USkeletalMesh* Mesh, const FString& ManifestFilename, const FString& ModelName, const FString& PackagePath)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs mesh=%s model=%s package=%s"), __FUNCTION__, *GetNameSafe(Mesh), *ModelName, *PackagePath);
    FString JsonText; // 同步读取的源文件内容，仅本次离线生成持有。
    TSharedPtr<FJsonObject> Root; // 解析树生命周期局限于本函数，无对象跨线程捕获。
    if (!Mesh || !PackagePath.StartsWith(TEXT("/Game/Enemies/Breach/Physics/")) || !FPackageName::IsValidLongPackageName(PackagePath)
        || !FFileHelper::LoadFileToString(JsonText, *ManifestFilename)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
    {
        UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_REJECTED invalid mesh/path/JSON"));
        return nullptr;
    }
    const TArray<TSharedPtr<FJsonValue>>* Models = nullptr; // JSON 拥有的型号数组，只借用。
    if (!Root->TryGetArrayField(TEXT("models"), Models) || Root->GetStringField(TEXT("units")) != TEXT("centimetres")
        || Root->GetStringField(TEXT("space")) != TEXT("UE_mesh_reference"))
    {
        UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_REJECTED units/space/models mismatch"));
        return nullptr;
    }
    TArray<FDemoHitSourceShape> Shapes; // 完整校验后才改资产，源缺失不会清空已有碰撞。
    for (const TSharedPtr<FJsonValue>& ModelValue : *Models) // 当前型号节点来自只读清单。
    {
        const TSharedPtr<FJsonObject> Model = ModelValue->AsObject(); // 借用当前型号字段。
        if (!Model.IsValid() || Model->GetStringField(TEXT("name")) != ModelName) continue;
        const TArray<TSharedPtr<FJsonValue>>* SourceShapes = nullptr; // 当前型号所有物理零件凸体。
        if (!Model->TryGetArrayField(TEXT("shapes"), SourceShapes)) break;
        for (const TSharedPtr<FJsonValue>& ShapeValue : *SourceShapes) // 每项对应单骨单凸体。
        {
            const TSharedPtr<FJsonObject> ShapeObject = ShapeValue->AsObject(); // 当前凸体 JSON 节点。
            FDemoHitSourceShape Shape; // 先完整构造临时形状，验证失败不修改目标。
            const TArray<TSharedPtr<FJsonValue>>* Points = nullptr; // 模型空间原始点列。
            if (!ShapeObject.IsValid() || !ShapeObject->TryGetArrayField(TEXT("vertices_cm"), Points))
            {
                UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_REJECTED missing shape object/vertices"));
                return nullptr;
            }
            Shape.Bone = FName(*ShapeObject->GetStringField(TEXT("bone")));
            Shape.Part = ShapeObject->GetStringField(TEXT("part"));
            if (Mesh->GetRefSkeleton().FindBoneIndex(Shape.Bone) == INDEX_NONE || Points->Num() < 4 || Points->Num() > 512)
            {
                UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_REJECTED bone=%s vertices=%d"), *Shape.Bone.ToString(), Points->Num());
                return nullptr;
            }
            for (const TSharedPtr<FJsonValue>& PointValue : *Points) // 每点三个厘米数值，禁止 NaN/巨大非厘米值。
            {
                const TArray<TSharedPtr<FJsonValue>>& Values = PointValue->AsArray(); // 当前 XYZ 数组。
                if (Values.Num() != 3)
                {
                    UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_REJECTED vertex must contain exactly XYZ"));
                    return nullptr;
                }
                const FVector Point(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber()); // 参考空间点。
                if (Point.ContainsNaN() || Point.GetAbsMax() > 1000.)
                {
                    UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_REJECTED vertex non-finite/outside model centimetre bounds"));
                    return nullptr;
                }
                Shape.Vertices.Add(Point);
            }
            Shapes.Add(MoveTemp(Shape));
        }
    }
    if (Shapes.IsEmpty())
    {
        UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_REJECTED no shapes for model=%s"), *ModelName);
        return nullptr;
    }
    UPackage* Package = CreatePackage(*PackagePath); // 目标专属包由 UObject 系统持有，调用方稍后保存。
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath); // 末段作为稳定资产名。
    UPhysicsAsset* Asset = LoadObject<UPhysicsAsset>(nullptr, *(PackagePath + TEXT(".") + AssetName)); // 已有时原位更新。
    if (!Asset)
    {
        Asset = NewObject<UPhysicsAsset>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
        FAssetRegistryModule::AssetCreated(Asset);
    }
    Asset->Modify();
    Asset->SkeletalBodySetups.Empty();
    Asset->ConstraintSetup.Empty(); // 受击查询资产明确不提供 ragdoll 或约束模拟。
    Asset->CollisionDisableTable.Empty();
    Asset->PreviewSkeletalMesh = Mesh;
    TMap<FName, USkeletalBodySetup*> Bodies; // 本次按骨合并，指针由 Asset.SkeletalBodySetups 强持有。
    for (const FDemoHitSourceShape& Shape : Shapes) // 逐零件转换轴，绝不以骨头位置直接减点替代旋转。
    {
        USkeletalBodySetup*& Body = Bodies.FindOrAdd(Shape.Bone); // 当前骨对应的唯一 BodySetup。
        if (!Body)
        {
            Body = NewObject<USkeletalBodySetup>(Asset, NAME_None, RF_Transactional);
            Body->BoneName = Shape.Bone;
            Body->PhysicsType = PhysType_Kinematic;
            Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
            Body->bConsiderForBounds = true;
            Body->DefaultInstance.SetCollisionProfileName(TEXT("Custom"));
            Body->DefaultInstance.SetObjectType(ECC_Pawn);
            Body->DefaultInstance.SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Body->DefaultInstance.SetResponseToAllChannels(ECR_Ignore);
            Body->DefaultInstance.SetResponseToChannel(ECC_GameTraceChannel2, ECR_Block); // 项目 WeaponTrace 专用通道。
            Body->DefaultInstance.bSimulatePhysics = false;
            Asset->SkeletalBodySetups.Add(Body);
        }
        const int32 BoneIndex = Mesh->GetRefSkeleton().FindBoneIndex(Shape.Bone); // 已在源检查中保证有效。
        const FTransform BoneComponent = GetReferenceComponentTransform(Mesh, BoneIndex); // 当前实际 FBX 骨参考坐标轴。
        FKConvexElem Convex; // 零件级凸包，环与凹护板已在源脚本拆分，不填核心窗口。
        Convex.SetName(FName(*Shape.Part));
        for (const FVector& Point : Shape.Vertices) // Point 为模型厘米，凸包点需要骨局部厘米。
        {
            Convex.VertexData.Add(BoneComponent.InverseTransformPosition(Point));
        }
        Convex.UpdateElemBox();
        Body->AggGeom.ConvexElems.Add(MoveTemp(Convex));
    }
    for (USkeletalBodySetup* Body : Asset->SkeletalBodySetups) // 烹制所有骨刚性碰撞；NullRHI 命令行同样可执行。
    {
        Body->InvalidatePhysicsData();
        Body->CreatePhysicsMeshes();
        for (FKConvexElem& Convex : Body->AggGeom.ConvexElems) // 显式保留实际凸包三角索引，供 DrawHitZones 与导航调试使用。
        {
            Convex.ComputeChaosConvexIndices(); // 引擎仅在索引为空时计算，不改变核心窗口或已有凸体边界。
        }
    }
    Asset->UpdateBodySetupIndexMap();
    Asset->UpdateBoundsBodiesArray();
    Asset->PostEditChange();
    Asset->MarkPackageDirty();
    Mesh->Modify();
    Mesh->SetPhysicsAsset(Asset);
    Mesh->MarkPackageDirty();
    UE_LOG(LogFPSDemo, Display, TEXT("HIT_ASSET_BUILT mesh=%s bodies=%d convex=%d"), *Mesh->GetName(), Bodies.Num(), Shapes.Num());
    return ValidateEnemyHitAsset(Mesh) ? Asset : nullptr;
}

bool UDemoEnemyHitAssetLibrary::ValidateEnemyHitAsset(USkeletalMesh* Mesh)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs mesh=%s"), __FUNCTION__, *GetNameSafe(Mesh));
    const UPhysicsAsset* Asset = Mesh ? Mesh->GetPhysicsAsset() : nullptr; // 网格强引用的受击资产。
    if (!Asset || Asset->SkeletalBodySetups.IsEmpty() || !Asset->ConstraintSetup.IsEmpty())
    {
        UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_INVALID missing bodies or unexpected simulation constraints"));
        return false;
    }
    TSet<FName> Bones; // 检查一骨一 Body，避免 Hit.BoneName 归属含糊。
    int32 ConvexCount = 0; // 验证日志统计所有查询凸体。
    for (USkeletalBodySetup* Body : Asset->SkeletalBodySetups) // 每个 body 必须为 Kinematic/QueryOnly；可按需加载内存派生碰撞。
    {
        if (!Body || Mesh->GetRefSkeleton().FindBoneIndex(Body->BoneName) == INDEX_NONE || Bones.Contains(Body->BoneName)
            || Body->PhysicsType != PhysType_Kinematic || Body->DefaultInstance.GetCollisionEnabled() != ECollisionEnabled::QueryOnly
            || Body->DefaultInstance.bSimulatePhysics || Body->GetCollisionTraceFlag() != CTF_UseSimpleAsComplex
            || Body->DefaultInstance.GetResponseToChannel(ECC_GameTraceChannel2) != ECR_Block || Body->AggGeom.ConvexElems.IsEmpty())
        {
            UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_INVALID body=%s"), *GetNameSafe(Body));
            return false;
        }
        Bones.Add(Body->BoneName);
        // Editor 读盘可能延迟创建 Chaos 对象；同步建立内存派生数据，不清空源几何、不保存或标脏包。
        Body->CreatePhysicsMeshes();
        if (!Body->bCreatedPhysicsMeshes || Body->bFailedToCreatePhysicsMeshes)
        {
            UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_INVALID_COOK_STATE bone=%s created=%d failed=%d"), *Body->BoneName.ToString(), static_cast<int32>(Body->bCreatedPhysicsMeshes), static_cast<int32>(Body->bFailedToCreatePhysicsMeshes));
            return false;
        }
        for (int32 ShapeIndex = 0; ShapeIndex < Body->AggGeom.ConvexElems.Num(); ++ShapeIndex) // 同步逐凸体测试真实 cooked 几何，不仅统计源点。
        {
            if (!ValidateCookedConvex(Body->AggGeom.ConvexElems[ShapeIndex], Body->BoneName, ShapeIndex)) return false;
            ++ConvexCount;
        }
    }
    if (!Bones.Contains(TEXT("body")) || !Bones.Contains(TEXT("core")) || !Bones.Contains(TEXT("forearm_l")) || !Bones.Contains(TEXT("forearm_r")))
    {
        UE_LOG(LogFPSDemo, Error, TEXT("HIT_ASSET_INVALID required damage regions absent"));
        return false;
    }
    UE_LOG(LogFPSDemo, Display, TEXT("HIT_ASSET_VALID mesh=%s bodies=%d convex=%d cookedRaycasts=%d"), *Mesh->GetName(), Bones.Num(), ConvexCount, ConvexCount);
    return true;
}
