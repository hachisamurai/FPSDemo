// 职责：Editor玩家骨骼动作/薄AnimBP作者工具；Build用于首次制作，Rebuild入口增量保留现有资产配置，调用者负责保存。
#include "Animation/DemoPlayerAnimationAuthoring.h"
#include "Animation/DemoAnatomicalArm.h"
#include "Animation/DemoPlayerAppearance.h"
#include "Animation/DemoPlayerAnimInstance.h"
#include "Animation/DemoPlayerWeaponAnimInstance.h" // 身体与机械薄图共用同一取消混合缓存。
#include "Animation/AnimGraphNode_DemoPlayerGrounding.h" // 最终混合Pose后按真实鞋底修正，不能仅校正源序列。
#include "Animation/AnimGraphNode_DemoPlayerAim.h" // 最终上身瞄准在地面适配之后，原动作及足IK保持各自职责。
#include "Animation/DemoPlayerSoleAuthoring.h" // 从新Body真实LOD0提取完整底面点及蒙皮，支持八边鞋底。
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h" // TP源取实际FP配置Montage片段，接入修正资产后不能仍硬编码旧Sequence。
#include "Animation/DemoFPAnimInstance.h" // 读取实际武器层CDO的只读AnimSet。
#include "Weapons/DemoWeaponBase.h" // 武器CDO决定当前FP层类，保持同一资源配置真值。
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h" // 独立眼球引用只读保留，TP重烘不创建或修改眼部资产。
#include "AnimPose.h"
#include "TwoBoneIK.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_TwoWayBlend.h" // 显式双姿态过渡，手和枪械零件同步归位。
#include "AnimGraphNode_LocalToComponentSpace.h" // 原生骨控制读取最终组件空间Pose。
#include "AnimGraphNode_ComponentToLocalSpace.h" // 控制结束转回标准AnimGraph根输出。
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_VariableGet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UnrealType.h"
#include "Debug/DemoLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace DemoPlayerAuthoring
{
    /** Path为/Game/Player下的受控包路径，返回包拥有的资源；重跑保留对象身份。 */
    template<class T> T* Asset(const FString& Path)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        const FString Name = FPackageName::GetLongPackageAssetName(Path); // 包叶名固定为对象名，避免生成重复后缀资产。
        T* Result = LoadObject<T>(nullptr, *(Path + TEXT(".") + Name)); // 本次同步借用，不跨GC异步持有。
        if (!Result) { Result = NewObject<T>(CreatePackage(*Path), *Name, RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Result); }
        Result->Modify(); return Result;
    }
    /** Graph由当前AnimBP拥有；X/Y是编辑器布局，不参与运行时计算。 */
    template<class T> T* Node(UEdGraph* Graph, int32 X, int32 Y)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        FGraphNodeCreator<T> Creator(*Graph); // Finalize分配引擎真实Pins。
        T* Result = Creator.CreateNode(); // Graph拥有节点，仅借用至本次编译结束。
        Result->NodePosX = X; Result->NodePosY = Y; Creator.Finalize(); return Result;
    }
    /** From/To为同图节点，Output/Input是引擎Pin名；任何类型/连接失败终止构建。 */
    bool Link(UEdGraphNode* From, FName Output, UEdGraphNode* To, FName Input)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        UEdGraphPin* A = From->FindPin(Output); // 节点重构前短期借用。
        UEdGraphPin* B = To->FindPin(Input); // 输入Pin必须经Schema类型检查。
        if (!A || !B || !From->GetSchema()->TryCreateConnection(A,B))
        { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_GRAPH_LINK_FAILED %s -> %s"),*Output.ToString(),*Input.ToString()); return false; }
        return true;
    }
    /** Graph/To是本次图资源，Property读取原生只读缓存，Pin是目标数据输入。 */
    bool Variable(UEdGraph* Graph, FName Property, UEdGraphNode* To, FName Pin, int32 X, int32 Y)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        UK2Node_VariableGet* Value = Node<UK2Node_VariableGet>(Graph,X,Y); // 图拥有，游戏线程缓存值复制给图求值。
        Value->VariableReference.SetSelfMember(Property); Value->ReconstructNode();
        return Link(Value,Property,To,Pin);
    }
    /** Model为固定四枪键；只生成本枪Skeleton的表现图，不创建第二套Gameplay装填状态。 */
    UClass* BuildMechanical(const FString& Model, USkeletalMesh* SuppliedMesh=nullptr, UAnimSequence* SuppliedClip=nullptr)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        USkeletalMesh* Mesh=SuppliedMesh?SuppliedMesh:LoadObject<USkeletalMesh>(nullptr,*(TEXT("/Game/Weapons/Meshes/Breach/Skeletal/SK_Breach_")+Model)); // 参数资产优先；旧制作器仍使用原枪，不改共享Skeleton。
        UAnimSequence* DefaultClip=SuppliedClip?SuppliedClip:LoadObject<UAnimSequence>(nullptr,*(TEXT("/Game/Weapons/Animations/Mechanical/A_Breach_")+Model+TEXT("_Reload_Tactical"))); // 图编译需同枪Skeleton；已验收新资产由调用者明确传入。
        if (!Mesh||!DefaultClip||Mesh->GetSkeleton()!=DefaultClip->GetSkeleton()) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_MECHANICAL_SOURCE_MISSING %s"),*Model); return nullptr; }
        const FString Name=TEXT("ABP_PlayerGun_")+Model; // 四种独立Skeleton分别编译，缓存逻辑共用原生父类。
        const FString Path=TEXT("/Game/Player/Animations/")+Name; // 只写本功能自动生成目录。
        UAnimBlueprint* BP=LoadObject<UAnimBlueprint>(nullptr,*(Path+TEXT(".")+Name)); // 重导复用对象身份。
        if (!BP)
        {
            UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>(); // 本次同步工厂，不保存外部引用。
            Factory->ParentClass=UDemoPlayerWeaponAnimInstance::StaticClass(); Factory->TargetSkeleton=Mesh->GetSkeleton();
            BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*Path),*Name,RF_Public|RF_Standalone,nullptr,GWarn));
            if (!BP) return nullptr;
            FAssetRegistryModule::AssetCreated(BP);
        }
        if (BP->ParentClass!=UDemoPlayerWeaponAnimInstance::StaticClass()) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_MECHANICAL_PARENT_MISMATCH")); return nullptr; }
        BP->Modify(); BP->TargetSkeleton=Mesh->GetSkeleton(); BP->SetPreviewMesh(Mesh);
        UEdGraph* Graph=nullptr; // 工厂拥有的AnimGraph，本次同步构建借用。
        for (UEdGraph* Candidate : BP->FunctionGraphs) if (Candidate->GetFName()==TEXT("AnimGraph")) Graph=Candidate;
        if (!Graph) return nullptr;
        const TArray<UEdGraphNode*> OldNodes=Graph->Nodes; // 删除旧节点时不使遍历数组失效。
        for (UEdGraphNode* Old : OldNodes) Graph->RemoveNode(Old);
        UAnimGraphNode_Root* Root=Node<UAnimGraphNode_Root>(Graph,500,0); // 唯一机械Pose输出。
        UAnimGraphNode_TwoWayBlend* Blend=Node<UAnimGraphNode_TwoWayBlend>(Graph,200,0); // 与身体共用0.12秒过渡权重。
        for (int32 Index=0; Index<2; ++Index) // A为旧冻结姿态，B为当前服务器phase。
        {
            UAnimGraphNode_SequenceEvaluator* Player=Node<UAnimGraphNode_SequenceEvaluator>(Graph,-100,Index*260); // 禁用Notify和RootMotion，只做纯机械求值。
            Player->Node.SetSequence(DefaultClip); Player->Node.SetTeleportToExplicitTime(true);
            for (FOptionalPinFromProperty& Pin : Player->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true;
            Player->ReconstructNode();
            if (!Variable(Graph,Index==0?TEXT("PreviousSequence"):TEXT("CurrentSequence"),Player,TEXT("Sequence"),-400,Index*260) ||
                !Variable(Graph,Index==0?TEXT("PreviousTime"):TEXT("CurrentTime"),Player,TEXT("ExplicitTime"),-400,Index*260+100) ||
                !Link(Player,TEXT("Pose"),Blend,Index==0?TEXT("A"):TEXT("B"))) return nullptr;
        }
        if (!Variable(Graph,TEXT("BlendAlpha"),Blend,TEXT("Alpha"),-100,-150) || !Link(Blend,TEXT("Pose"),Root,TEXT("Result"))) return nullptr;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status==BS_Error||!BP->GeneratedClass) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_MECHANICAL_GRAPH_FAILED %s"),*Model); return nullptr; }
        BP->MarkPackageDirty(); return BP->GeneratedClass;
    }
    /** Model/Action是固定四枪键；从武器实际FP层AnimSet读取完整单段源，不猜测文件名或回退旧动作。 */
    UAnimSequence* SourceSequence(const FString& Model, const FString& Action)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs model=%s action=%s"),__FUNCTION__,*Model,*Action);
        const FString ClassPath=TEXT("/Game/Weapons/Blueprints/BP_Weapon_")+Model+TEXT(".BP_Weapon_")+Model+TEXT("_C"); // 固定内部Model生成只读武器类路径。
        const UClass* WeaponClass=LoadClass<ADemoWeaponBase>(nullptr,*ClassPath); // Blueprint当前配置类，不使用过时导出快照。
        const ADemoWeaponBase* Weapon=WeaponClass?Cast<ADemoWeaponBase>(WeaponClass->GetDefaultObject()):nullptr; // 只读CDO。
        const UDemoWeaponLayerAnimInstance* Layer=Weapon && Weapon->Config.WeaponAnimLayerClass?Weapon->Config.WeaponAnimLayerClass->GetDefaultObject<UDemoWeaponLayerAnimInstance>():nullptr; // 本枪实际配置的FP层。
        if (!Layer) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_TP_CONFIG_SOURCE_MISSING model=%s"),*Model); return nullptr; }
        if (Action==TEXT("Idle")) return Layer->AnimSet.IdlePose;
        const UAnimMontage* Montage=Action==TEXT("Fire")?Layer->AnimSet.FireMontage.Get():Action==TEXT("Reload_Tactical")?Layer->AnimSet.TacticalReload.ArmsMontage.Get():
            Action==TEXT("Reload_Empty")?Layer->AnimSet.EmptyReload.ArmsMontage.Get():Action==TEXT("BoltCycle")?Layer->AnimSet.BoltCycle.ArmsMontage.Get():nullptr; // 动作白名单只选实际AnimSet引用。
        if (!Montage || Montage->SlotAnimTracks.Num()!=1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1)
        { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_TP_CONFIG_MONTAGE_INVALID model=%s action=%s"),*Model,*Action); return nullptr; }
        const FAnimSegment& Segment=Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0]; // 当前项目单段Montage契约，不能把裁剪/倍速当完整源。
        UAnimSequence* Sequence=Cast<UAnimSequence>(Segment.GetAnimReference()); // 资产仍由CDO闭包保活。
        if (!Sequence || !FMath::IsNearlyZero(Segment.StartPos) || !FMath::IsNearlyZero(Segment.AnimStartTime)
            || !FMath::IsNearlyEqual(Segment.AnimEndTime,Sequence->GetPlayLength(),.001f) || !FMath::IsNearlyEqual(Segment.AnimPlayRate,1.f) || Segment.LoopingCount!=1)
        { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_TP_CONFIG_SEGMENT_INVALID model=%s action=%s"),*Model,*Action); return nullptr; }
        return Sequence;
    }
    /** Model为内部四枪键，Preferred以肩中点前/右/上厘米表示；读作者配置，旧配置缺字段时保留传入默认。 */
    bool ReadPreferredAnchor(const FString& Model,FVector& Preferred)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs model=%s"),__FUNCTION__,*Model);
        FString Text; TSharedPtr<FJsonObject> Root; // 同步作者数据，不在运行包中读取JSON。
        const TSharedPtr<FJsonObject>* Anchors=nullptr; const TArray<TSharedPtr<FJsonValue>>* Values=nullptr; // 借用Root中的值，仅本次调用有效。
        const FString Path=FPaths::ProjectDir()/TEXT("Art/Player/Animations/fp_pose_profiles.json");
        if (!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())
        { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_ANCHOR_PROFILE_INVALID %s"),*Path); return false; }
        if (!Root->TryGetObjectField(TEXT("third_person_anchor_cm"),Anchors))
        { UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_ANCHOR_LEGACY_DEFAULT model=%s"),*Model); return true; }
        if (!(*Anchors)->TryGetArrayField(Model,Values)||Values->Num()!=3)
        { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_ANCHOR_PROFILE_MISSING model=%s"),*Model); return false; }
        Preferred=FVector((*Values)[0]->AsNumber(),(*Values)[1]->AsNumber(),(*Values)[2]->AsNumber());
        if (Preferred.ContainsNaN()||Preferred.X<24.f||Preferred.X>44.f||Preferred.Y<0.f||Preferred.Y>24.f||Preferred.Z<-16.f||Preferred.Z>12.f)
        { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_ANCHOR_PROFILE_RANGE model=%s value=%s"),*Model,*Preferred.ToString()); return false; }
        return true;
    }
    /** Body/Reference是真实未改骨姿，Model为四枪键；对所有动作120Hz预检同一整枪锚点的双臂可达域，姿态偏好从JSON配置。 */
    bool ChooseAnchor(USkeletalMesh* Body, const FString& Model, const FAnimPose& Reference, const FTransform& SourceBase, FVector Forward, FVector Right, FTransform& OutAnchor)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs model=%s"),__FUNCTION__,*Model);
        FVector PreferredAnchor(Model==TEXT("Pistol")?40.f:34.f,Model==TEXT("Pistol")?16.f:Model==TEXT("Sniper")?22.f:14.f,Model==TEXT("Pistol")?-4.f:8.f); // 旧配置兼容值；新作者配置可单枪调整。
        if (!ReadPreferredAnchor(Model,PreferredAnchor)) return false;
        const float InwardYaw=Model==TEXT("Pistol")?0.f:-30.f; // 监督全轨迹候选将长枪内收30度并抬到肩位，避开旧枪托穿胸/上臂；末尾上身反转恢复真实枪管前向。
        const FQuat Rotation=FQuat(FVector::UpVector,FMath::DegreesToRadians(InwardYaw))*FRotationMatrix::MakeFromXZ(Forward,FVector::UpVector).ToQuat(); // 同一刚体旋转作用于整枪/双腕，五指接触不改变。
        const FVector ShoulderL=UAnimPoseExtensions::GetBonePose(Reference,TEXT("upperarm_l"),EAnimPoseSpaces::World).GetLocation(); // 正常肩位是固定约束，不移动肩适配目标。
        const FVector ShoulderR=UAnimPoseExtensions::GetBonePose(Reference,TEXT("upperarm_r"),EAnimPoseSpaces::World).GetLocation(); // 右侧固定约束。
        const FVector ElbowL=UAnimPoseExtensions::GetBonePose(Reference,TEXT("lowerarm_l"),EAnimPoseSpaces::World).GetLocation(); // 原骨长测量点。
        const FVector ElbowR=UAnimPoseExtensions::GetBonePose(Reference,TEXT("lowerarm_r"),EAnimPoseSpaces::World).GetLocation(); // 原骨长测量点。
        const float ReachL=FVector::Dist(ShoulderL,ElbowL)+FVector::Dist(ElbowL,UAnimPoseExtensions::GetBonePose(Reference,TEXT("hand_l"),EAnimPoseSpaces::World).GetLocation())-1.f; // 留1cm非完全拉直余量。
        const float ReachR=FVector::Dist(ShoulderR,ElbowR)+FVector::Dist(ElbowR,UAnimPoseExtensions::GetBonePose(Reference,TEXT("hand_r"),EAnimPoseSpaces::World).GetLocation())-1.f; // 保持真实臂长，不放松失败阈值。
        TArray<FVector> OffsetsL,OffsetsR; // 所有源帧转到身体朝向后的手腕相对整枪锚点值。
        TArray<FString> Actions={TEXT("Idle"),TEXT("Reload_Tactical"),TEXT("Reload_Empty")}; // 同枪必须共用同一基准锚点，不能每动作随意挪枪。
        if (Model==TEXT("Sniper")) Actions.Add(TEXT("BoltCycle"));
        else Actions.Add(TEXT("Fire")); // 普通短后坐也必须包含在同枪锚点可达域中，不能导入后临时挪枪或伸腕。
        FAnimPoseEvaluationOptions Options; Options.OptionalSkeletalMesh=Body; // 同Skeleton离线采样，完全只读。
        for (const FString& Action : Actions) // 完整所有帧，不只用Idle单帧猜测装填可达性。
        {
            UAnimSequence* Source=SourceSequence(Model,Action); // 读取已配置的新旧FP源；只改变来源，不改变父Agent维护的锚点算法。
            if (!Source) return false;
            const int32 Frames=FMath::Max(1,FMath::RoundToInt(Source->GetPlayLength()*120.f)); // 与修正源和实际TP烘焙120Hz一致，保留子帧指部避让。
            for (int32 Frame=0; Frame<=Frames; ++Frame)
            {
                FAnimPose Pose; // 单帧求值副本，用后即可丢弃。
                UAnimPoseExtensions::GetAnimPoseAtTime(Source,Source->GetPlayLength()*Frame/Frames,Options,Pose);
                if (!Pose.IsValid()) return false;
                OffsetsL.Add(Rotation.RotateVector(UAnimPoseExtensions::GetBonePose(Pose,TEXT("hand_l"),EAnimPoseSpaces::World).GetRelativeTransform(SourceBase).GetLocation()));
                OffsetsR.Add(Rotation.RotateVector(UAnimPoseExtensions::GetBonePose(Pose,TEXT("hand_r"),EAnimPoseSpaces::World).GetRelativeTransform(SourceBase).GetLocation()));
            }
        }
        const FVector Mid=(ShoulderL+ShoulderR)*.5f; // 身体正常肩中点，不使用FP上身形变。
        float BestCost=TNumericLimits<float>::Max(); // 候选偏好来自实际胸颈几何诊断；所有源帧仍必须满足原臂长，不用外观偏好覆盖可达失败。
        FVector Best=FVector::ZeroVector; // 只有整套动作满足两个球域后才发布候选。
        for (int32 Fwd=24;Fwd<=44;Fwd+=2) // 有界厘米候选，完整轨迹必须可达，不能通过移动肩根满足握点。
            for (int32 Side=0;Side<=24;Side+=2)
                for (int32 Height=-16;Height<=12;Height+=2)
                {
                    const FVector Candidate=Mid+Forward*Fwd+Right*Side+FVector::UpVector*Height; // 待验证整枪平移，旋转/腕接触保持固定。
                    bool bReachable=true; // 任意一帧不可达就淘汰，不能仅报告平均误差。
                    for (int32 Index=0; Index<OffsetsL.Num(); ++Index)
                        if (FVector::DistSquared(Candidate+OffsetsL[Index],ShoulderL)>FMath::Square(ReachL) || FVector::DistSquared(Candidate+OffsetsR[Index],ShoulderR)>FMath::Square(ReachR)) { bReachable=false; break; }
                    const float Cost=2.f*FMath::Square(Fwd-PreferredAnchor.X)+FMath::Square(Side-PreferredAnchor.Y)+.5f*FMath::Square(Height-PreferredAnchor.Z); // 可达只是一道门槛，选中值仍需枪托/胸臂碰撞与实际画面复验。
                    if (bReachable&&Cost<BestCost) { BestCost=Cost; Best=Candidate; }
                }
        if (BestCost==TNumericLimits<float>::Max()) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_TP_NO_COMMON_REACH model=%s samples=%d"),*Model,OffsetsL.Num()); return false; }
        OutAnchor=FTransform(Rotation,Best);
        UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_TP_ANCHOR model=%s allActionSamples=%d anchor=%s reachL=%.2f reachR=%.2f"),*Model,OffsetsL.Num(),*Best.ToString(),ReachL,ReachR);
        return true;
    }
    /** Pose为新帧副本，Side区分左右；Target/Pole为组件空间厘米，Rotation为手腕方向；不拉伸骨长。 */
    bool ArmIK(FAnimPose& Pose, const TCHAR* Side, FVector Target, FQuat Rotation, FVector Pole)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        const FName Upper(*FString::Printf(TEXT("upperarm_%s"),Side)); // 当前完整三骨链名称。
        const FName Lower(*FString::Printf(TEXT("lowerarm_%s"),Side)); // 肘关节。
        const FName Hand(*FString::Printf(TEXT("hand_%s"),Side)); // 手腕目标，指骨保持源局部握姿。
        FTransform A = UAnimPoseExtensions::GetBonePose(Pose,Upper,EAnimPoseSpaces::World); // IK修改的肩/肘/腕值副本。
        FTransform B = UAnimPoseExtensions::GetBonePose(Pose,Lower,EAnimPoseSpaces::World);
        FTransform C = UAnimPoseExtensions::GetBonePose(Pose,Hand,EAnimPoseSpaces::World);
        const FName Middle(*FString::Printf(TEXT("middle_01_%s"),Side)); // 腕到中指根的掌纵向，避免依赖FBX骨轴方向。
        const FVector PalmLocal=C.InverseTransformVectorNoScale(UAnimPoseExtensions::GetBonePose(Pose,Middle,EAnimPoseSpaces::World).GetLocation()-C.GetLocation());
        const FVector AnatomicalPole=DemoAnatomicalArm::ChoosePole(A,B,C,Target,Rotation,PalmLocal,Pole); // 肩腕固定、原骨长固定，只选择较自然的肘平面。
        AnimationCore::SolveTwoBoneIK(A,B,C,AnatomicalPole,Target,false,1.f,1.f);
        if (FVector::Dist(C.GetLocation(),Target) > .35f)
        { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_TP_IK_UNREACHABLE side=%s error=%.3f target=%s shoulder=%s"),Side,FVector::Dist(C.GetLocation(),Target),*Target.ToString(),*A.GetLocation().ToString()); return false; }
        C.SetRotation(Rotation);
        UAnimPoseExtensions::SetBonePose(Pose,A,Upper,EAnimPoseSpaces::World);
        UAnimPoseExtensions::SetBonePose(Pose,B,Lower,EAnimPoseSpaces::World);
        UAnimPoseExtensions::SetBonePose(Pose,C,Hand,EAnimPoseSpaces::World);
        return true;
    }
    /** Reference为原Skeleton参考姿势，Shoulder/Forward/Right为组件空间厘米/单位轴；返回掌指向下、拇指侧向前的自然左腕，不复用FP隐藏手姿。 */
    FTransform RelaxedPistolWrist(const FAnimPose& Reference, const FVector& Shoulder, const FVector& Forward, const FVector& Right)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        FTransform Wrist=UAnimPoseExtensions::GetBonePose(Reference,TEXT("hand_l"),EAnimPoseSpaces::World); // 原腕旋转只作为语义轴基准，保持原单位缩放。
        const FVector LongAxis=(UAnimPoseExtensions::GetBonePose(Reference,TEXT("middle_01_l"),EAnimPoseSpaces::World).GetLocation()-Wrist.GetLocation()).GetSafeNormal(); // 手腕至中指根定义掌纵轴，不猜手骨本地朝向。
        const FVector SideAxis=(UAnimPoseExtensions::GetBonePose(Reference,TEXT("index_01_l"),EAnimPoseSpaces::World).GetLocation()-UAnimPoseExtensions::GetBonePose(Reference,TEXT("pinky_01_l"),EAnimPoseSpaces::World).GetLocation()).GetSafeNormal(); // 小指侧至食指侧消除单轴对齐的任意翻掌。
        const FQuat ReferenceBasis=FRotationMatrix::MakeFromXY(LongAxis,SideAxis).ToQuat(); // 两轴正交化构成原掌平面。
        const FQuat GoalBasis=FRotationMatrix::MakeFromXY((Forward*.08f-FVector::UpVector).GetSafeNormal(),(Forward+FVector::UpVector*.08f).GetSafeNormal()).ToQuat(); // 手指向下略前、拇指侧向前，掌面自然朝腿侧。
        Wrist.SetLocation(Shoulder-Right*12.f+Forward*6.f-FVector::UpVector*49.f); // 原臂长约55cm，肩腕距50.8cm保留微屈；实际作者网格衣裤净距约3.31cm。
        Wrist.SetRotation((GoalBasis*ReferenceBasis.Inverse()*Wrist.GetRotation()).GetNormalized());
        return Wrist;
    }
    /** Action为固定手枪动作键，Phase为0..1真实源进度；仅装填抓匣/机械操作中段保留原左手接触，首尾平滑回自然下垂。 */
    float PistolLeftContactWeight(const FString& Action, float Phase)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        if (Action==TEXT("Reload_Tactical")) return FMath::SmoothStep(0.f,.10f,Phase)*(1.f-FMath::SmoothStep(.72f,1.f,Phase)); // 原seat=.68，留.04余量再收手，不提前离开弹匣。
        if (Action==TEXT("Reload_Empty")) return FMath::SmoothStep(0.f,.08f,Phase)*(1.f-FMath::SmoothStep(.84f,1.f,Phase)); // 套筒阶段完成后才收手，原机械时长不变。
        return 0.f; // 持枪和开火均单手；FP隐藏左手的悬空位置不能成为TP可见姿势。
    }
    /** Body为同Skeleton新模型，Model/Action为固定四枪动作键；源FP资产只读，手枪自然单手，装填中段保留源接触。 */
    UAnimSequence* BakeUpper(USkeletalMesh* Body, const FString& Model, const FString& Action)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
        UAnimSequence* Source = SourceSequence(Model,Action); // 新手指修正必须沿当前FP配置进入TP；原资源保持只读。
        UAnimSequence* Idle = SourceSequence(Model,TEXT("Idle")); // 剔除同一实际FP层整枪构图的基准锚点。
        if (!Source || !Idle || Source->GetSkeleton()!=Body->GetSkeleton())
        { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_TP_SOURCE_MISSING model=%s action=%s"),*Model,*Action); return nullptr; }
        FAnimPoseEvaluationOptions Options; // 完整实际参考骨，不使用重定向猜测或修改骨长。
        Options.OptionalSkeletalMesh = Body;
        FAnimPose Reference; // 正常全身参考姿势，不复用FP肩膀/脊柱旋转。
        UAnimPoseExtensions::GetReferencePose(Body->GetSkeleton(),Reference);
        FAnimPose IdlePose; // 只读取FP枪锚点，不把此姿势作为全身基础。
        UAnimPoseExtensions::GetAnimPoseAtTime(Idle,0,Options,IdlePose);
        if (!Reference.IsValid() || !IdlePose.IsValid()) return nullptr;
        const FTransform SourceBaseAnchor = UAnimPoseExtensions::GetBonePose(IdlePose,TEXT("ik_hand_gun"),EAnimPoseSpaces::World); // FP构图原点。
        const FVector ShoulderL = UAnimPoseExtensions::GetBonePose(Reference,TEXT("upperarm_l"),EAnimPoseSpaces::World).GetLocation(); // 左侧正常肩位。
        const FVector ShoulderR = UAnimPoseExtensions::GetBonePose(Reference,TEXT("upperarm_r"),EAnimPoseSpaces::World).GetLocation(); // 右侧正常肩位。
        const FVector Right = (ShoulderR-ShoulderL).GetSafeNormal2D(); // 由真实肩宽推导方向，不硬编码Blender轴。
        const FVector Forward = FVector::CrossProduct(Right,FVector::UpVector).GetSafeNormal(); // 当前原骨肩链Right为-X，Cross(Right,+Z)得到角色参考前向+Y。
        const bool bPistol=Model==TEXT("Pistol"); // 只改第三人称手枪左臂，其他枪、真实右手握点与FP序列不变。
        const FTransform RelaxedWrist=bPistol?RelaxedPistolWrist(Reference,ShoulderL,Forward,Right):FTransform::Identity; // 每次烘焙固定自然姿势，不累积逐帧旋转误差。
        const FVector RelaxedPole=ShoulderL-Right*20.f+Forward*4.f-FVector::UpVector*25.f; // 自然左肘略向外、前方微屈；同腕目标保持原链长。
        FTransform BaseAnchor; // 同枪所有源动作共同可达的整枪锚点，绝不只调某帧腕点或伸长胳膊。
        if (!ChooseAnchor(Body,Model,Reference,SourceBaseAnchor,Forward,Right,BaseAnchor)) return nullptr;
        const FReferenceSkeleton& Ref = Body->GetRefSkeleton(); // 新网格骨链，只用于识别手及其后代。
        TArray<FName> Bones; // 全骨轨道保留参考变换，方便任意工具离线单独检查TP。
        UAnimPoseExtensions::GetBoneNames(Reference,Bones);
        TArray<FName> HandDescendants; // 手腕以下局部动画直接复用，保留原五指握点。
        TSet<FName> LeftHandDescendants; // 仅左手指节随自然姿势/装填混合，不误改右手扳机和握柄。
        for (int32 Index=0; Index<Ref.GetNum(); ++Index) // 每骨从父链查询一次，不在每帧重复搜索。
        {
            int32 Parent = Index; // 当前检查到的祖先；不修改RefSkeleton。
            while (Parent != INDEX_NONE)
            {
                if (Ref.GetBoneName(Parent)==TEXT("hand_l") || Ref.GetBoneName(Parent)==TEXT("hand_r"))
                { if (Parent != Index) { HandDescendants.Add(Ref.GetBoneName(Index)); if (Ref.GetBoneName(Parent)==TEXT("hand_l")) LeftHandDescendants.Add(Ref.GetBoneName(Index)); } break; } // 父链归属决定左右，不按名字后缀误判独立IK骨。
                Parent = Ref.GetParentIndex(Parent);
            }
        }
        UAnimSequence* Result = Asset<UAnimSequence>(TEXT("/Game/Player/Animations/Upper/A_TP_")+Model+TEXT("_")+Action); // 只写新TP目录。
        Result->SetSkeleton(Body->GetSkeleton()); Result->SetPreviewMesh(Body);
        IAnimationDataController& Controller = Result->GetController(); // 正式Editor写入接口使派生压缩失效。
        const int32 FrameCount = FMath::Max(1,FMath::RoundToInt(Source->GetPlayLength()*120.f)); // 120Hz离线烘焙，保持高频手指避让和同一原动作秒数。
        Controller.OpenBracket(FText::FromString(TEXT("Bake human third person upper body")),false);
        Controller.InitializeModel(); Controller.SetFrameRate(FFrameRate(120,1),false); Controller.SetNumberOfFrames(FFrameNumber(FrameCount),false); Controller.RemoveAllBoneTracks(false); // 曲线采样率与可达性预检一致。
        TArray<TArray<FVector3f>> Positions, Scales; // [骨][帧]本地变换临时缓存。
        TArray<TArray<FQuat4f>> Rotations; // 归一化本地四元数，禁止缩骨或位置拉伸。
        Positions.SetNum(Bones.Num()); Scales.SetNum(Bones.Num()); Rotations.SetNum(Bones.Num());
        for (int32 Frame=0; Frame<=FrameCount; ++Frame) // 包含首尾，装填和拉栓都可精准Seek。
        {
            FAnimPose SourcePose; // 当前源帧只读，不修改共享动画。
            UAnimPoseExtensions::GetAnimPoseAtTime(Source,Source->GetPlayLength()*Frame/FrameCount,Options,SourcePose);
            FAnimPose Pose = Reference; // 每帧从正常骨架开始，无累计误差。
            if (!SourcePose.IsValid()) { Controller.CloseBracket(false); return nullptr; }
            const float LeftContact=bPistol?PistolLeftContactWeight(Action,static_cast<float>(Frame)/FrameCount):1.f; // 源进度不改变武器/GAS时钟；动作端点导数为0。
            for (FName Bone : HandDescendants) // 右手/长枪完全保留原指曲线；手枪自然左手用参考指姿，装填中段恢复完整源接触。
            {
                FTransform Finger=UAnimPoseExtensions::GetBonePose(SourcePose,Bone,EAnimPoseSpaces::Local); // 当前指节值副本，不写源动画。
                if (bPistol && LeftHandDescendants.Contains(Bone)) Finger.SetRotation(FQuat::Slerp(UAnimPoseExtensions::GetBonePose(Reference,Bone,EAnimPoseSpaces::Local).GetRotation(),Finger.GetRotation(),LeftContact).GetNormalized());
                UAnimPoseExtensions::SetBonePose(Pose,Finger,Bone,EAnimPoseSpaces::Local);
            }
            const FTransform SourceAnchor = UAnimPoseExtensions::GetBonePose(SourcePose,TEXT("ik_hand_gun"),EAnimPoseSpaces::World); // 源动作中整枪倾斜/移动。
            const FTransform Anchor = SourceAnchor.GetRelativeTransform(SourceBaseAnchor)*BaseAnchor; // 同一整枪动作移到正常身体前方。
            UAnimPoseExtensions::SetBonePose(Pose,Anchor,TEXT("ik_hand_gun"),EAnimPoseSpaces::World);
            for (const TCHAR* Side : {TEXT("l"),TEXT("r")}) // TP两手始终可见；手枪左臂根据阶段在自然下垂和真实装填之间求解。
            {
                const FName Hand(*FString::Printf(TEXT("hand_%s"),Side)); // 源腕骨和IK腕骨名称。
                FTransform Wrist = UAnimPoseExtensions::GetBonePose(SourcePose,Hand,EAnimPoseSpaces::World).GetRelativeTransform(SourceAnchor)*Anchor; // 默认保存手相对枪完整接触，只有手枪左手首尾替换。
                const bool bLeft = Side[0]==TEXT('l'); // 左右肘向各自身体外侧下沉，避开胸腔和枪托。
                FVector Pole = (bLeft?ShoulderL:ShoulderR) + Right*(bLeft?-42.f:42.f) + Forward*(bPistol&&bLeft?32.f:12.f) - FVector::UpVector*32.f; // 装填中段沿已验前肘轨迹，长枪完全保留原值。
                if (bPistol && bLeft)
                {
                    const FVector Arc=(-Right*12.f+Forward*14.f)*(4.f*LeftContact*(1.f-LeftContact)); // 抬/收手走身体外前弧，端点为0，避免从大腿侧直线穿过衣身。
                    Wrist.SetLocation(FMath::Lerp(RelaxedWrist.GetLocation(),Wrist.GetLocation(),LeftContact)+Arc);
                    Wrist.SetRotation(FQuat::Slerp(RelaxedWrist.GetRotation(),Wrist.GetRotation(),LeftContact).GetNormalized());
                    Pole=FMath::Lerp(RelaxedPole,Pole,LeftContact);
                }
                if (!ArmIK(Pose,Side,Wrist.GetLocation(),Wrist.GetRotation(),Pole))
                { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_TP_IK_FRAME model=%s action=%s frame=%d/%d side=%s"),*Model,*Action,Frame,FrameCount,Side); Controller.CloseBracket(false); return nullptr; }
                UAnimPoseExtensions::SetBonePose(Pose,UAnimPoseExtensions::GetBonePose(Pose,Hand,EAnimPoseSpaces::World),FName(*FString::Printf(TEXT("ik_hand_%s"),Side)),EAnimPoseSpaces::World);
            }
            if (Model!=TEXT("Pistol")) // 正常人类侧身持长枪：肩胸旋转而腿保持前向，整枪恢复与实际射线相同的+Y方向。
            {
                const FName SpineName(TEXT("spine_01")); // 腰上最低脊骨，绕自身旋转不改变与pelvis的本地骨长。
                const FTransform SpinePose=UAnimPoseExtensions::GetBonePose(Pose,SpineName,EAnimPoseSpaces::World); // 整个上身子树的共同刚体基准。
                const FQuat Turn(FVector::UpVector,FMath::DegreesToRadians(30.f)); // 抵消ChooseAnchor长枪的-30度内收；刚体一致应用于胸肩/手/枪以保留接触。
                const FVector Pivot=SpinePose.GetLocation(); // 旋转枢轴取实际脊骨原点，不把枪绕世界零点转出身体。
                const FTransform CounterTurn(Turn,Pivot-Turn.RotateVector(Pivot)); // UE右乘依次应用，完全保持枪-手/肩-肘距离。
                UAnimPoseExtensions::SetBonePose(Pose,SpinePose*CounterTurn,SpineName,EAnimPoseSpaces::World);
                UAnimPoseExtensions::SetBonePose(Pose,Anchor*CounterTurn,TEXT("ik_hand_gun"),EAnimPoseSpaces::World);
                for (const TCHAR* Side : {TEXT("l"),TEXT("r")}) // 独立IK支系不随spine旋转，重新复制实际手腕保持工具/机械锚点一致。
                    UAnimPoseExtensions::SetBonePose(Pose,UAnimPoseExtensions::GetBonePose(Pose,FName(*FString::Printf(TEXT("hand_%s"),Side)),EAnimPoseSpaces::World),FName(*FString::Printf(TEXT("ik_hand_%s"),Side)),EAnimPoseSpaces::World);
                FTransform HeadPose=UAnimPoseExtensions::GetBonePose(Pose,TEXT("head"),EAnimPoseSpaces::World); // 头在自身颈端反向转回，视线与枪管一致，不移动颈链。
                HeadPose.SetRotation(Turn.Inverse()*HeadPose.GetRotation());
                UAnimPoseExtensions::SetBonePose(Pose,HeadPose,TEXT("head"),EAnimPoseSpaces::World);
            }
            for (int32 Index=0; Index<Bones.Num(); ++Index) // 保存完整本地轨道，骨平移和缩放保持参考，不伪装成功。
            {
                FTransform Local = UAnimPoseExtensions::GetBonePose(Pose,Bones[Index],EAnimPoseSpaces::Local); // 当帧值副本。
                Local.NormalizeRotation(); Positions[Index].Add(FVector3f(Local.GetLocation())); Rotations[Index].Add(FQuat4f(Local.GetRotation())); Scales[Index].Add(FVector3f(Local.GetScale3D()));
            }
        }
        for (int32 Index=0; Index<Bones.Num(); ++Index) // Controller管理可编辑数据与压缩重建。
            if (!Controller.AddBoneCurve(Bones[Index],false) || !Controller.SetBoneTrackKeys(Bones[Index],Positions[Index],Rotations[Index],Scales[Index],false))
            { Controller.CloseBracket(false); UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_TP_TRACK_FAILED %s"),*Bones[Index].ToString()); return nullptr; }
        Controller.NotifyPopulated(); Controller.CloseBracket(false); Result->PostEditChange(); Result->MarkPackageDirty();
        UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_TP_BAKED model=%s action=%s frames=%d"),*Model,*Action,FrameCount); return Result;
    }
}

bool UDemoPlayerAnimationAuthoring::RebuildFirearmPoses(USkeletalMesh* Body)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__); // 静态Editor接口无this，不保存调用者对象。
    if (!Body || !Body->GetSkeleton())
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_FIREARM_REBAKE_REJECT missing body or skeleton")); return false; }
    for (const FString Model : {TEXT("Pistol"),TEXT("Rifle"),TEXT("Shotgun"),TEXT("Sniper")})
    {
        const TArray<FString> Actions={TEXT("Idle"),TEXT("Reload_Tactical"),TEXT("Reload_Empty"),Model==TEXT("Sniper")?TEXT("BoltCycle"):TEXT("Fire")}; // 只修改原四枪配套动作，不涉及移动/近战。
        for (const FString& Action : Actions)
            if (!DemoPlayerAuthoring::BakeUpper(Body,Model,Action))
            { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_FIREARM_REBAKE_FAILED model=%s action=%s"),*Model,*Action); return false; }
    }
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_FIREARM_POSES_REBAKED weapons=4; caller must save explicit sequences"));
    return true;
}

bool UDemoPlayerAnimationAuthoring::RebuildPistolSingleHand(USkeletalMesh* Body)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    const UDemoPlayerAppearance* Appearance=LoadObject<UDemoPlayerAppearance>(nullptr,TEXT("/Game/Player/DA_PlayerAppearance.DA_PlayerAppearance")); // 只借用现有配置，不重建或修改DA。
    const FDemoPlayerWeaponPoses* Poses=Appearance?Appearance->WeaponPoses.Find(TEXT("pistol")):nullptr; // 四个目标必须仍是正式已装配的TP引用。
    if (!Body || !Appearance || Appearance->BodyMesh!=Body || !Poses) { UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_SINGLE_HAND invalid current appearance/body")); return false; }
    const TArray<FString> Actions={TEXT("Idle"),TEXT("Fire"),TEXT("Reload_Tactical"),TEXT("Reload_Empty")}; // 只允许四个手枪TP序列，不重烘其他枪和移动。
    const UAnimSequence* Sequences[]={Poses->Hold.Get(),Poses->Fire.Get(),Poses->Reload.Get(),Poses->EmptyReload.Get()}; // 当前DA强引用的目标序列，保存包由外部白名单导入器负责。
    for (int32 Index=0;Index<Actions.Num();++Index) // 全部预检通过后才开始作者修改，避免对替换过的用户引用写错资产。
        if (!Sequences[Index] || Sequences[Index]->GetPathName()!=TEXT("/Game/Player/Animations/Upper/A_TP_Pistol_")+Actions[Index]+TEXT(".A_TP_Pistol_")+Actions[Index])
        { UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_SINGLE_HAND unexpected configured sequence action=%s"),*Actions[Index]); return false; }
    for (const FString& Action:Actions) if (!DemoPlayerAuthoring::BakeUpper(Body,TEXT("Pistol"),Action)) return false; // 任意IK/源失败保留错误，不保存部分结果。
    UE_LOG(LogFPSDemo,Display,TEXT("PISTOL_SINGLE_HAND_BAKED sequences=4"));
    return true;
}

bool UDemoPlayerAnimationAuthoring::RefreshModelGrounding(USkeletalMesh* Body)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    UDemoPlayerAppearance* Appearance=LoadObject<UDemoPlayerAppearance>(nullptr,TEXT("/Game/Player/DA_PlayerAppearance.DA_PlayerAppearance")); // 只借用现有配置，不创建空白替代。
    UAnimBlueprint* Blueprint=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/Player/Animations/ABP_PlayerFullBody.ABP_PlayerFullBody")); // 保留用户现有图结构和动作引用。
    if (!Body || !Appearance || !Blueprint || Body->GetSkeleton()!=Blueprint->TargetSkeleton)
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_MODEL_GROUNDING_INVALID_INPUT")); return false; }
    TArray<FDemoPlayerSolePoint> Left,Right; // 先验证新网格，成功后再改内存资产。
    if (!DemoPlayerSoleAuthoring::Extract(Body,Left,Right)) return false;
    UAnimGraphNode_DemoPlayerGrounding* Ground=nullptr; // 只允许唯一既有贴地节点，图变化时拒绝猜测。
    for (UEdGraph* Graph:Blueprint->FunctionGraphs)
        for (UEdGraphNode* Node:Graph->Nodes)
            if (UAnimGraphNode_DemoPlayerGrounding* Candidate=Cast<UAnimGraphNode_DemoPlayerGrounding>(Node))
            {
                if (Ground) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_MODEL_MULTIPLE_GROUNDING_NODES")); return false; }
                Ground=Candidate;
            }
    if (!Ground) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_MODEL_NO_GROUNDING_NODE")); return false; }
    Blueprint->Modify(); Appearance->Modify(); Ground->Modify();
    Ground->Node.LeftSole=Left; Ground->Node.RightSole=Right;
    Appearance->LeftSole=Left; Appearance->RightSole=Right;
    Blueprint->SetPreviewMesh(Body);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status==BS_Error || !Blueprint->GeneratedClass)
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_MODEL_GROUNDING_COMPILE_FAILED")); return false; }
    Blueprint->MarkPackageDirty(); Appearance->MarkPackageDirty();
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_MODEL_GROUNDING_UPDATED mesh=%s left=%d right=%d"),*Body->GetName(),Left.Num(),Right.Num());
    return true;
}

bool UDemoPlayerAnimationAuthoring::Build(USkeletalMesh* Body, USkeletalMesh* Arms, const TMap<FName,UAnimSequence*>& Clips)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    const TArray<FName> Names = {TEXT("Idle"),TEXT("Walk"),TEXT("Run"),TEXT("JumpStart"),TEXT("JumpLoop"),TEXT("JumpLand"),TEXT("Sprint"),TEXT("StrafeLeft"),TEXT("StrafeRight")}; // 保留0..6，侧向两态追加，不重排原生编号。
    if (!Body || !Arms || Body->GetSkeleton()!=Arms->GetSkeleton())
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_BUILD_INVALID shared skeleton required")); return false; }
    for (FName Name : Names) // 必须9项完整，不能用Idle冒充没有生成的运动动作。
        if (!Clips.Contains(Name) || !Clips.FindChecked(Name) || Clips.FindChecked(Name)->GetSkeleton()!=Body->GetSkeleton())
        { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_CLIP_MISSING %s"),*Name.ToString()); return false; }
    UDemoPlayerAppearance* Appearance = DemoPlayerAuthoring::Asset<UDemoPlayerAppearance>(TEXT("/Game/Player/DA_PlayerAppearance")); // 闭包保活所有角色资产。
    const TObjectPtr<UStaticMesh> EyeMeshBefore=Appearance->EyeMesh; // Asset复用既有DA；模型/TP作者不得清除独立眼部导入配置。
    const FTransform LeftEyeBefore=Appearance->LeftEyeRest, RightEyeBefore=Appearance->RightEyeRest; // head局部厘米/旋转的值快照，不能随重编借用代理。
    const FVector EyeLimitsBefore(Appearance->EyeMaxYaw,Appearance->EyeMaxPitch,Appearance->EyeFollowSpeed); // 上限度/速度1秒的保护快照，首次尚未导入眼球时同样保持默认值。
    Appearance->BodyMesh=Body; Appearance->ArmsMesh=Arms; Appearance->MovementClips.Reset(); Appearance->WeaponPoses.Reset();
    Appearance->BodyOffset=FTransform(FRotator(0,-90,0),FVector(0,0,-99)); // 重建旧DA也写入Probe02实测-3cm接地校准；原骨/胶囊均保持不变。
    if (!DemoPlayerSoleAuthoring::Extract(Body,Appearance->LeftSole,Appearance->RightSole)) return false; // 鞋型/权重错误使作者失败，不用猜测foot骨高度掩盖。
    FAnimPose Reference; // 同Skeleton参考骨盆给展示枪补偿步态bob。
    UAnimPoseExtensions::GetReferencePose(Body->GetSkeleton(),Reference);
    if (!Reference.IsValid()) return false;
    Appearance->ReferencePelvis=UAnimPoseExtensions::GetBonePose(Reference,TEXT("pelvis"),EAnimPoseSpaces::World);
    for (FName Name : Names) Appearance->MovementClips.Add(Clips.FindChecked(Name)); // 显式引用保证Cook收集。
    for (const FString Model : {TEXT("Pistol"),TEXT("Rifle"),TEXT("Shotgun"),TEXT("Sniper")}) // 每枪独立持枪/两装填；普通枪加短后坐，狙击加含后坐的拉栓。
    {
        FDemoPlayerWeaponPoses Poses; // 本枪数据值在烘焙成功后才写入Appearance。
        Poses.Hold=DemoPlayerAuthoring::BakeUpper(Body,Model,TEXT("Idle"));
        if (Model!=TEXT("Sniper")) Poses.Fire=DemoPlayerAuthoring::BakeUpper(Body,Model,TEXT("Fire")); // 不改变原FP短Montage及其源序列。
        Poses.Reload=DemoPlayerAuthoring::BakeUpper(Body,Model,TEXT("Reload_Tactical"));
        Poses.EmptyReload=DemoPlayerAuthoring::BakeUpper(Body,Model,TEXT("Reload_Empty"));
        if (Model==TEXT("Sniper")) Poses.Bolt=DemoPlayerAuthoring::BakeUpper(Body,Model,TEXT("BoltCycle"));
        Poses.MechanicalAnimClass=DemoPlayerAuthoring::BuildMechanical(Model);
        if (!Poses.Hold || !Poses.Reload || !Poses.EmptyReload || !Poses.MechanicalAnimClass || (Model==TEXT("Sniper")?!Poses.Bolt:!Poses.Fire)) return false;
        Appearance->WeaponPoses.Add(FName(*Model.ToLower()),Poses);
    }
    const FString Path=TEXT("/Game/Player/Animations/ABP_PlayerFullBody"); // 专用自动生成AnimBP，设计师修改前复制资源。
    UAnimBlueprint* BP=LoadObject<UAnimBlueprint>(nullptr,*(Path+TEXT(".ABP_PlayerFullBody"))); // 重跑保留同资产身份。
    if (!BP)
    {
        UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>(); // 同步工厂，无长寿命外部引用。
        Factory->ParentClass=UDemoPlayerAnimInstance::StaticClass(); Factory->TargetSkeleton=Body->GetSkeleton();
        BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),CreatePackage(*Path),TEXT("ABP_PlayerFullBody"),RF_Public|RF_Standalone,nullptr,GWarn));
        if (!BP) return false;
        FAssetRegistryModule::AssetCreated(BP);
    }
    if (BP->ParentClass!=UDemoPlayerAnimInstance::StaticClass()) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_BP_PARENT_MISMATCH")); return false; }
    BP->Modify(); BP->TargetSkeleton=Body->GetSkeleton(); BP->SetPreviewMesh(Body);
    UEdGraph* Graph=nullptr; // 工厂生成的AnimGraph同步借用。
    for (UEdGraph* Candidate : BP->FunctionGraphs) if (Candidate->GetFName()==TEXT("AnimGraph")) Graph=Candidate;
    if (!Graph) return false;
    const TArray<UEdGraphNode*> OldNodes=Graph->Nodes; // 副本迭代，删除不失效当前列表。
    for (UEdGraphNode* Old : OldNodes) Graph->RemoveNode(Old);
    UAnimGraphNode_Root* Root=DemoPlayerAuthoring::Node<UAnimGraphNode_Root>(Graph,1600,0); // 唯一最终Pose输出，足底适配之后才提交。
    UAnimGraphNode_BlendListByInt* Movement=DemoPlayerAuthoring::Node<UAnimGraphNode_BlendListByInt>(Graph,0,0); // 九状态之间使用引擎0.1秒平滑混合。
    while (Movement->Node.GetBlendTimes().Num()<Names.Num()) Movement->Node.AddPose();
    Movement->ReconstructNode();
    for (int32 Index=0; Index<Names.Num(); ++Index) // 每状态独立Evaluator，跳跃取样由真实落地边沿控制。
    {
        UAnimGraphNode_SequenceEvaluator* Player=DemoPlayerAuthoring::Node<UAnimGraphNode_SequenceEvaluator>(Graph,-400,Index*170); // 无Notify/RootMotion副作用。
        Player->Node.SetSequence(Clips.FindChecked(Names[Index])); Player->Node.SetTeleportToExplicitTime(true); Player->Node.SetShouldLoop(Index!=3&&Index!=5);
        if (!DemoPlayerAuthoring::Variable(Graph,FName(*(Names[Index].ToString()+TEXT("Time"))),Player,TEXT("ExplicitTime"),-680,Index*170+50) ||
            !DemoPlayerAuthoring::Link(Player,TEXT("Pose"),Movement,FName(*FString::Printf(TEXT("BlendPose_%d"),Index)))) return false;
    }
    if (!DemoPlayerAuthoring::Variable(Graph,TEXT("LocomotionState"),Movement,TEXT("ActiveChildIndex"),-250,-150)) return false;
    UAnimGraphNode_SequenceEvaluator* Upper=DemoPlayerAuthoring::Node<UAnimGraphNode_SequenceEvaluator>(Graph,0,-450); // 独立正常肩位的上身动作。
    Upper->Node.SetSequence(Appearance->WeaponPoses.FindChecked(TEXT("pistol")).Hold); Upper->Node.SetTeleportToExplicitTime(true);
    for (FOptionalPinFromProperty& Pin : Upper->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true; // 显式公开原生动态资源。
    Upper->ReconstructNode();
    UAnimGraphNode_SequenceEvaluator* PreviousUpper=DemoPlayerAuthoring::Node<UAnimGraphNode_SequenceEvaluator>(Graph,0,-850); // 取消前冻结的完整上身Pose。
    PreviousUpper->Node.SetSequence(Appearance->WeaponPoses.FindChecked(TEXT("pistol")).Hold); PreviousUpper->Node.SetTeleportToExplicitTime(true);
    for (FOptionalPinFromProperty& Pin : PreviousUpper->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true;
    PreviousUpper->ReconstructNode();
    UAnimGraphNode_TwoWayBlend* UpperBlend=DemoPlayerAuthoring::Node<UAnimGraphNode_TwoWayBlend>(Graph,300,-450); // 与机械图相同权重，整枪锚点/手/弹匣同步变化。
    UAnimGraphNode_LayeredBoneBlend* Layer=DemoPlayerAuthoring::Node<UAnimGraphNode_LayeredBoneBlend>(Graph,500,0); // 腿和骨盆来自移动，spine与IK枪来自上身。
    Layer->Node.LayerSetup[0].BranchFilters.Reset();
    FBranchFilter Spine; Spine.BoneName=TEXT("spine_01"); Spine.BlendDepth=0; // 完整胸部及头臂，不覆盖骨盆/腿。
    FBranchFilter IK; IK.BoneName=TEXT("ik_hand_root"); IK.BlendDepth=0; // 独立手IK支系包含动画枪锚点。
    Layer->Node.LayerSetup[0].BranchFilters.Add(Spine); Layer->Node.LayerSetup[0].BranchFilters.Add(IK);
    Layer->ReconstructNode();
    UAnimGraphNode_LocalToComponentSpace* ToComponent=DemoPlayerAuthoring::Node<UAnimGraphNode_LocalToComponentSpace>(Graph,750,0); // 全部移动/上身混合已完成，此处转换供最终鞋底检测。
    UAnimGraphNode_DemoPlayerGrounding* Ground=DemoPlayerAuthoring::Node<UAnimGraphNode_DemoPlayerGrounding>(Graph,1000,0); // 只改双腿旋转/踝位置，不动骨盆与上身枪手关系。
    Ground->Node.LeftSole=Appearance->LeftSole; Ground->Node.RightSole=Appearance->RightSole; Ground->ReconstructNode(); // 图CDO自包含真实鞋底常量，工作线程不读取DA/UObject。
    UAnimGraphNode_ComponentToLocalSpace* ToLocal=DemoPlayerAuthoring::Node<UAnimGraphNode_ComponentToLocalSpace>(Graph,1350,0); // 原生骨控制回到引擎标准根Pose。
    if (!DemoPlayerAuthoring::Variable(Graph,TEXT("UpperBodySequence"),Upper,TEXT("Sequence"),-350,-600) ||
        !DemoPlayerAuthoring::Variable(Graph,TEXT("UpperBodyTime"),Upper,TEXT("ExplicitTime"),-350,-450) ||
        !DemoPlayerAuthoring::Variable(Graph,TEXT("PreviousUpperBodySequence"),PreviousUpper,TEXT("Sequence"),-350,-1000) ||
        !DemoPlayerAuthoring::Variable(Graph,TEXT("PreviousUpperBodyTime"),PreviousUpper,TEXT("ExplicitTime"),-350,-850) ||
        !DemoPlayerAuthoring::Variable(Graph,TEXT("UpperBodyBlendAlpha"),UpperBlend,TEXT("Alpha"),50,-250) ||
        !DemoPlayerAuthoring::Link(PreviousUpper,TEXT("Pose"),UpperBlend,TEXT("A")) ||
        !DemoPlayerAuthoring::Link(Upper,TEXT("Pose"),UpperBlend,TEXT("B")) ||
        !DemoPlayerAuthoring::Variable(Graph,TEXT("UpperBodyWeight"),Layer,TEXT("BlendWeights_0"),100,-250) ||
        !DemoPlayerAuthoring::Link(Movement,TEXT("Pose"),Layer,TEXT("BasePose")) ||
        !DemoPlayerAuthoring::Link(UpperBlend,TEXT("Pose"),Layer,TEXT("BlendPoses_0")) ||
        !DemoPlayerAuthoring::Variable(Graph,TEXT("bGroundedForFeet"),Ground,TEXT("bGrounded"),700,220) ||
        !DemoPlayerAuthoring::Variable(Graph,TEXT("FootFloorPoint"),Ground,TEXT("FloorPoint"),700,320) ||
        !DemoPlayerAuthoring::Variable(Graph,TEXT("FootFloorNormal"),Ground,TEXT("FloorNormal"),700,420) ||
        !DemoPlayerAuthoring::Link(Layer,TEXT("Pose"),ToComponent,TEXT("LocalPose")) ||
        !DemoPlayerAuthoring::Link(ToComponent,TEXT("ComponentPose"),Ground,TEXT("ComponentPose")) ||
        !DemoPlayerAuthoring::Link(Ground,TEXT("Pose"),ToLocal,TEXT("ComponentPose")) ||
        !DemoPlayerAuthoring::Link(ToLocal,TEXT("Pose"),Root,TEXT("Result"))) return false;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status==BS_Error || !BP->GeneratedClass) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_GRAPH_COMPILE_FAILED")); return false; }
    if (Appearance->EyeMesh!=EyeMeshBefore || !Appearance->LeftEyeRest.Equals(LeftEyeBefore) || !Appearance->RightEyeRest.Equals(RightEyeBefore)
        || !FVector(Appearance->EyeMaxYaw,Appearance->EyeMaxPitch,Appearance->EyeFollowSpeed).Equals(EyeLimitsBefore))
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_BUILD_CHANGED_EYES independent eye configuration must survive mesh/TP rebake")); return false; }
    UE_LOG(LogFPSDemo,Log,TEXT("PLAYER_BUILD_EYES_PRESERVED configured=%d"),EyeMeshBefore!=nullptr); // 先模型再眼球导入；未来模型重烘也必须保留已设置值。
    Appearance->AnimClass=BP->GeneratedClass; Appearance->MarkPackageDirty(); BP->MarkPackageDirty();
    if (!RebuildAimLayer()) return false; // 完整重建也必须保留瞄准层，不能依赖只执行过一次的补丁脚本。
    if (!RebuildPistolRunArm()) return false; // 全量重烘同样安装单手跑动层，避免下次导入丢失左臂摆动。
    if (!RebuildMeleeFullBodyLayer()) return false; // 全量制作同样保留静止近战骨盆/双腿层，配置关闭时权重为零。
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_ANIMATION_BUILD_SUCCESS locomotion=9 weapons=4 upper=16 mechanicalGraphs=4 aim=1")); return true;
}

bool UDemoPlayerAnimationAuthoring::RebuildMeleeFullBodyLayer()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    UAnimBlueprint* BP=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/Player/Animations/ABP_PlayerFullBody.ABP_PlayerFullBody")); // 现有图短期借用；未知父类拒绝改写。
    if (!BP || BP->ParentClass!=UDemoPlayerAnimInstance::StaticClass())
    { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_FULL_BODY_GRAPH_MISSING")); return false; }
    UEdGraph* Graph=nullptr; // BP拥有的AnimGraph，不重建其他函数图。
    for (UEdGraph* Candidate:BP->FunctionGraphs) if (Candidate->GetFName()==TEXT("AnimGraph")) Graph=Candidate;
    if (!Graph) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_FULL_BODY_NO_ANIMGRAPH")); return false; }
    UAnimGraphNode_TwoWayBlend* FullBody=nullptr; // 重跑按稳定注释定位已安装节点，避免重复叠加。
    UAnimGraphNode_LocalToComponentSpace* ToComponent=nullptr; // 所有本地Pose混合后的唯一CS入口。
    UEdGraphPin* AuthoredPose=nullptr; // 原上身层使用的同一混合Pose输出，含作者骨盆/腿，拥有者是Graph。
    UEdGraphPin* UpperInput=nullptr; // 原spine动作层输入，用于修复旧安装器直接复用Pose输出导致连接被Schema移走的问题。
    UAnimGraphNode_TwoWayBlend* AuthoredBlend=nullptr; // 全身层独立的只读求值支路；UE Pose输出不能像普通变量引脚一样随意一接多。
    for (UEdGraphNode* Node:Graph->Nodes)
    {
        if (Node->NodeComment==TEXT("StationaryMeleeFullBody")) FullBody=Cast<UAnimGraphNode_TwoWayBlend>(Node);
        if (Node->NodeComment==TEXT("MeleeAuthoredPose")) AuthoredBlend=Cast<UAnimGraphNode_TwoWayBlend>(Node);
        if (auto* Found=Cast<UAnimGraphNode_LocalToComponentSpace>(Node)) ToComponent=Found;
        if (auto* Layer=Cast<UAnimGraphNode_LayeredBoneBlend>(Node))
        {
            bool bSpineLayer=false; // 只找原spine_01动作层，不能误接单手跑动循环。
            for (const FInputBlendPose& Setup:Layer->Node.LayerSetup)
                for (const FBranchFilter& Filter:Setup.BranchFilters) if (Filter.BoneName==TEXT("spine_01")) bSpineLayer=true;
            UEdGraphPin* Input=Layer->FindPin(TEXT("BlendPoses_0")); // 图检查期间同步借用，重建前不缓存到下一轮调用。
            if (bSpineLayer && Input) { UpperInput=Input; if (Input->LinkedTo.Num()==1) AuthoredPose=Input->LinkedTo[0]; }
        }
    }
    if (!ToComponent || !UpperInput) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_FULL_BODY_UNEXPECTED_GRAPH")); return false; }
    BP->Modify();
    if (!AuthoredPose && FullBody && !AuthoredBlend)
    {
        UEdGraphPin* OldInput=FullBody->FindPin(TEXT("B")); // 只修复本工具上一版的唯一已知结构，未知手工图拒绝改写。
        if (!OldInput || OldInput->LinkedTo.Num()!=1) return false;
        AuthoredPose=OldInput->LinkedTo[0]; OldInput->BreakLinkTo(AuthoredPose);
        if (!Graph->GetSchema()->TryCreateConnection(AuthoredPose,UpperInput)) return false;
        UE_LOG(LogFPSDemo,Log,TEXT("MELEE_GRAPH_RESTORED_UPPER_BRANCH"));
    }
    if (!AuthoredPose) return false;
    if (!AuthoredBlend)
    {
        AuthoredBlend=DemoPlayerAuthoring::Node<UAnimGraphNode_TwoWayBlend>(Graph,400,-750); AuthoredBlend->NodeComment=TEXT("MeleeAuthoredPose"); // 独立支路读取相同缓存，不复制Gameplay或播放时钟。
        UAnimGraphNode_SequenceEvaluator* Current=DemoPlayerAuthoring::Node<UAnimGraphNode_SequenceEvaluator>(Graph,0,-750); // 当前完整骨盆/腿/上身资源。
        UAnimGraphNode_SequenceEvaluator* Previous=DemoPlayerAuthoring::Node<UAnimGraphNode_SequenceEvaluator>(Graph,0,-1050); // 取消时同上身层冻结旧Pose，避免骨盆瞬间归零。
        for (UAnimGraphNode_SequenceEvaluator* Eval:{Current,Previous})
        {
            Eval->Node.SetTeleportToExplicitTime(true); Eval->Node.SetShouldLoop(false);
            for (FOptionalPinFromProperty& Pin:Eval->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true;
            Eval->ReconstructNode();
        }
        if (!DemoPlayerAuthoring::Variable(Graph,TEXT("UpperBodySequence"),Current,TEXT("Sequence"),-350,-750)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("UpperBodyTime"),Current,TEXT("ExplicitTime"),-350,-650)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("PreviousUpperBodySequence"),Previous,TEXT("Sequence"),-350,-1050)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("PreviousUpperBodyTime"),Previous,TEXT("ExplicitTime"),-350,-950)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("UpperBodyBlendAlpha"),AuthoredBlend,TEXT("Alpha"),100,-500)
            || !DemoPlayerAuthoring::Link(Previous,TEXT("Pose"),AuthoredBlend,TEXT("A"))
            || !DemoPlayerAuthoring::Link(Current,TEXT("Pose"),AuthoredBlend,TEXT("B"))) return false;
    }
    if (!FullBody)
    {
        UEdGraphPin* Input=ToComponent->FindPin(TEXT("LocalPose")); // 原移动+上身+跑动左臂合成输入。
        if (!Input || Input->LinkedTo.Num()!=1) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_FULL_BODY_INVALID_INPUT")); return false; }
        UEdGraphPin* Previous=Input->LinkedTo[0]; // 插入时不重建前后节点，因此本次连接期间有效。
        FullBody=DemoPlayerAuthoring::Node<UAnimGraphNode_TwoWayBlend>(Graph,720,-160);
        FullBody->NodeComment=TEXT("StationaryMeleeFullBody"); FullBody->bCommentBubbleVisible=true;
        Input->BreakLinkTo(Previous);
        if (!Graph->GetSchema()->TryCreateConnection(Previous,FullBody->FindPin(TEXT("A")))
            || !DemoPlayerAuthoring::Link(FullBody,TEXT("Pose"),ToComponent,TEXT("LocalPose"))
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("MeleeFullBodyWeight"),FullBody,TEXT("Alpha"),400,-160))
        { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_FULL_BODY_LINK_FAILED")); return false; }
    }
    if (FullBody->FindPin(TEXT("B"))->LinkedTo.IsEmpty()
        && !DemoPlayerAuthoring::Link(AuthoredBlend,TEXT("Pose"),FullBody,TEXT("B"))) return false; // 重跑不动已存在的正确输入。
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status==BS_Error || !BP->GeneratedClass) { UE_LOG(LogFPSDemo,Error,TEXT("MELEE_FULL_BODY_COMPILE_FAILED")); return false; }
    BP->MarkPackageDirty(); UE_LOG(LogFPSDemo,Display,TEXT("MELEE_FULL_BODY_GRAPH_READY")); return true;
}

bool UDemoPlayerAnimationAuthoring::RebuildAimLayer()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    UDemoPlayerAppearance* Appearance=LoadObject<UDemoPlayerAppearance>(nullptr,TEXT("/Game/Player/DA_PlayerAppearance.DA_PlayerAppearance")); // 只借用现有外观，禁止默认值覆盖已调过的颜色/眼部/动画配置。
    UAnimBlueprint* BP=Appearance && Appearance->AnimClass?Cast<UAnimBlueprint>(Appearance->AnimClass->ClassGeneratedBy):nullptr; // 修改当前Appearance实际使用的图，不能误改已停用卡通图。
    if (!Appearance || !BP || BP->ParentClass!=UDemoPlayerAnimInstance::StaticClass())
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_AIM_GRAPH_MISSING appearance/BP/parent")); return false; }
    UEdGraph* Graph=nullptr; // BP拥有的图，只在本次同步作者调用借用。
    for (UEdGraph* Candidate:BP->FunctionGraphs) if (Candidate->GetFName()==TEXT("AnimGraph")) Graph=Candidate;
    if (!Graph) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_AIM_GRAPH_MISSING AnimGraph")); return false; }
    UAnimGraphNode_DemoPlayerGrounding* Ground=nullptr; // 最终CS输入，必须是既有足底节点。
    UAnimGraphNode_DemoPlayerAim* Aim=nullptr; // 重跑复用唯一节点，不重复创建变量读取或堆叠俯仰。
    for (UEdGraphNode* Node:Graph->Nodes) // 图上已有对象的短期借用。
    { if (auto* Found=Cast<UAnimGraphNode_DemoPlayerGrounding>(Node)) Ground=Found; if (auto* Found=Cast<UAnimGraphNode_DemoPlayerAim>(Node)) Aim=Found; }
    if (!Ground) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_AIM_GRAPH_MISSING final grounding")); return false; }
    BP->Modify();
    if (!Aim)
    {
        UEdGraphPin* Output=Ground->FindPin(TEXT("Pose")); // 修改前保存唯一后继输入；失败不保存包。
        if (!Output || Output->LinkedTo.Num()!=1) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_AIM_GRAPH_UNEXPECTED_OUTPUT")); return false; }
        UEdGraphPin* Destination=Output->LinkedTo[0]; // 插入节点期间不会重建后继节点，指针在本次同步操作内有效。
        Aim=DemoPlayerAuthoring::Node<UAnimGraphNode_DemoPlayerAim>(Graph,1250,0);
        Aim->Node.ReferencePelvis=Appearance->ReferencePelvis; Aim->ReconstructNode();
        Output->BreakLinkTo(Destination);
        if (!DemoPlayerAuthoring::Link(Ground,TEXT("Pose"),Aim,TEXT("ComponentPose"))
            || !Graph->GetSchema()->TryCreateConnection(Aim->FindPin(TEXT("Pose")),Destination)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("AimPitch"),Aim,TEXT("Pitch"),900,500)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("AimRightAxis"),Aim,TEXT("RightAxis"),900,600)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("AimTorsoFraction"),Aim,TEXT("TorsoFraction"),900,700)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("AimSupportWeight"),Aim,TEXT("SupportWeight"),900,800)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("bAimWeaponActive"),Aim,TEXT("bEnabled"),900,900)) return false;
    }
    Aim->Node.ReferencePelvis=Appearance->ReferencePelvis; // 换模型后完整构建仍使用新的作者骨盆，不残留旧基线。
    // 已保存的旧节点把ReferencePelvis序列化为隐藏字段；仅修改UPROPERTY默认元数据不会迁移旧图。
    for (FOptionalPinFromProperty& PropertyPin:Aim->ShowPinForProperties) // 同步迁移唯一新增动态输入，保持其他作者选择。
        if (PropertyPin.PropertyName==TEXT("ReferencePelvis")) PropertyPin.bShowPin=true;
    Aim->ReconstructNode(); // 新运行参数加入后重建Pins；已有输入连接由引擎按名字保留。
    if (UEdGraphPin* PelvisPin=Aim->FindPin(TEXT("ReferencePelvis")); !PelvisPin || (PelvisPin->LinkedTo.IsEmpty()
        && !DemoPlayerAuthoring::Variable(Graph,TEXT("AimReferencePelvis"),Aim,TEXT("ReferencePelvis"),900,1300)))
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_AIM_GRAPH_PIN_FAILED ReferencePelvis")); return false; } // 每枪作者坐标传给独立IK支系，不移动蒙皮或改变骨长。
    if (UEdGraphPin* Offset=Aim->FindPin(TEXT("GripOffset")); !Offset || (Offset->LinkedTo.IsEmpty()
        && !DemoPlayerAuthoring::Variable(Graph,TEXT("AimGripOffset"),Aim,TEXT("GripOffset"),900,1000))) return false; // 只补新引脚一次，重跑不累积变量节点。
    if (UEdGraphPin* Yaw=Aim->FindPin(TEXT("HeadYaw")); !Yaw || (Yaw->LinkedTo.IsEmpty()
        && !DemoPlayerAuthoring::Variable(Graph,TEXT("HeadYaw"),Aim,TEXT("HeadYaw"),900,1100))) return false; // 新水平头部角度只绑定一次，保留既有俯仰图。
    if (UEdGraphPin* Up=Aim->FindPin(TEXT("UpAxis")); !Up || (Up->LinkedTo.IsEmpty()
        && !DemoPlayerAuthoring::Variable(Graph,TEXT("AimUpAxis"),Aim,TEXT("UpAxis"),900,1200))) return false; // 真实组件空间竖轴，不能在图中硬编码身体导入朝向。
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status==BS_Error || !BP->GeneratedClass) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_AIM_GRAPH_COMPILE_FAILED")); return false; }
    BP->MarkPackageDirty(); UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_AIM_GRAPH_READY nodes=%d"),Graph->Nodes.Num()); return true;
}

bool UDemoPlayerAnimationAuthoring::RebuildPistolRunArm()
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    UDemoPlayerAppearance* Appearance=LoadObject<UDemoPlayerAppearance>(nullptr,TEXT("/Game/Player/DA_PlayerAppearance.DA_PlayerAppearance")); // 只修改手枪新增循环引用，保留既有眼球、鞋底及其他武器配置。
    UAnimBlueprint* BP=LoadObject<UAnimBlueprint>(nullptr,TEXT("/Game/Player/Animations/ABP_PlayerFullBody.ABP_PlayerFullBody")); // 增量修改原图，不重建瞄准或足IK。
    FDemoPlayerWeaponPoses* Poses=Appearance?Appearance->WeaponPoses.Find(TEXT("pistol")):nullptr; // 当前正式手枪配置。
    USkeletalMesh* Body=Appearance?Appearance->BodyMesh.Get():nullptr; // 原骨架/蒙皮只读。
    UAnimSequence* Run=Appearance && Appearance->MovementClips.IsValidIndex(2)?Appearance->MovementClips[2].Get():nullptr; // 只借用实际跑步周期；空手上身姿势不能直接叠到持枪锁骨。
    if (!Body || !BP || BP->ParentClass!=UDemoPlayerAnimInstance::StaticClass() || !Poses || !Poses->Hold || !Run
        || Run->GetSkeleton()!=Body->GetSkeleton() || Poses->Hold->GetSkeleton()!=Body->GetSkeleton())
    { UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_RUN_ARM_SOURCE_INVALID")); return false; }
    const FString Path=TEXT("/Game/Player/Animations/Upper/A_TP_Pistol_RunLeftArm"); // 唯一新增动作，不覆盖原Hold/Reload序列。
    if (Poses->RunLeftArm && Poses->RunLeftArm->GetPathName()!=Path+TEXT(".A_TP_Pistol_RunLeftArm"))
    { UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_RUN_ARM_CUSTOM_ASSET refusing to overwrite configured custom clip")); return false; }
    FAnimPoseEvaluationOptions Options; Options.OptionalSkeletalMesh=Body; // 同步离线采样实际骨长，不使用替代Skeleton。
    FAnimPose Hold; UAnimPoseExtensions::GetAnimPoseAtTime(Poses->Hold,0.f,Options,Hold); // 当前右手握枪姿势是所有非左臂轨道的基准。
    if (!Hold.IsValid()) { UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_RUN_ARM_HOLD_INVALID")); return false; }
    const FTransform RestWrist=UAnimPoseExtensions::GetBonePose(Hold,TEXT("hand_l"),EAnimPoseSpaces::World); // 当前自然垂手方向与手指保持不变，不沿用空手跑步横抬肘的框架。
    const FVector Shoulder=UAnimPoseExtensions::GetBonePose(Hold,TEXT("upperarm_l"),EAnimPoseSpaces::World).GetLocation(); // 正式肩根固定，不靠平移肩膀或缩骨躲避衣服。
    const FVector RestElbow=UAnimPoseExtensions::GetBonePose(Hold,TEXT("lowerarm_l"),EAnimPoseSpaces::World).GetLocation(); // 当前微屈左肘作为摆动平面基准。
    const FVector Right=(UAnimPoseExtensions::GetBonePose(Hold,TEXT("upperarm_r"),EAnimPoseSpaces::World).GetLocation()-Shoulder).GetSafeNormal(); // 真实肩链右轴，适配当前导入朝向。
    TArray<FName> Bones; UAnimPoseExtensions::GetBoneNames(Hold,Bones); // 资源保存完整轨道，单独预览也能看到当前持枪姿势。
    const FReferenceSkeleton& Ref=Body->GetRefSkeleton(); // 分类使用真实父链，包括手指与扭转辅助骨。
    TSet<FName> LeftBones; // 只覆盖upperarm_l及后代，锁骨与躯干保持现有握姿。
    for (int32 Index=0;Index<Ref.GetNum();++Index)
    {
        int32 Parent=Index; // 单骨祖先遍历，不修改参考骨。
        while (Parent!=INDEX_NONE)
        {
            if (Ref.GetBoneName(Parent)==TEXT("upperarm_l")) { LeftBones.Add(Ref.GetBoneName(Index)); break; }
            Parent=Ref.GetParentIndex(Parent);
        }
    }
    UAnimSequence* Sequence=DemoPlayerAuthoring::Asset<UAnimSequence>(Path); // 新包或已存在同名包，保存由Python白名单负责。
    Sequence->SetSkeleton(Body->GetSkeleton()); Sequence->SetPreviewMesh(Body);
    IAnimationDataController& Controller=Sequence->GetController(); // 引擎标准作者接口，自动使压缩派生数据失效。
    const int32 Frames=FMath::Max(1,FMath::RoundToInt(Run->GetPlayLength()*120.f)); // 120Hz、片长与跑步一致，末帧复制起点确保无循环接缝。
    Controller.OpenBracket(FText::FromString(TEXT("Bake pistol left arm run cycle")),false);
    Controller.InitializeModel(); Controller.SetFrameRate(FFrameRate(120,1),false); Controller.SetNumberOfFrames(FFrameNumber(Frames),false); Controller.RemoveAllBoneTracks(false);
    TArray<TArray<FVector3f>> Positions,Scales; // [骨][帧]局部平移/缩放，仅本次作者调用持有。
    TArray<TArray<FQuat4f>> Rotations; // 原骨长的局部旋转，不通过伸缩制造摆臂。
    Positions.SetNum(Bones.Num()); Scales.SetNum(Bones.Num()); Rotations.SetNum(Bones.Num());
    for (int32 Frame=0;Frame<=Frames;++Frame)
    {
        FAnimPose Motion=Hold; // 每帧从当前握姿独立计算；保持既有指部自然姿势，不累积误差。
        const float Wave=FMath::Sin(2.f*PI*(Frame==Frames?0.f:static_cast<float>(Frame)/Frames)); // 一步循环一组前后摆动，与腿部时钟同相位，尾帧显式回起点。
        const FQuat Swing(Right,FMath::DegreesToRadians(24.f*Wave)); // 24度肩摆幅；手掌在躯干外侧沿纵向圆弧移动，不横扫腹部。
        const FVector WristOffset=RestWrist.GetLocation()-Shoulder; // 原自然垂手肩腕距离，厘米。
        const FVector Target=Shoulder+Swing.RotateVector(WristOffset.GetSafeNormal()*(WristOffset.Size()-2.f*(Wave+1.f))); // 前摆额外最多4cm屈肘，保持两段真实臂长，避免僵直钟摆。
        const FVector Pole=Shoulder+Swing.RotateVector(RestElbow-Shoulder)-Right*8.f; // 肘平面略向身体外侧，所有相位连续。
        if (!DemoPlayerAuthoring::ArmIK(Motion,TEXT("l"),Target,(Swing*RestWrist.GetRotation()).GetNormalized(),Pole))
        { Controller.CloseBracket(false); UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_RUN_ARM_IK_FAILED frame=%d"),Frame); return false; }
        for (int32 Index=0;Index<Bones.Num();++Index)
        {
            FTransform Local=UAnimPoseExtensions::GetBonePose(LeftBones.Contains(Bones[Index])?Motion:Hold,Bones[Index],EAnimPoseSpaces::Local); // 只替换由当前Hold解算的左臂；右腕/指/枪锚点逐帧严格等于Hold。
            Local.NormalizeRotation(); Positions[Index].Add(FVector3f(Local.GetLocation())); Rotations[Index].Add(FQuat4f(Local.GetRotation())); Scales[Index].Add(FVector3f(Local.GetScale3D()));
        }
    }
    for (int32 Index=0;Index<Bones.Num();++Index)
        if (!Controller.AddBoneCurve(Bones[Index],false) || !Controller.SetBoneTrackKeys(Bones[Index],Positions[Index],Rotations[Index],Scales[Index],false))
        { Controller.CloseBracket(false); UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_RUN_ARM_TRACK_INVALID %s"),*Bones[Index].ToString()); return false; }
    Controller.NotifyPopulated(); Controller.CloseBracket(false); Sequence->PostEditChange(); Sequence->MarkPackageDirty();
    UEdGraph* Graph=nullptr; // 当前BP拥有图，节点操作仅在Editor游戏线程同步执行。
    for (UEdGraph* Candidate:BP->FunctionGraphs) if (Candidate->GetFName()==TEXT("AnimGraph")) Graph=Candidate;
    if (!Graph) { UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_RUN_ARM_GRAPH_MISSING")); return false; }
    UAnimGraphNode_LocalToComponentSpace* ToComponent=nullptr; // 既有上身混合后、地面/瞄准前的转换节点。
    UAnimGraphNode_LayeredBoneBlend* Layer=nullptr; // 稳定注释标识用于增量重跑，避免重复叠层。
    for (UEdGraphNode* Node:Graph->Nodes)
    {
        if (auto* Found=Cast<UAnimGraphNode_LocalToComponentSpace>(Node)) ToComponent=Found;
        if (Node->NodeComment==TEXT("PistolRunLeftArm")) Layer=Cast<UAnimGraphNode_LayeredBoneBlend>(Node);
    }
    if (!ToComponent) { UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_RUN_ARM_GRAPH_NO_LOCAL_POSE")); return false; }
    BP->Modify();
    if (!Layer)
    {
        UEdGraphPin* Input=ToComponent->FindPin(TEXT("LocalPose")); // 插入前验证唯一前驱，未知手工图结构拒绝修改。
        if (!Input || Input->LinkedTo.Num()!=1) { UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_RUN_ARM_GRAPH_UNEXPECTED_INPUT")); return false; }
        UEdGraphPin* Previous=Input->LinkedTo[0]; // 后继不重建，连接期间短期借用有效。
        Layer=DemoPlayerAuthoring::Node<UAnimGraphNode_LayeredBoneBlend>(Graph,660,0);
        Layer->NodeComment=TEXT("PistolRunLeftArm"); Layer->Node.LayerSetup[0].BranchFilters.Reset();
        FBranchFilter Left; Left.BoneName=TEXT("upperarm_l"); Left.BlendDepth=0; // 精确整条左臂分支，右手/枪/腿继续取现有输入Pose。
        Layer->Node.LayerSetup[0].BranchFilters.Add(Left); Layer->ReconstructNode();
        UAnimGraphNode_SequenceEvaluator* Cycle=DemoPlayerAuthoring::Node<UAnimGraphNode_SequenceEvaluator>(Graph,340,440); // 显式相位来自真实移动，不运行Notify或RootMotion。
        Cycle->Node.SetSequence(Sequence); Cycle->Node.SetTeleportToExplicitTime(true); Cycle->Node.SetShouldLoop(true);
        for (FOptionalPinFromProperty& Pin:Cycle->ShowPinForProperties) if (Pin.PropertyName==TEXT("Sequence")) Pin.bShowPin=true;
        Cycle->ReconstructNode(); Input->BreakLinkTo(Previous);
        if (!Graph->GetSchema()->TryCreateConnection(Previous,Layer->FindPin(TEXT("BasePose")))
            || !DemoPlayerAuthoring::Link(Layer,TEXT("Pose"),ToComponent,TEXT("LocalPose"))
            || !DemoPlayerAuthoring::Link(Cycle,TEXT("Pose"),Layer,TEXT("BlendPoses_0"))
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("RunLeftArmSequence"),Cycle,TEXT("Sequence"),0,520)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("RunLeftArmTime"),Cycle,TEXT("ExplicitTime"),0,620)
            || !DemoPlayerAuthoring::Variable(Graph,TEXT("RunLeftArmWeight"),Layer,TEXT("BlendWeights_0"),350,200)) return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FKismetEditorUtilities::CompileBlueprint(BP);
    if (BP->Status==BS_Error || !BP->GeneratedClass) { UE_LOG(LogFPSDemo,Error,TEXT("PISTOL_RUN_ARM_GRAPH_COMPILE_FAILED")); return false; }
    Appearance->Modify(); Poses=Appearance->WeaponPoses.Find(TEXT("pistol")); Poses->RunLeftArm=Sequence;
    BP->MarkPackageDirty(); Appearance->MarkPackageDirty();
    UE_LOG(LogFPSDemo,Display,TEXT("PISTOL_RUN_ARM_READY frames=%d leftBones=%d duration=%.3f"),Frames,LeftBones.Num(),Sequence->GetPlayLength());
    return true;
}

UClass* UDemoPlayerAnimationAuthoring::BuildWeaponMechanicalGraph(const FString& Model, USkeletalMesh* Mesh, UAnimSequence* DefaultClip)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs model=%s"),__FUNCTION__,*Model);
    if ((Model!=TEXT("Pistol") && Model!=TEXT("Rifle") && Model!=TEXT("Shotgun") && Model!=TEXT("Sniper")) || !Mesh || !DefaultClip)
    { UE_LOG(LogFPSDemo,Error,TEXT("PACK_MECHANICAL_INPUT_REJECT")); return nullptr; }
    return DemoPlayerAuthoring::BuildMechanical(Model,Mesh,DefaultClip);
}
