// 职责：用UE5.4原生资产接口创建独立Mesh枪口火光插槽，并导出真实LOD0参考几何/权重/Socket事实。
// 使用同名头的Editor Python入口；仅允许当前四枪，不自动选点、不改原Muzzle、不保存任何UE包。
// 复用本项目DemoAnimationPoseProbe的RenderLOD0只读数据路径；不放宽原Manny专用导出门禁或引入第三方依赖。
#include "Diagnostics/DemoMuzzleFlashAuthoring.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Particles/ParticleSystemComponent.h"
#include "AssetCompilingManager.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Materials/MaterialInterface.h"
#include "BoneWeights.h"
#include "Debug/DemoStaticLog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

namespace
{
    const FName FlashSocketName(TEXT("MuzzleFlash")); // 唯一可写名称；原Muzzle及所有其他插槽只读。

    /** 当前四枪CDO实际Config.Mesh的完整对象路径白名单；不加载另一资源或接受同名替身。 */
    bool IsTrustedWeapon(const USkeletalMesh* Mesh)
    {
        DEMO_LOG_STATIC_CALL(Mesh);
        if(!Mesh||!Mesh->GetSkeleton()) return false;
        const FString Path=Mesh->GetPathName(); // 当前借用对象实际完整路径，不用显示Label或短名匹配。
        return Path==TEXT("/FPSAnimationPack/Meshes/Weapons/M1911/SKM_M1911A1.SKM_M1911A1")
            ||Path==TEXT("/FPSAnimationPack/Meshes/Weapons/MX16A4/SKM_MX16A4.SKM_MX16A4")
            ||Path==TEXT("/FPSAnimationPack/Meshes/Weapons/Drake-12/SKM_Drake-12.SKM_Drake-12")
            ||Path==TEXT("/FPSAnimationPack/Meshes/Weapons/L96X/SKM_L96X.SKM_L96X");
    }

    /** 纯值三分量保原坐标/单位；本JSON辅助逐点调用，日志仅限时追踪开启。 */
    TArray<TSharedPtr<FJsonValue>> VectorArray(const FVector& Value)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        return {MakeShared<FJsonValueNumber>(Value.X),MakeShared<FJsonValueNumber>(Value.Y),MakeShared<FJsonValueNumber>(Value.Z)};
    }

    /** UE矩阵按实际存储行导出；Matrix可为FMatrix或FMatrix44f，不转轴/转置/缩单位。 */
    template<typename MatrixType> TArray<TSharedPtr<FJsonValue>> MatrixRows(const MatrixType& Matrix)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        TArray<TSharedPtr<FJsonValue>> Rows; // 四行各四列，Python列向量重放须自行转置一次。
        for(int32 Row=0;Row<4;++Row)
        {
            TArray<TSharedPtr<FJsonValue>> Values; // 当前真实矩阵行，不由分解TRS重新估计逆绑定值。
            for(int32 Column=0;Column<4;++Column) Values.Add(MakeShared<FJsonValueNumber>(Matrix.M[Row][Column]));
            Rows.Add(MakeShared<FJsonValueArray>(Values));
        }
        return Rows;
    }

    /** 导出组件/局部TRS及UE实际矩阵；调用方已经验证Transform有限。 */
    TSharedRef<FJsonObject> TransformObject(const FTransform& Transform)
    {
        DEMO_LOG_STATIC_TICK(nullptr);
        TSharedRef<FJsonObject> Object=MakeShared<FJsonObject>(); // 单次返回独立值，无输入对象生命周期依赖。
        const FQuat Rotation=Transform.GetRotation(); // XYZW，不是欧拉角；防导入器猜旋转顺序。
        Object->SetArrayField(TEXT("translation_cm"),VectorArray(Transform.GetTranslation()));
        Object->SetArrayField(TEXT("rotation_xyzw"),{MakeShared<FJsonValueNumber>(Rotation.X),MakeShared<FJsonValueNumber>(Rotation.Y),MakeShared<FJsonValueNumber>(Rotation.Z),MakeShared<FJsonValueNumber>(Rotation.W)});
        Object->SetArrayField(TEXT("scale_xyz"),VectorArray(Transform.GetScale3D()));
        Object->SetArrayField(TEXT("matrix_rows_ue"),MatrixRows(Transform.ToMatrixWithScale()));
        return Object;
    }

    /** 借真实Mesh参考局部姿态，父先子后组成组件参考矩阵；失败不输出局部/不完整数组。 */
    bool BuildReference(const USkeletalMesh* Mesh,TArray<FTransform>& ComponentReference)
    {
        DEMO_LOG_STATIC_CALL(Mesh);
        const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton(); // Mesh自身绑定参考，不用Skeleton重定向姿态替换。
        if(Ref.GetNum()<=0||Ref.GetRefBonePose().Num()!=Ref.GetNum())
        { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_REFERENCE_REJECT empty/inconsistent reference")); return false; }
        ComponentReference.SetNum(Ref.GetNum());
        for(int32 Index=0;Index<Ref.GetNum();++Index)
        {
            const int32 Parent=Ref.GetParentIndex(Index); // 必须已经处理完该父骨，避免循环/越界参考链。
            if(Parent<INDEX_NONE||Parent>=Index||Ref.GetRefBonePose()[Index].ContainsNaN())
            { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_REFERENCE_REJECT bone=%d parent=%d"),Index,Parent); return false; }
            ComponentReference[Index]=Parent==INDEX_NONE?Ref.GetRefBonePose()[Index]:Ref.GetRefBonePose()[Index]*ComponentReference[Parent];
            if(ComponentReference[Index].ContainsNaN())
            { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_REFERENCE_REJECT nonfinite component bone=%d"),Index); return false; }
        }
        return true;
    }

    /** 保留Socket作者归属/顺序，并根据本Mesh参考父骨合成组件变换；缺父骨仅记事实，不伪造原点。 */
    TSharedRef<FJsonObject> SocketObject(const USkeletalMesh* Mesh,const USkeletalMeshSocket* Socket,const TCHAR* OwnerKind,const TArray<FTransform>& Reference)
    {
        DEMO_LOG_STATIC_TICK(Mesh);
        TSharedRef<FJsonObject> Object=MakeShared<FJsonObject>(); // 只读作者字段；绝不调用Modify或隐式创建Socket。
        Object->SetStringField(TEXT("owner_kind"),OwnerKind);
        Object->SetBoolField(TEXT("is_null"),Socket==nullptr);
        if(!Socket) return Object;
        Object->SetStringField(TEXT("object"),Socket->GetPathName());
        Object->SetStringField(TEXT("outer"),GetPathNameSafe(Socket->GetOuter()));
        Object->SetStringField(TEXT("name"),Socket->SocketName.ToString()); Object->SetStringField(TEXT("bone"),Socket->BoneName.ToString());
        Object->SetBoolField(TEXT("force_always_animated"),Socket->bForceAlwaysAnimated);
        Object->SetBoolField(TEXT("active_for_this_mesh"),Mesh->FindSocket(Socket->SocketName)==Socket);
        const int32 BoneIndex=Mesh->GetRefSkeleton().FindBoneIndex(Socket->BoneName); // Skeleton上的其他网格用骨可能不在本Mesh，明确标无参考组件解。
        const FTransform Local(Socket->RelativeRotation,Socket->RelativeLocation,Socket->RelativeScale); // 实际作者骨局部厘米/度/scale。
        const bool bLocalFinite=!Local.ContainsNaN(); // 非有限作者值也不能写成合法JSON数值。
        Object->SetBoolField(TEXT("local_transform_finite"),bLocalFinite);
        if(bLocalFinite) Object->SetObjectField(TEXT("bone_local"),TransformObject(Local));
        Object->SetNumberField(TEXT("parent_mesh_bone_index"),BoneIndex);
        const bool bValid=bLocalFinite&&Reference.IsValidIndex(BoneIndex); // 仅参考矩阵可解，不是游戏动画求值证明。
        Object->SetBoolField(TEXT("component_reference_valid"),bValid);
        if(bValid) Object->SetObjectField(TEXT("component_reference"),TransformObject(Local*Reference[BoneIndex]));
        return Object;
    }

    /** 单独导出两个作者集合，保同名遮蔽事实；FindSocket的active字段说明当前实际优先命中者。 */
    TSharedRef<FJsonObject> SocketFactsObject(const USkeletalMesh* Mesh,const TArray<FTransform>& Reference)
    {
        DEMO_LOG_STATIC_CALL(Mesh);
        TSharedRef<FJsonObject> Object=MakeShared<FJsonObject>(); // 调用期间借用Mesh/Skeleton，返回值无UObject指针。
        Object->SetStringField(TEXT("mesh"),Mesh->GetPathName()); Object->SetStringField(TEXT("skeleton"),Mesh->GetSkeleton()->GetPathName());
        Object->SetStringField(TEXT("scope"),TEXT("Saved asset author definitions and this mesh reference pose; not animated/RHI world sockets"));
        TArray<TSharedPtr<FJsonValue>> MeshSockets,SkeletonSockets; // 两集合原序保留，不合并/去重掩盖同名定义。
        for(const USkeletalMeshSocket* Socket:Mesh->GetMeshOnlySocketList()) MeshSockets.Add(MakeShared<FJsonValueObject>(SocketObject(Mesh,Socket,TEXT("mesh_only"),Reference)));
        for(const USkeletalMeshSocket* Socket:Mesh->GetSkeleton()->Sockets) SkeletonSockets.Add(MakeShared<FJsonValueObject>(SocketObject(Mesh,Socket,TEXT("skeleton"),Reference)));
        Object->SetArrayField(TEXT("mesh_only"),MeshSockets); Object->SetArrayField(TEXT("skeleton_sockets"),SkeletonSockets);
        return Object;
    }
}

bool UDemoMuzzleFlashAuthoring::ExportWeaponReference(USkeletalMesh* Mesh,const FString& SavedOutputJSON)
{
    DEMO_LOG_STATIC_CALL(Mesh);
    FString Output=FPaths::ConvertRelativePathToFull(SavedOutputJSON); // 唯一可写目标，限定本次Saved制作证据目录且不覆盖历史。
    FPaths::NormalizeFilename(Output); FPaths::CollapseRelativeDirectories(Output);
    FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("MuzzleFlashSockets20261117")); // 固定目录不能由外部参数改写。
    FPaths::NormalizeDirectoryName(Directory);
    if(!IsInGameThread()||!IsTrustedWeapon(Mesh)||!Output.StartsWith(Directory+TEXT("/"),ESearchCase::IgnoreCase)
        ||!FPaths::GetExtension(Output).Equals(TEXT("json"),ESearchCase::IgnoreCase)||IFileManager::Get().FileExists(*Output))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT thread/whitelist/path/existing target=%s"),*Output); return false; }
    FAssetCompilingManager::Get().FinishAllCompilation(); // 只等已有资产编译完成后借稳定CPU缓冲，不请求重建或保存。
    const FSkeletalMeshRenderData* Render=Mesh->GetResourceForRendering(); // Mesh拥有；仅本同步调用内借用，不跨资产构建保留。
    if(!Render||Render->LODRenderData.IsEmpty())
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT missing actual renderLOD0")); return false; }
    const FSkeletalMeshLODRenderData& LOD=Render->LODRenderData[0]; // 真实render LOD0，不取导入SourceModel或简化LOD。
    const FSkinWeightVertexBuffer* Weights=LOD.GetSkinWeightVertexBuffer(); // 当前实际LOD原始量化权重。
    const FRawStaticIndexBuffer16or32Interface* IndexBuffer=LOD.MultiSizeIndexContainer.GetIndexBuffer(); // 原三角顺序，不焊接/重排。
    const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
    const auto& Inverses=Mesh->GetRefBasesInvMatrix(); // 引擎真实逆绑定，不由参考矩阵重新求逆替换。
    TArray<FTransform> Reference; // Mesh真实局部参考组成的组件变换，本次函数内独占。
    if(!Weights||LOD.GetNumVertices()==0||Weights->GetNumVertices()!=LOD.GetNumVertices()||LOD.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices()!=LOD.GetNumVertices()
        ||!IndexBuffer||IndexBuffer->Num()==0||IndexBuffer->Num()%3!=0||Inverses.Num()!=Ref.GetNum()||!BuildReference(Mesh,Reference))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT incomplete positions/indices/weights/reference")); return false; }
    TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>(); // 所有输出均为实际输入快照，不生成枪口候选坐标。
    Root->SetStringField(TEXT("state"),TEXT("READ_ONLY_ACTUAL_WEAPON_RENDER_LOD0_REFERENCE"));
    Root->SetStringField(TEXT("mesh"),Mesh->GetPathName()); Root->SetNumberField(TEXT("lod"),0);
    Root->SetStringField(TEXT("coordinates"),TEXT("UE component reference centimetres; UE row-vector matrices as stored; p*RefBasesInv*PoseMatrix, transpose once for column-vector replay"));
    Root->SetStringField(TEXT("limitation"),TEXT("Reference inputs only; no animation, cloth, morph, material WPO, RHI skinning or visible barrel-exit selection"));
    Root->SetNumberField(TEXT("vertex_count"),LOD.GetNumVertices()); Root->SetNumberField(TEXT("triangle_count"),IndexBuffer->Num()/3);
    Root->SetNumberField(TEXT("inv_max_raw_bone_weight_float"),UE::AnimationCore::InvMaxRawBoneWeightFloat);
    Root->SetBoolField(TEXT("weights_renormalized"),false); Root->SetNumberField(TEXT("max_bone_influences"),Weights->GetMaxBoneInfluences());
    TArray<TSharedPtr<FJsonValue>> Positions,Vertices,Indices,Sections,Materials,Bones; // 按原顶点/section/骨/材质索引稳定顺序，positions_cm可直接离线绘制。
    const TArray<FSkeletalMaterial>& MeshMaterials=Mesh->GetMaterials(); // 只读Mesh材质槽，不重新分配材质。
    for(int32 Slot=0;Slot<MeshMaterials.Num();++Slot)
    {
        const FSkeletalMaterial& Material=MeshMaterials[Slot]; // 当前真实槽配置，null材质也作为事实保留。
        TSharedRef<FJsonObject> Entry=MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("slot"),Slot); Entry->SetStringField(TEXT("name"),Material.MaterialSlotName.ToString());
        Entry->SetStringField(TEXT("imported_name"),Material.ImportedMaterialSlotName.ToString());
        Entry->SetStringField(TEXT("material"),GetPathNameSafe(Material.MaterialInterface.Get())); Materials.Add(MakeShared<FJsonValueObject>(Entry));
    }
    for(int32 SectionIndex=0;SectionIndex<LOD.RenderSections.Num();++SectionIndex)
    {
        const FSkelMeshRenderSection& Section=LOD.RenderSections[SectionIndex]; // 原渲染section，含实际三角范围及骨palette。
        if(Section.BaseVertexIndex+Section.NumVertices>LOD.GetNumVertices()||Section.BaseIndex+Section.NumTriangles*3>static_cast<uint32>(IndexBuffer->Num()))
        { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT section range=%d"),SectionIndex); return false; }
        TSharedRef<FJsonObject> Entry=MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("section"),SectionIndex); Entry->SetNumberField(TEXT("material_slot"),Section.MaterialIndex);
        Entry->SetNumberField(TEXT("base_vertex_index"),Section.BaseVertexIndex); Entry->SetNumberField(TEXT("num_vertices"),Section.NumVertices);
        Entry->SetNumberField(TEXT("base_index"),Section.BaseIndex); Entry->SetNumberField(TEXT("num_triangles"),Section.NumTriangles);
        TArray<TSharedPtr<FJsonValue>> BoneMap; // 各weight局部骨索引到本Mesh全局骨索引的实际映射。
        for(const FBoneIndexType Bone:Section.BoneMap)
        {
            if(!Ref.IsValidIndex(Bone)) { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT section bone palette")); return false; }
            BoneMap.Add(MakeShared<FJsonValueNumber>(Bone));
        }
        Entry->SetArrayField(TEXT("bone_map_mesh_indices"),BoneMap); Sections.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Positions.Reserve(LOD.GetNumVertices()); Vertices.Reserve(LOD.GetNumVertices());
    for(uint32 Index=0;Index<LOD.GetNumVertices();++Index)
    {
        const FVector3f Position=LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index); // 实际renderLOD0绑定组件坐标，未经过蒙皮/材质。
        int32 SectionIndex=0,SectionVertex=0; LOD.GetSectionFromVertexIndex(Index,SectionIndex,SectionVertex); // 引擎查真实顶点section，不按最近骨或AABB猜分组。
        if(Position.ContainsNaN()||!LOD.RenderSections.IsValidIndex(SectionIndex))
        { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT vertex position/section=%u"),Index); return false; }
        Positions.Add(MakeShared<FJsonValueArray>(VectorArray(FVector(Position))));
        const FSkelMeshRenderSection& Section=LOD.RenderSections[SectionIndex];
        TSharedRef<FJsonObject> Entry=MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("vertex"),Index); Entry->SetNumberField(TEXT("section"),SectionIndex);
        TArray<TSharedPtr<FJsonValue>> Influences; uint32 RawSum=0; // 只记录实际总和，不重归一化或修改原权重。
        for(uint32 Slot=0;Slot<Weights->GetMaxBoneInfluences();++Slot)
        {
            const uint16 RawWeight=Weights->GetBoneWeight(Index,Slot); // 精确保留量化权重，零权重不当有效骨。
            if(RawWeight==0) continue;
            const uint32 PaletteIndex=Weights->GetBoneIndex(Index,Slot); // 先经section palette，禁止直接当全局骨号。
            if(!Section.BoneMap.IsValidIndex(PaletteIndex)||!Ref.IsValidIndex(Section.BoneMap[PaletteIndex]))
            { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT vertex palette/bone=%u"),Index); return false; }
            const int32 Bone=Section.BoneMap[PaletteIndex];
            TSharedRef<FJsonObject> Influence=MakeShared<FJsonObject>();
            Influence->SetNumberField(TEXT("slot"),Slot); Influence->SetNumberField(TEXT("section_bone_index"),PaletteIndex);
            Influence->SetNumberField(TEXT("mesh_bone_index"),Bone); Influence->SetStringField(TEXT("bone"),Ref.GetBoneName(Bone).ToString());
            Influence->SetNumberField(TEXT("raw_weight_uint16"),RawWeight); Influence->SetNumberField(TEXT("engine_weight_float"),static_cast<float>(RawWeight)*UE::AnimationCore::InvMaxRawBoneWeightFloat);
            Influences.Add(MakeShared<FJsonValueObject>(Influence)); RawSum+=RawWeight;
        }
        if(Influences.IsEmpty()) { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT no influences vertex=%u"),Index); return false; }
        Entry->SetNumberField(TEXT("raw_weight_sum"),RawSum); Entry->SetArrayField(TEXT("influences"),Influences); Vertices.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Indices.Reserve(IndexBuffer->Num());
    for(int32 Index=0;Index<IndexBuffer->Num();++Index)
    {
        const uint32 Vertex=IndexBuffer->Get(Index); // 每连续三个索引为一个原始三角，不改变绕序。
        if(Vertex>=LOD.GetNumVertices()) { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT triangle vertex")); return false; }
        Indices.Add(MakeShared<FJsonValueNumber>(Vertex));
    }
    for(int32 Index=0;Index<Ref.GetNum();++Index)
    {
        if(Inverses[Index].ContainsNaN()) { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT nonfinite inverse bind bone=%d"),Index); return false; }
        TSharedRef<FJsonObject> Entry=MakeShared<FJsonObject>();
        Entry->SetNumberField(TEXT("mesh_bone_index"),Index); Entry->SetStringField(TEXT("name"),Ref.GetBoneName(Index).ToString());
        Entry->SetNumberField(TEXT("parent_mesh_bone_index"),Ref.GetParentIndex(Index));
        Entry->SetObjectField(TEXT("bone_local_reference"),TransformObject(Ref.GetRefBonePose()[Index]));
        Entry->SetObjectField(TEXT("component_reference"),TransformObject(Reference[Index]));
        Entry->SetArrayField(TEXT("ref_bases_inv_matrix_rows_ue"),MatrixRows(Inverses[Index])); Bones.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Root->SetArrayField(TEXT("positions_cm"),Positions); Root->SetArrayField(TEXT("indices"),Indices); Root->SetArrayField(TEXT("vertices"),Vertices);
    Root->SetArrayField(TEXT("sections"),Sections); Root->SetArrayField(TEXT("material_slots"),Materials); Root->SetArrayField(TEXT("reference_bones"),Bones);
    Root->SetObjectField(TEXT("socket_facts"),SocketFactsObject(Mesh,Reference));
    FString Json; // 先完整序列化，再只向独占新目标落盘；失败不保存UE输入包。
    const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
    if(!FJsonSerializer::Serialize(Root,Writer)||!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true)
        ||!FFileHelper::SaveStringToFile(Json,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_NoReplaceExisting))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_EXPORT_REJECT serialize/write target=%s"),*Output); return false; }
    UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_FLASH_REFERENCE_EXPORTED mesh=%s vertices=%u triangles=%d file=%s"),*Mesh->GetPathName(),LOD.GetNumVertices(),IndexBuffer->Num()/3,*Output);
    return true;
}

bool UDemoMuzzleFlashAuthoring::SetMeshMuzzleFlashSocket(USkeletalMesh* Mesh,FName BoneName,const FTransform& ComponentTransform)
{
    DEMO_LOG_STATIC_CALL(Mesh);
    if(!IsInGameThread()||!IsTrustedWeapon(Mesh)||BoneName.IsNone()||ComponentTransform.ContainsNaN()
        ||!ComponentTransform.GetRotation().IsNormalized()||ComponentTransform.GetScale3D().GetMin()<=UE_SMALL_NUMBER)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SOCKET_REJECT thread/whitelist/bone/transform")); return false; }
    FAssetCompilingManager::Get().FinishAllCompilation(); // 等既有构建完成后改Socket，不请求资产重新生成。
    TArray<FTransform> Reference; // 只读Mesh参考链，用于从指定组件位置反求父骨局部变换。
    if(!BuildReference(Mesh,Reference)) return false;
    const int32 BoneIndex=Mesh->GetRefSkeleton().FindBoneIndex(BoneName); // 显式指定父骨，不能根据旧Muzzle或最远顶点猜测。
    if(!Reference.IsValidIndex(BoneIndex)||Reference[BoneIndex].GetScale3D().GetMin()<=UE_SMALL_NUMBER)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SOCKET_REJECT missing/degenerate parent bone=%s"),*BoneName.ToString()); return false; }
    const FTransform Relative=ComponentTransform.GetRelativeTransform(Reference[BoneIndex]); // UE组合Local*Parent，单位仍为厘米。
    const FTransform SocketLocal(Relative.Rotator(),Relative.GetTranslation(),Relative.GetScale3D()); // 实际Socket以Rotator存旋转，写前先核其可表达的重组结果。
    if(Relative.ContainsNaN()||Relative.GetScale3D().GetMin()<=UE_SMALL_NUMBER||!(SocketLocal*Reference[BoneIndex]).Equals(ComponentTransform,.001))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SOCKET_REJECT invalid relative transform")); return false; }
    USkeletalMeshSocket* Existing=nullptr; // 只查Mesh自己的集合，不把Skeleton同名对象拿来编辑。
    int32 MatchCount=0; // 重复同名作者数据拒绝修改，避免静默选错对象。
    for(USkeletalMeshSocket* Socket:Mesh->GetMeshOnlySocketList())
        if(Socket&&Socket->SocketName==FlashSocketName) { Existing=Socket; ++MatchCount; }
    if(MatchCount>1||(Existing&&Existing->GetOuter()!=Mesh))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SOCKET_REJECT duplicate/non-mesh-owned existing socket")); return false; }
    Mesh->Modify(); // 仅本Mesh事务/标脏；禁止对Skeleton、参考骨或权重调用写接口。
    USkeletalMeshSocket* Socket=Existing?Existing:NewObject<USkeletalMeshSocket>(Mesh,NAME_None,RF_Transactional); // 新Socket严格Outer=当前Mesh。
    Socket->Modify(); Socket->SocketName=FlashSocketName; Socket->BoneName=BoneName;
    Socket->RelativeLocation=Relative.GetTranslation(); Socket->RelativeRotation=Relative.Rotator(); Socket->RelativeScale=Relative.GetScale3D();
    if(!Existing) Mesh->AddSocket(Socket,false); // 官方Editor接口；false明确不复制到Skeleton。
    int32 VerifiedCount=0; // 用真实mesh-only列表读回归属和固定名，不能仅因函数执行过就报创建成功。
    for(const USkeletalMeshSocket* Candidate:Mesh->GetMeshOnlySocketList()) if(Candidate==Socket&&Candidate->SocketName==FlashSocketName&&Candidate->GetOuter()==Mesh) ++VerifiedCount;
    FTransform NativeLocal; int32 NativeBone=INDEX_NONE,NativeIndex=INDEX_NONE; // 原生实际查找输出；Editor不使用非Editor SocketMap缓存。
    const USkeletalMeshSocket* NativeSocket=Mesh->FindSocketInfo(FlashSocketName,NativeLocal,NativeBone,NativeIndex);
    if(VerifiedCount!=1||NativeSocket!=Socket||NativeBone!=BoneIndex||!NativeLocal.Equals(Socket->GetSocketLocalTransform(),.0001)
        ||!(NativeLocal*Reference[BoneIndex]).Equals(ComponentTransform,.001))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SOCKET_REJECT native mesh-only readback failed")); return false; }
    Mesh->MarkPackageDirty(); // 不保存；调用者须单独保护包/保存/新进程再读回。
    UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_FLASH_SOCKET_SET mesh=%s bone=%s component=%s relative=%s saved=0"),*Mesh->GetPathName(),*BoneName.ToString(),*ComponentTransform.ToString(),*Relative.ToString());
    return true;
}

FString UDemoMuzzleFlashAuthoring::ReadSocketFacts(USkeletalMesh* Mesh)
{
    DEMO_LOG_STATIC_CALL(Mesh);
    if(!IsInGameThread()||!IsTrustedWeapon(Mesh))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SOCKET_READ_REJECT thread/whitelist")); return {}; }
    FAssetCompilingManager::Get().FinishAllCompilation(); // 仅借当前稳定参考数据，无动画/材质求值或保存。
    TArray<FTransform> Reference; // 当前真实Mesh参考组件变换，不从旧导出报告回填。
    if(!BuildReference(Mesh,Reference)) return {};
    FString Json; // 只返回内存JSON；调用者若需持久证据可新命名ExportWeaponReference，不覆盖旧报告。
    const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
    if(!FJsonSerializer::Serialize(SocketFactsObject(Mesh,Reference),Writer))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_SOCKET_READ_REJECT serialize")); return {}; }
    return Json;
}

FString UDemoMuzzleFlashAuthoring::SampleFlashFrame(USkeletalMeshComponent* Source,UAnimSequence* Sequence,float PoseTime,UParticleSystemComponent* Flash,float Step)
{
    DEMO_LOG_STATIC_CALL(Source);
    const UWorld* World=IsValid(Source)?Source->GetWorld():nullptr; // 只借本次Editor World，不寻找/启动游戏世界。
    const AActor* SourceOwner=IsValid(Source)?Source->GetOwner():nullptr; // 调用方须自建Transient Actor，拒绝当前Map持久Actor。
    const AActor* FlashOwner=IsValid(Flash)?Flash->GetOwner():nullptr;
    USkeletalMesh* Mesh=IsValid(Source)?Source->GetSkeletalMeshAsset():nullptr; // 必须与真实Sequence同Skeleton，不自动重定向。
    if(!IsInGameThread()||!World||World->WorldType!=EWorldType::Editor||World->HasBegunPlay()
        ||!SourceOwner||!SourceOwner->HasAnyFlags(RF_Transient)||!Source->IsRegistered()||!IsTrustedWeapon(Mesh)
        ||!IsValid(Sequence)||Sequence->GetSkeleton()!=Mesh->GetSkeleton()||!FMath::IsFinite(PoseTime)||PoseTime<0.f||PoseTime>Sequence->GetPlayLength()+KINDA_SMALL_NUMBER
        ||!IsValid(Flash)||Flash->GetWorld()!=World||!FlashOwner||!FlashOwner->HasAnyFlags(RF_Transient)||!Flash->IsRegistered()
        ||Flash->GetAttachParent()!=Source||Flash->GetAttachSocketName()!=FlashSocketName||!Source->DoesSocketExist(FlashSocketName)
        ||!FMath::IsFinite(Step)||Step<0.f||Step>.25f)
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_FRAME_REJECT Editor/transient/mesh/sequence/time/attachment/step")); return {}; }
    Source->SetAnimationMode(EAnimationMode::AnimationSingleNode); // 只改调用方瞬态组件，不改资产/CDO/正式玩家AnimBP。
    Source->SetAnimation(Sequence); Source->Stop(); Source->SetPosition(FMath::Min(PoseTime,Sequence->GetPlayLength()),false);
    Source->TickAnimation(0.f,false); Source->RefreshBoneTransforms(); // 显式机械Pose，不靠BeginPlay、World推进或Notify。
    Source->UpdateComponentToWorld(); Source->UpdateChildTransforms(); // 真实插槽附着链在同一次取样前完成，不手工回填Flash世界位置。
    // 生产PSC默认可被WorldManager批量接管；孤立Editor采样不推进World，直接Tick会被其managed门禁跳过。
    // 仅接管调用者Transient PSC的调度，不Deactivate或改模板/全局CVar；之后由调用者继续显式步进并销毁。
    Flash->SetComponentTickEnabled(false);
    if(Flash->IsTickManaged())
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_FRAME_REJECT explicit transient tick still managed")); return {}; }
    Flash->TickComponent(Step,LEVELTICK_All,nullptr); // 原生无TickFunction走同步Compute/Finalize，不启动World/玩法或批量manager。
    if(!IsValid(Flash)||!Flash->IsRegistered())
    { UE_LOG(LogFPSDemo,Log,TEXT("MUZZLE_FLASH_FRAME_COMPLETE transient PSC naturally completed before sample")); return {}; }
    Flash->MarkRenderDynamicDataDirty(); // 后续真实捕获消费本组件数据；本函数本身不声称像素已渲染。
    const FTransform SocketWorld=Source->GetSocketTransform(FlashSocketName,RTS_World); // 实际同步骨求值后原生组件Socket世界变换。
    const FTransform FlashWorld=Flash->GetComponentTransform(); // 真正PSC变换，不能以Socket目标值替代读回。
    if(SocketWorld.ContainsNaN()||FlashWorld.ContainsNaN())
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_FRAME_REJECT nonfinite actual component/socket")); return {}; }
    TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>(); // 单帧独立值，不保存输入/瞬态对象引用。
    Root->SetStringField(TEXT("state"),TEXT("EDITOR_TRANSIENT_ACTUAL_MECHANICAL_POSE_AND_ATTACHED_PSC"));
    Root->SetStringField(TEXT("mesh"),Mesh->GetPathName()); Root->SetStringField(TEXT("sequence"),Sequence->GetPathName());
    Root->SetNumberField(TEXT("pose_time_seconds"),PoseTime); Root->SetNumberField(TEXT("particle_step_seconds"),Step);
    Root->SetObjectField(TEXT("source_world"),TransformObject(Source->GetComponentTransform()));
    Root->SetObjectField(TEXT("socket_world"),TransformObject(SocketWorld)); Root->SetObjectField(TEXT("psc_world"),TransformObject(FlashWorld));
    Root->SetObjectField(TEXT("psc_relative"),TransformObject(Flash->GetRelativeTransform()));
    Root->SetNumberField(TEXT("origin_error_cm"),FVector::Distance(SocketWorld.GetTranslation(),FlashWorld.GetTranslation()));
    Root->SetNumberField(TEXT("rotation_error_degrees"),FMath::RadiansToDegrees(SocketWorld.GetRotation().AngularDistance(FlashWorld.GetRotation())));
    Root->SetNumberField(TEXT("active_particle_count_engine"),Flash->GetNumActiveParticles());
    Root->SetBoolField(TEXT("psc_active"),Flash->IsActive()); Root->SetBoolField(TEXT("particle_world_positions_available"),false);
    Root->SetStringField(TEXT("tick_mode"),TEXT("Caller-owned transient PSC unregistered from automatic tick management; explicit synchronous component simulation only"));
    Root->SetStringField(TEXT("limitation"),TEXT("Component transforms and engine particle count only; CPU particle centres/local-space modules not sampled; no normal world scheduling, RHI, game camera, input or multiplayer acceptance"));
    FString Json; // JSON只返回给调用者；没有文件或资产写入。
    const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);
    if(!FJsonSerializer::Serialize(Root,Writer))
    { UE_LOG(LogFPSDemo,Warning,TEXT("MUZZLE_FLASH_FRAME_REJECT serialize")); return {}; }
    return Json;
}
