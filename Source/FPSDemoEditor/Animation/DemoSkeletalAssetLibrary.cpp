#include "Animation/DemoSkeletalAssetLibrary.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Debug/DemoLog.h"
#include "AssetCompilingManager.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"

bool UDemoSkeletalAssetLibrary::SetSkeletonSocket(USkeletalMesh* Mesh, FName SocketName, FName BoneName, const FTransform& ComponentTransform)
{
    // 静态蓝图函数没有 this，沿用编辑器资产工具的同日志类别入口格式。
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs mesh=%s socket=%s bone=%s"), __FUNCTION__, *GetNameSafe(Mesh), *SocketName.ToString(), *BoneName.ToString());
    if (!Mesh || !Mesh->GetSkeleton() || SocketName.IsNone() || BoneName.IsNone()
        || ComponentTransform.ContainsNaN() || ComponentTransform.GetScale3D().GetMin() <= 0.f)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("SKELETON_SOCKET_REJECTED invalid asset/name/transform"));
        return false;
    }
    // 只借用当前网格的参考骨架，不用 Skeleton 重定向姿势替代其真实绑定轴。
    const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
    const int32 BoneIndex = Reference.FindBoneIndex(BoneName);
    if (BoneIndex == INDEX_NONE)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("SKELETON_SOCKET_REJECTED missing bone=%s"), *BoneName.ToString());
        return false;
    }
    // UE 变换组合为 Local * Parent；由父链得到该骨模型空间变换后反求 Socket 局部空间。
    FTransform BoneComponent = FTransform::Identity;
    for (int32 Index = BoneIndex; Index != INDEX_NONE; Index = Reference.GetParentIndex(Index)) // Index 沿参考父链回溯至单根。
    {
        BoneComponent = BoneComponent * Reference.GetRefBonePose()[Index];
    }
    const FTransform Relative = ComponentTransform.GetRelativeTransform(BoneComponent); // 只用于本次写入，单位厘米。
    USkeleton* Skeleton = Mesh->GetSkeleton(); // 借用资产，创建的 Socket 由 Skeleton 强持有。
    Skeleton->Modify();
    USkeletalMeshSocket* Socket = Skeleton->FindSocket(SocketName); // 同名幂等复用，不累积重复挂点。
    if (!Socket)
    {
        Socket = NewObject<USkeletalMeshSocket>(Skeleton, NAME_None, RF_Transactional);
        Skeleton->Sockets.Add(Socket);
    }
    Socket->Modify();
    Socket->SocketName = SocketName;
    Socket->BoneName = BoneName;
    Socket->RelativeLocation = Relative.GetTranslation();
    Socket->RelativeRotation = Relative.Rotator();
    Socket->RelativeScale = Relative.GetScale3D();
    Skeleton->PostEditChange();
    Skeleton->MarkPackageDirty();
    UE_LOG(LogFPSDemo, Log, TEXT("SKELETON_SOCKET_UPDATED socket=%s parent=%s relative=%s"), *SocketName.ToString(), *BoneName.ToString(), *Relative.ToString());
    return true;
}

bool UDemoSkeletalAssetLibrary::ValidateRigidSkin(USkeletalMesh* Mesh, int32 ExpectedWeightedBones)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs mesh=%s expectedWeightedBones=%d"), __FUNCTION__, *GetNameSafe(Mesh), ExpectedWeightedBones);
    FAssetCompilingManager::Get().FinishAllCompilation(); // Commandlet 导入后可能仍有异步构建，不能读取未完成的渲染权重。
    const FSkeletalMeshRenderData* RenderData = Mesh ? Mesh->GetResourceForRendering() : nullptr; // 资产拥有的只读渲染数据。
    if (!RenderData || RenderData->LODRenderData.IsEmpty() || ExpectedWeightedBones <= 0)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("RIGID_SKIN_REJECTED missing mesh/LOD/expected count"));
        return false;
    }
    const FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[0]; // 仅检查原始 LOD0，简化 LOD 可按引擎规则合并影响。
    const FSkinWeightVertexBuffer* Weights = LOD.GetSkinWeightVertexBuffer(); // 当前网格持有，无跨构建缓存。
    if (!Weights || Weights->GetNumVertices() == 0)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("RIGID_SKIN_REJECTED empty vertex weights"));
        return false;
    }
    TSet<int32> WeightedBones; // 记录实际影响几何的全局骨编号，排除只有父层级作用的 root/aim_yaw。
    uint32 CheckedVertices = 0; // 遍历 section 的顶点总数，检测遗漏或范围不一致。
    for (const FSkelMeshRenderSection& Section : LOD.RenderSections) // Section 属于 LOD，BoneMap 转换局部骨索引。
    {
        for (uint32 Vertex = Section.BaseVertexIndex; Vertex < Section.BaseVertexIndex + Section.NumVertices; ++Vertex)
        {
            uint32 NonZero = 0; // 当前顶点有效影响数；刚性金属要求恰为 1。
            if (Vertex >= Weights->GetNumVertices())
            {
                UE_LOG(LogFPSDemo, Error, TEXT("RIGID_SKIN_REJECTED vertex range=%u"), Vertex);
                return false;
            }
            for (uint32 Influence = 0; Influence < Weights->GetMaxBoneInfluences(); ++Influence) // Influence 是权重槽位，零权重忽略。
            {
                if (Weights->GetBoneWeight(Vertex, Influence) == 0) continue;
                ++NonZero;
                const uint32 LocalBone = Weights->GetBoneIndex(Vertex, Influence); // 索引先映射 section BoneMap，不能当全局骨编号。
                if (!Section.BoneMap.IsValidIndex(LocalBone) || Section.BoneMap[LocalBone] >= Mesh->GetRefSkeleton().GetNum())
                {
                    UE_LOG(LogFPSDemo, Error, TEXT("RIGID_SKIN_REJECTED invalid bone vertex=%u local=%u"), Vertex, LocalBone);
                    return false;
                }
                WeightedBones.Add(Section.BoneMap[LocalBone]);
            }
            if (NonZero != 1)
            {
                UE_LOG(LogFPSDemo, Error, TEXT("RIGID_SKIN_REJECTED vertex=%u influences=%u"), Vertex, NonZero);
                return false;
            }
            ++CheckedVertices;
        }
    }
    if (CheckedVertices != Weights->GetNumVertices() || WeightedBones.Num() != ExpectedWeightedBones)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("RIGID_SKIN_REJECTED coverage vertices=%u/%u weightedBones=%d/%d"), CheckedVertices, Weights->GetNumVertices(), WeightedBones.Num(), ExpectedWeightedBones);
        return false;
    }
    UE_LOG(LogFPSDemo, Display, TEXT("RIGID_SKIN_VALID mesh=%s vertices=%u weightedBones=%d"), *Mesh->GetName(), CheckedVertices, WeightedBones.Num());
    return true;
}
