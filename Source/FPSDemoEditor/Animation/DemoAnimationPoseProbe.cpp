// 职责：用临时 FPreviewScene 同步执行动画组件，导出真正的原生后处理结果。
// 依赖同名头、UnrealEd、Engine、Json和DemoLog；不保存包、不触发动画Notify、不模拟物理。
// ExportMannyMeleeRenderLOD0仅在本轮Saved目录导出原Manny实际渲染LBS输入，不替换SourceCache或生产蒙皮。
#include "Animation/DemoAnimationPoseProbe.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "AssetCompilingManager.h"
#include "PreviewScene.h"
#include "Debug/DemoLog.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "BoneWeights.h" // 与UE5.4 GetSkinnedVertexPosition一致的原始权重量化系数。
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

TMap<FName, FTransform> UDemoAnimationPoseProbe::SampleSequence(USkeletalMesh* Mesh, UAnimSequence* Sequence, float Time, bool bPostProcess)
{
    DEMO_LOG_CALL();
    if (!IsInGameThread() || !Mesh || !Sequence || Mesh->GetSkeleton() != Sequence->GetSkeleton()
        || !FMath::IsFinite(Time) || Time < 0.f || Time > Sequence->GetPlayLength() + KINDA_SMALL_NUMBER)
    {
        UE_LOG(LogFPSDemo, Error, TEXT("POSE_PROBE_REJECTED thread/asset/skeleton/time mesh=%s sequence=%s time=%f"), *GetNameSafe(Mesh), *GetNameSafe(Sequence), Time);
        return {};
    }
    FAssetCompilingManager::Get().FinishAllCompilation(); // 同步构建完成后才读取 LOD 和动画数据。
    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false)); // 一次调用持有的隔离预览世界。
    USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(); // 注册后由 Scene 持有至本函数结束。
    Component->SetSkeletalMesh(Mesh);
    Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Component->SetDisablePostProcessBlueprint(!bPostProcess);
    Component->SetForcedLOD(1); // API 使用 1 表示渲染 LOD0，确保所有修形骨都参与。
    Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Scene.AddComponent(Component, FTransform::Identity);
    Component->SetAnimation(Sequence);
    Component->SetPosition(FMath::Min(Time, Sequence->GetPlayLength()), false);
    Component->SetPlayRate(0.f);
    Component->TickAnimation(0.f, false);
    Component->RefreshBoneTransforms();
    const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton(); // 组件数组按网格骨索引排列。
    const TArray<FTransform>& Poses = Component->GetComponentSpaceTransforms(); // 当前同步求值结果，返回前复制。
    TMap<FName, FTransform> Result; // 返回独立值，不暴露预览组件内部内存。
    if (Poses.Num() != Reference.GetNum() || (bPostProcess && Mesh->GetPostProcessAnimBlueprint() && !Component->GetPostProcessInstance()))
    {
        UE_LOG(LogFPSDemo, Error, TEXT("POSE_PROBE_REJECTED incomplete pose/postprocess bones=%d expected=%d"), Poses.Num(), Reference.GetNum());
        return Result;
    }
    for (int32 Index = 0; Index < Reference.GetNum(); ++Index) // 索引仅在本次已完成求值的数组范围内使用。
    {
        if (Poses[Index].ContainsNaN())
        {
            UE_LOG(LogFPSDemo, Error, TEXT("POSE_PROBE_REJECTED non-finite bone=%d"), Index);
            return {};
        }
        Result.Add(Reference.GetBoneName(Index), Poses[Index]);
    }
    UE_LOG(LogFPSDemo, Log, TEXT("POSE_PROBE_SAMPLED sequence=%s seconds=%f postprocess=%d bones=%d"), *GetNameSafe(Sequence), Time, bPostProcess, Result.Num());
    return Result;
}

bool UDemoAnimationPoseProbe::ExportMannyMeleeRenderLOD0(USkeletalMesh* Mesh,const FString& OutputFile)
{
    DEMO_LOG_CALL();
    FString Output=FPaths::ConvertRelativePathToFull(OutputFile); // 只用于本次导出的绝对目标，不执行任意文件操作。
    FPaths::NormalizeFilename(Output); FPaths::CollapseRelativeDirectories(Output);
    FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("MeleeElbowClearance20261108"));
    FPaths::NormalizeDirectoryName(Directory);
    if (!IsInGameThread() || !Mesh || Mesh->GetPathName()!=TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny")
        || !Output.StartsWith(Directory+TEXT("/"),ESearchCase::IgnoreCase) || FPaths::GetExtension(Output)!=TEXT("json")
        || IFileManager::Get().FileExists(*Output))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT thread/mesh/path/existing target=%s"),*Output); return false; }
    FAssetCompilingManager::Get().FinishAllCompilation(); // 同步借用稳定的只读渲染数据，不触发重建/保存。
    const FSkeletalMeshRenderData* Render=Mesh->GetResourceForRendering();
    if (!Render || Render->LODRenderData.IsEmpty())
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT missing renderLOD0")); return false; }
    const FSkeletalMeshLODRenderData& LOD=Render->LODRenderData[0]; // 本工具不允许任何其他LOD。
    const FSkinWeightVertexBuffer* Weights=LOD.GetSkinWeightVertexBuffer();
    const FRawStaticIndexBuffer16or32Interface* IndexBuffer=LOD.MultiSizeIndexContainer.GetIndexBuffer();
    const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
    const auto& Inverses=Mesh->GetRefBasesInvMatrix(); // 引擎实际逆绑定数组，禁止用重新求逆参考姿态替代。
    if (!Weights || Weights->GetNumVertices()!=LOD.GetNumVertices() || !IndexBuffer || IndexBuffer->Num()%3!=0
        || Ref.GetNum()!=161 || Inverses.Num()!=Ref.GetNum())
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT incomplete actual buffers/ref")); return false; }
    TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>(); // 本次导出独占JSON，只包含实际只读数据。
    Root->SetStringField(TEXT("state"),TEXT("READ_ONLY_ACTUAL_MANNY_RENDER_LOD0_INPUTS"));
    Root->SetStringField(TEXT("mesh"),Mesh->GetPathName()); Root->SetNumberField(TEXT("lod"),0);
    Root->SetStringField(TEXT("coordinates"),TEXT("UE component centimetres; FMatrix rows serialized as stored; row-vector p*RefBasesInv*PoseMatrix; transpose for column-vector replay"));
    Root->SetNumberField(TEXT("inv_max_raw_bone_weight_float"),UE::AnimationCore::InvMaxRawBoneWeightFloat);
    Root->SetBoolField(TEXT("weights_renormalized"),false); Root->SetNumberField(TEXT("max_bone_influences"),Weights->GetMaxBoneInfluences());
    TArray<TSharedPtr<FJsonValue>> Sections,Vertices,Indices; // 原始索引顺序保持，不焊接或按空间重排。
    for (const FSkelMeshRenderSection& Section:LOD.RenderSections)
    {
        TSharedRef<FJsonObject> Entry=MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("base_vertex_index"),Section.BaseVertexIndex); Entry->SetNumberField(TEXT("num_vertices"),Section.NumVertices);
        TArray<TSharedPtr<FJsonValue>> BoneMap;
        for (const FBoneIndexType Index:Section.BoneMap)
        {
            if (Index>=Ref.GetNum()) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT section bone map")); return false; }
            TSharedRef<FJsonObject> Bone=MakeShared<FJsonObject>(); Bone->SetNumberField(TEXT("mesh_index"),Index);
            Bone->SetStringField(TEXT("bone"),Ref.GetBoneName(Index).ToString()); BoneMap.Add(MakeShared<FJsonValueObject>(Bone));
        }
        Entry->SetArrayField(TEXT("bone_map"),BoneMap); Sections.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Vertices.Reserve(LOD.GetNumVertices());
    for (uint32 Index=0;Index<LOD.GetNumVertices();++Index)
    {
        const FVector3f Position=LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index);
        int32 SectionIndex=0,SectionVertex=0; LOD.GetSectionFromVertexIndex(Index,SectionIndex,SectionVertex);
        if (!LOD.RenderSections.IsValidIndex(SectionIndex) || Position.ContainsNaN())
        { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT vertex=%u"),Index); return false; }
        const FSkelMeshRenderSection& Section=LOD.RenderSections[SectionIndex];
        TSharedRef<FJsonObject> Entry=MakeShared<FJsonObject>(); Entry->SetNumberField(TEXT("vertex"),Index);
        Entry->SetNumberField(TEXT("section"),SectionIndex);
        TArray<TSharedPtr<FJsonValue>> Point; Point.Add(MakeShared<FJsonValueNumber>(Position.X)); Point.Add(MakeShared<FJsonValueNumber>(Position.Y)); Point.Add(MakeShared<FJsonValueNumber>(Position.Z));
        Entry->SetArrayField(TEXT("position"),Point);
        TArray<TSharedPtr<FJsonValue>> Influences; uint32 RawSum=0; // 仅求和记录，不用总和重归一化。
        for (uint32 Slot=0;Slot<Weights->GetMaxBoneInfluences();++Slot)
        {
            const uint16 RawWeight=Weights->GetBoneWeight(Index,Slot);
            if (RawWeight==0) continue;
            const uint32 PaletteIndex=Weights->GetBoneIndex(Index,Slot);
            if (!Section.BoneMap.IsValidIndex(PaletteIndex))
            { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT vertex palette=%u"),Index); return false; }
            const int32 MeshBone=Section.BoneMap[PaletteIndex];
            if (!Ref.IsValidIndex(MeshBone))
            { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT vertex bone=%u"),Index); return false; }
            TSharedRef<FJsonObject> Influence=MakeShared<FJsonObject>();
            Influence->SetNumberField(TEXT("slot"),Slot); Influence->SetNumberField(TEXT("section_bone_index"),PaletteIndex);
            Influence->SetNumberField(TEXT("mesh_bone_index"),MeshBone); Influence->SetStringField(TEXT("bone"),Ref.GetBoneName(MeshBone).ToString());
            Influence->SetNumberField(TEXT("raw_weight_uint16"),RawWeight);
            Influence->SetNumberField(TEXT("engine_weight_float"),static_cast<float>(RawWeight)*UE::AnimationCore::InvMaxRawBoneWeightFloat);
            Influences.Add(MakeShared<FJsonValueObject>(Influence)); RawSum+=RawWeight;
        }
        if (Influences.IsEmpty()) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT no influences vertex=%u"),Index); return false; }
        Entry->SetNumberField(TEXT("raw_weight_sum"),RawSum); Entry->SetArrayField(TEXT("influences"),Influences);
        Vertices.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Indices.Reserve(IndexBuffer->Num());
    for (int32 Index=0;Index<IndexBuffer->Num();++Index)
    {
        const uint32 Vertex=IndexBuffer->Get(Index);
        if (Vertex>=LOD.GetNumVertices()) { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT triangle index")); return false; }
        Indices.Add(MakeShared<FJsonValueNumber>(Vertex));
    }
    TSharedRef<FJsonObject> Reference=MakeShared<FJsonObject>(),InverseRows=MakeShared<FJsonObject>();
    TArray<FTransform> ComponentReference; ComponentReference.SetNum(Ref.GetNum()); // 本次组件参考，只读父先子后组合。
    for (int32 Index=0;Index<Ref.GetNum();++Index)
    {
        const int32 Parent=Ref.GetParentIndex(Index);
        ComponentReference[Index]=Parent==INDEX_NONE?Ref.GetRefBonePose()[Index]:Ref.GetRefBonePose()[Index]*ComponentReference[Parent];
        const FTransform& Transform=ComponentReference[Index];
        TSharedRef<FJsonObject> Entry=MakeShared<FJsonObject>();
        const FVector P=Transform.GetTranslation(),S=Transform.GetScale3D(); const FQuat Q=Transform.GetRotation();
        TArray<TSharedPtr<FJsonValue>> Position,Rotation,Scale;
        Position.Add(MakeShared<FJsonValueNumber>(P.X)); Position.Add(MakeShared<FJsonValueNumber>(P.Y)); Position.Add(MakeShared<FJsonValueNumber>(P.Z));
        Rotation.Add(MakeShared<FJsonValueNumber>(Q.X)); Rotation.Add(MakeShared<FJsonValueNumber>(Q.Y)); Rotation.Add(MakeShared<FJsonValueNumber>(Q.Z)); Rotation.Add(MakeShared<FJsonValueNumber>(Q.W));
        Scale.Add(MakeShared<FJsonValueNumber>(S.X)); Scale.Add(MakeShared<FJsonValueNumber>(S.Y)); Scale.Add(MakeShared<FJsonValueNumber>(S.Z));
        Entry->SetArrayField(TEXT("location"),Position); Entry->SetArrayField(TEXT("rotation"),Rotation); Entry->SetArrayField(TEXT("scale"),Scale);
        Entry->SetNumberField(TEXT("parent_mesh_index"),Parent); Reference->SetObjectField(Ref.GetBoneName(Index).ToString(),Entry);
        TArray<TSharedPtr<FJsonValue>> Rows;
        for (int32 Row=0;Row<4;++Row)
        {
            TArray<TSharedPtr<FJsonValue>> Values;
            for (int32 Column=0;Column<4;++Column) Values.Add(MakeShared<FJsonValueNumber>(Inverses[Index].M[Row][Column]));
            Rows.Add(MakeShared<FJsonValueArray>(Values));
        }
        InverseRows->SetArrayField(Ref.GetBoneName(Index).ToString(),Rows);
    }
    Root->SetArrayField(TEXT("sections"),Sections); Root->SetArrayField(TEXT("vertices"),Vertices); Root->SetArrayField(TEXT("indices"),Indices);
    Root->SetObjectField(TEXT("reference_component"),Reference); Root->SetObjectField(TEXT("ref_bases_inv_matrix_rows_ue"),InverseRows);
    FString Json; const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
    if (!FJsonSerializer::Serialize(Root,Writer) || !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true)
        || !FFileHelper::SaveStringToFile(Json,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MELEE_RENDER_LOD0_EXPORT_REJECT serialization/write target=%s"),*Output); return false; }
    UE_LOG(LogFPSDemo,Log,TEXT("MELEE_RENDER_LOD0_EXPORT_SAVED mesh=%s vertices=%d indices=%d bones=%d path=%s"),
        *Mesh->GetName(),Vertices.Num(),Indices.Num(),Ref.GetNum(),*Output);
    return true;
}

TArray<FVector> UDemoAnimationPoseProbe::SampleSkinnedPositions(USkeletalMesh* Mesh,UAnimSequence* Sequence,float Time,int32 Stride)
{
    DEMO_LOG_CALL();
    if (!IsInGameThread() || !Mesh || !Sequence || Mesh->GetSkeleton()!=Sequence->GetSkeleton() || !FMath::IsFinite(Time)
        || Time<0.f || Time>Sequence->GetPlayLength()+KINDA_SMALL_NUMBER || Stride<1 || Stride>1024)
    { UE_LOG(LogFPSDemo,Warning,TEXT("SKIN_PROBE_REJECTED asset/thread/time/stride")); return {}; }
    FAssetCompilingManager::Get().FinishAllCompilation(); // 确保CPU可读LOD在借用期间已完成构建。
    const FSkeletalMeshRenderData* Render=Mesh->GetResourceForRendering(); // Mesh持有，函数不跨构建保留。
    if (!Render || Render->LODRenderData.IsEmpty())
    { UE_LOG(LogFPSDemo,Warning,TEXT("SKIN_PROBE_REJECTED missing render LOD")); return {}; }
    const FSkeletalMeshLODRenderData& LOD=Render->LODRenderData[0]; // 实际LOD0渲染顶点/section bone map。
    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false)); // 本次调用隔离场景。
    USkeletalMeshComponent* Component=NewObject<USkeletalMeshComponent>(); // Scene持有至返回，禁止访问游戏Pawn。
    Component->SetSkeletalMesh(Mesh); Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Component->SetDisablePostProcessBlueprint(true); Component->SetForcedLOD(1);
    Component->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Scene.AddComponent(Component,FTransform::Identity);
    Component->SetAnimation(Sequence); Component->SetPosition(FMath::Min(Time,Sequence->GetPlayLength()),false);
    Component->SetPlayRate(0.f); Component->TickAnimation(0.f,false); Component->RefreshBoneTransforms();
    FSkinWeightVertexBuffer* Weights=Component->GetSkinWeightBuffer(0); // 本实例实际权重，section局部索引由引擎转换。
    if (!Weights || Weights->GetNumVertices()!=LOD.GetNumVertices())
    { UE_LOG(LogFPSDemo,Warning,TEXT("SKIN_PROBE_REJECTED inconsistent vertex weights")); return {}; }
    TArray<FMatrix44f> RefToLocal; Component->GetCurrentRefToLocalMatrices(RefToLocal,0); // 同帧真实骨矩阵与资产逆绑定矩阵。
    TArray<FVector> Result; Result.Reserve((LOD.GetNumVertices()+Stride-1)/Stride); // 返回独立采样值。
    for (uint32 Vertex=0;Vertex<LOD.GetNumVertices();Vertex+=Stride)
    {
        const FVector Point(USkinnedMeshComponent::GetSkinnedVertexPosition(Component,Vertex,LOD,*Weights,RefToLocal)); // 引擎线性蒙皮，不手工推定bone map。
        if (Point.ContainsNaN()) { UE_LOG(LogFPSDemo,Warning,TEXT("SKIN_PROBE_REJECTED nonfinite vertex=%u"),Vertex); return {}; }
        Result.Add(Point);
    }
    UE_LOG(LogFPSDemo,Log,TEXT("SKIN_PROBE_SAMPLED mesh=%s points=%d"),*Mesh->GetName(),Result.Num());
    return Result;
}
