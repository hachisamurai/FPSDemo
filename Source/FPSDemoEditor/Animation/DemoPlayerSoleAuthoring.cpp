#include "Animation/DemoPlayerSoleAuthoring.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h" // FSkeletalMaterial完整定义位于此，独立编译不能依赖其他翻译单元的传递包含。
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "AssetCompilingManager.h"
#include "Debug/DemoLog.h"

bool DemoPlayerSoleAuthoring::Extract(USkeletalMesh* Mesh, TArray<FDemoPlayerSolePoint>& Left, TArray<FDemoPlayerSolePoint>& Right)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    Left.Reset(); Right.Reset();
    FAssetCompilingManager::Get().FinishAllCompilation(); // Commandlet的网格导入异步构建完成后才能读取真实LOD0权重。
    const FSkeletalMeshRenderData* Render=Mesh?Mesh->GetResourceForRendering():nullptr; // 只在本同步函数内借用，资源归Mesh。
    if (!Render || Render->LODRenderData.IsEmpty()) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_SOLE_MISSING_RENDER_DATA")); return false; }
    const FSkeletalMeshLODRenderData& LOD=Render->LODRenderData[0]; // 当前本Body只有LOD0，运行节点无需读渲染缓冲。
    const FSkinWeightVertexBuffer* Weights=LOD.GetSkinWeightVertexBuffer(); // 每section的局部骨索引需经过BoneMap。
    if (!Weights) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_SOLE_MISSING_WEIGHTS")); return false; }
    const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton(); // 实际Body的参考骨，导入器已与原Skeleton逐骨校验。
    TArray<FTransform> Bind=Ref.GetRefBonePose(); // 转组件空间的值副本，不改变共享参考骨。
    for (int32 Index=0; Index<Bind.Num(); ++Index) // 父骨先于子骨，UE Transform右乘父。
        if (Ref.GetParentIndex(Index)!=INDEX_NONE) Bind[Index]=Bind[Index]*Bind[Ref.GetParentIndex(Index)];
    const int32 FootBones[2]={Ref.FindBoneIndex(TEXT("foot_l")),Ref.FindBoneIndex(TEXT("foot_r"))}; // 左右对应输出数组。
    const int32 BallBones[2]={Ref.FindBoneIndex(TEXT("ball_l")),Ref.FindBoneIndex(TEXT("ball_r"))}; // 脚趾是当前鞋前掌实际次要影响骨。
    if (FootBones[0]==INDEX_NONE || FootBones[1]==INDEX_NONE || BallBones[0]==INDEX_NONE || BallBones[1]==INDEX_NONE) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_SOLE_MISSING_BONES")); return false; }
    TArray<FDemoPlayerSolePoint> Candidates[2]; // 临时所有鞋顶点蒙皮值；保留最低平面全部唯一点，兼容Art05八边鞋底而非丢弃四角以外的点。
    TArray<FVector> Positions[2]; // 与Candidates同序的绑定组件位置，用于高度过滤/重复裂分顶点合并。
    float Lowest[2]={TNumericLimits<float>::Max(),TNumericLimits<float>::Max()}; // 本侧实际鞋底参考高度cm。
    for (const FSkelMeshRenderSection& Section:LOD.RenderSections)
    {
        if (!Mesh->GetMaterials().IsValidIndex(Section.MaterialIndex) || Mesh->GetMaterials()[Section.MaterialIndex].MaterialSlotName!=TEXT("Shoe")) continue; // 独立鞋材质是提取边界，裤脚/皮肤不得成为足底。
        for (uint32 Vertex=Section.BaseVertexIndex; Vertex<Section.BaseVertexIndex+Section.NumVertices; ++Vertex)
        {
            float FootWeights[2]={0,0}; // 引擎量化权重，归一化前按本侧足骨累加。
            float BallWeights[2]={0,0}; // 本侧脚趾影响；拒绝任何别的关节。
            for (uint32 Influence=0; Influence<Weights->GetMaxBoneInfluences(); ++Influence)
            {
                const uint16 Weight=Weights->GetBoneWeight(Vertex,Influence); // 0影响跳过，不解析其未定义骨索引。
                if (!Weight) continue;
                const uint32 LocalBone=Weights->GetBoneIndex(Vertex,Influence); // section-local索引，不是Ref骨索引。
                if (!Section.BoneMap.IsValidIndex(LocalBone)) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_SOLE_BONE_MAP_INVALID")); return false; }
                const int32 Bone=Section.BoneMap[LocalBone]; // 映射后的真实模型骨索引。
                if (Bone==FootBones[0]) FootWeights[0]+=Weight;
                else if (Bone==FootBones[1]) FootWeights[1]+=Weight;
                else if (Bone==BallBones[0]) BallWeights[0]+=Weight;
                else if (Bone==BallBones[1]) BallWeights[1]+=Weight;
                else { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_SOLE_UNSUPPORTED_INFLUENCE bone=%s"),*Ref.GetBoneName(Bone).ToString()); return false; }
            }
            const int32 Side=FootWeights[0]+BallWeights[0]>0?0:1; // 根据真实权重分侧，不能按世界X猜左右。
            const float Total=FootWeights[Side]+BallWeights[Side]; // 归一化抵消8/16位权重表示差异。
            if (Total<=0 || FootWeights[1-Side]+BallWeights[1-Side]>0) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_SOLE_CROSS_SIDE_OR_EMPTY")); return false; }
            const FVector Position(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Vertex)); // 当前实际导入的参考组件位置cm。
            FDemoPlayerSolePoint Point; // 同一个角点在两影响骨局部的位置，与GPU蒙皮使用相同参考逆矩阵。
            Point.FootLocalPosition=Bind[FootBones[Side]].InverseTransformPosition(Position);
            Point.BallLocalPosition=Bind[BallBones[Side]].InverseTransformPosition(Position);
            Point.FootWeight=FootWeights[Side]/Total;
            Candidates[Side].Add(Point); Positions[Side].Add(Position); Lowest[Side]=FMath::Min(Lowest[Side],static_cast<float>(Position.Z));
        }
    }
    for (int32 Side=0; Side<2; ++Side)
    {
        TArray<FDemoPlayerSolePoint>& Result=Side==0?Left:Right; // 本侧输出借用，函数返回后由Appearance/AnimGraph持有。
        TArray<FVector> Unique; // 鞋硬边导致LOD0同位置多顶点，不能重复计为多个几何角点。
        for (int32 Index=0; Index<Candidates[Side].Num(); ++Index)
        {
            if (Positions[Side][Index].Z>Lowest[Side]+.05f) continue; // 当前鞋底参考为水平多边形，非底面鞋帮不加入；原高度容差保持。
            bool bDuplicate=false; // 同步比较已收集底面点，消除硬边拆点；不截断八边底面或保存异步回调。
            for (const FVector& Existing:Unique) if (Existing.Equals(Positions[Side][Index],.001f)) { bDuplicate=true; break; }
            if (!bDuplicate) { Unique.Add(Positions[Side][Index]); Result.Add(Candidates[Side][Index]); }
        }
        // 运行节点本来就遍历全部点求最低值；只扩展作者输入数量，不改变贴地、骨长或穿透容差。
        if (Result.Num()<4 || Result.Num()>32) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_SOLE_CORNER_COUNT side=%d count=%d supported=4..32"),Side,Result.Num()); return false; }
    }
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_SOLE_EXTRACTED left=%d right=%d bindZ=(%.4f,%.4f)"),Left.Num(),Right.Num(),Lowest[0],Lowest[1]);
    return true;
}
