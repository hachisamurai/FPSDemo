#include "Animation/DemoFPSequenceAuthoring.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "AnimPose.h"
#include "TwoBoneIK.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Debug/DemoLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace DemoFPSequence
{
    /** Model为四枪标识；只读Blender接触作者输出，调用者持有返回JSON至本次同步烘焙结束。 */
    TSharedPtr<FJsonObject> LoadLeftTracks(const FString& Model)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs model=%s"),__FUNCTION__,*Model);
        const FString Path=FPaths::ProjectDir()/TEXT("Art/Player/Animations/left_hand_contact_tracks.json"); // 离线输入，不形成打包运行依赖。
        FString Text; // UTF8作者文件内容，仅本次制作使用。
        TSharedPtr<FJsonObject> Root; // JSON树共享所有权，返回子对象独立保活。
        const TSharedPtr<FJsonObject>* Weapons=nullptr; // 借用树内映射，不缓存至下次制作。
        const TSharedPtr<FJsonObject>* Result=nullptr;
        if (!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)
            ||!Root.IsValid()||!Root->TryGetObjectField(TEXT("weapons"),Weapons)||!(*Weapons)->TryGetObjectField(Model,Result))
        { UE_LOG(LogFPSDemo,Error,TEXT("FP_LEFT_CONTACT_MISSING %s model=%s"),*Path,*Model); return nullptr; }
        return *Result;
    }

    /** Value为一帧作者数据；Wrist/Rotation是枪局部目标，Fingers为UE原指骨局部旋转增量，坏数据拒绝保存。 */
    bool ReadLeftFrame(const TSharedPtr<FJsonValue>& Value,FVector& Wrist,FQuat& Rotation,TMap<FName,FQuat>& Fingers)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] %hs"),__FUNCTION__);
        const TSharedPtr<FJsonObject>* Frame=nullptr; // 借用已解析的当前帧，输出均为值副本。
        const TArray<TSharedPtr<FJsonValue>>* Position=nullptr; // 厘米三元组。
        const TArray<TSharedPtr<FJsonValue>>* Quaternion=nullptr; // UE xyzw四元组。
        const TSharedPtr<FJsonObject>* FingerData=nullptr; // 15根指骨的局部增量。
        if (!Value->TryGetObject(Frame)||!(*Frame)->TryGetArrayField(TEXT("wrist_cm"),Position)||Position->Num()!=3
            ||!(*Frame)->TryGetArrayField(TEXT("rotation_xyzw"),Quaternion)||Quaternion->Num()!=4
            ||!(*Frame)->TryGetObjectField(TEXT("finger_delta_xyzw"),FingerData)) return false;
        Wrist=FVector((*Position)[0]->AsNumber(),(*Position)[1]->AsNumber(),(*Position)[2]->AsNumber());
        Rotation=FQuat((*Quaternion)[0]->AsNumber(),(*Quaternion)[1]->AsNumber(),(*Quaternion)[2]->AsNumber(),(*Quaternion)[3]->AsNumber());
        if (Wrist.ContainsNaN()||Rotation.ContainsNaN()||Rotation.SizeSquared()<.99) return false;
        Rotation.Normalize(); Fingers.Reset();
        for (const auto& Field:(*FingerData)->Values) // 增量仅允许当前左手指骨，不能覆盖枪锚点或右臂。
        {
            const TArray<TSharedPtr<FJsonValue>>* Components=nullptr; // 单指骨xyzw值，树拥有。
            if (!Field.Key.EndsWith(TEXT("_l"))||!Field.Value->TryGetArray(Components)||Components->Num()!=4) return false;
            FQuat Delta((*Components)[0]->AsNumber(),(*Components)[1]->AsNumber(),(*Components)[2]->AsNumber(),(*Components)[3]->AsNumber()); // 当前帧局部附加旋转，无平移或缩放。
            if (Delta.ContainsNaN()||Delta.SizeSquared()<.99) return false;
            Delta.Normalize(); Fingers.Add(FName(*Field.Key),Delta);
        }
        return Fingers.Num()==15;
    }

    /** Path为工具独占资产包路径；重建保持对象身份，不能覆盖别的目录。 */
    template<class T> T* Asset(const FString& Path)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
        const FString Name=FPackageName::GetLongPackageAssetName(Path); // 包名和对象名一一对应。
        T* Result=LoadObject<T>(nullptr,*(Path+TEXT(".")+Name)); // 包持有资源，当前只同步借用。
        if (!Result)
        {
            Result=NewObject<T>(CreatePackage(*Path),*Name,RF_Public|RF_Standalone);
            FAssetRegistryModule::AssetCreated(Result);
        }
        Result->Modify(); return Result;
    }

    /** 归一化时间T在A..B平滑过渡；端点夹紧避免动作阶段外的外插。 */
    float Ease(float T,float A,float B)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
        const float X=FMath::Clamp((T-A)/FMath::Max(B-A,.001f),0.f,1.f); // 当前区间进度，范围0..1。
        return X*X*(3.f-2.f*X);
    }

    /** Pose为此帧私有副本；Side为l/r，Target/Rotation是组件空间手腕目标，PoleOffset为相对肩的肘极点厘米值，无拉伸。 */
    void ArmIK(FAnimPose& Pose,const TCHAR* Side,const FVector& Target,const FQuat& Rotation,const FVector& PoleOffset)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
        const FName Upper(*FString::Printf(TEXT("upperarm_%s"),Side)); // 上臂链起点。
        const FName Lower(*FString::Printf(TEXT("lowerarm_%s"),Side)); // 肘关节骨。
        const FName Hand(*FString::Printf(TEXT("hand_%s"),Side)); // 手腕骨，指骨随它保持握持。
        FTransform A=UAnimPoseExtensions::GetBonePose(Pose,Upper,EAnimPoseSpaces::World); // IK修改的三个独立值副本。
        FTransform B=UAnimPoseExtensions::GetBonePose(Pose,Lower,EAnimPoseSpaces::World);
        FTransform C=UAnimPoseExtensions::GetBonePose(Pose,Hand,EAnimPoseSpaces::World);
        const FVector Pole=A.GetLocation()+PoleOffset; // 左右肘分别标定：左臂避镜头，右臂向外下弯以避开有体积的枪托。
        AnimationCore::SolveTwoBoneIK(A,B,C,Pole,Target,false,1.f,1.f);
        C.SetRotation(Rotation);
        UAnimPoseExtensions::SetBonePose(Pose,A,Upper,EAnimPoseSpaces::World);
        UAnimPoseExtensions::SetBonePose(Pose,B,Lower,EAnimPoseSpaces::World);
        UAnimPoseExtensions::SetBonePose(Pose,C,Hand,EAnimPoseSpaces::World);
    }

    /** Sequence为新烘焙手臂动作；单主实例FPAction通道，禁止对枪械另建同名Montage播放器。 */
    void Montage(UAnimSequence* Sequence,const FString& Name)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
        UAnimMontage* Result=Asset<UAnimMontage>(TEXT("/Game/Weapons/Animations/Montages/AM_FP_")+Name); // 包拥有的真实Montage。
        Result->SetSkeleton(Sequence->GetSkeleton()); Result->SlotAnimTracks.Reset(); Result->CompositeSections.Reset();
        FSlotAnimationTrack& Track=Result->SlotAnimTracks.AddDefaulted_GetRef(); // 唯一输出通道，对应主图Slot。
        Track.SlotName=TEXT("FPAction");
        FAnimSegment Segment; // 单段原速动作；运行时按ReloadSeconds调整播放速率。
        Segment.SetAnimReference(Sequence); Segment.StartPos=0; Segment.AnimStartTime=0;
        Segment.AnimEndTime=Sequence->GetPlayLength(); Segment.AnimPlayRate=1; Segment.LoopingCount=1;
        Track.AnimTrack.AnimSegments.Add(Segment);
        static_cast<UAnimCompositeBase*>(Result)->SetCompositeLength(Sequence->GetPlayLength());
        Result->AddAnimCompositeSection(TEXT("Default"),0);
        Result->BlendIn.SetBlendTime(.035f); Result->BlendOut.SetBlendTime(.05f);
        Result->PostEditChange(); Result->MarkPackageDirty();
    }
}

bool UDemoFPSequenceAuthoring::Build(const FString& Model,USkeletalMesh* Arms,USkeletalMesh* WeaponMesh,
    const TMap<FName,UAnimSequence*>& Clips,FVector SupportGrip,FVector MagazineGrip,FVector BoltGrip,
    FVector RightGrip,FVector RightElbowOffset,float BoltReachClearance,float Duration)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs"), __FUNCTION__);
    UAnimSequence* Template=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/FirstPersonArms/Animations/FP_Rifle_Idle.FP_Rifle_Idle")); // 只读模板姿势来源。
    if (!Arms||!WeaponMesh||!Template||Duration<=0||!Clips.Contains("Tactical")||!Clips.Contains("Empty")
        ||RightGrip.ContainsNaN()||RightElbowOffset.ContainsNaN()||!FMath::IsFinite(BoltReachClearance)||BoltReachClearance<0.f)
    { UE_LOG(LogFPSDemo,Error,TEXT("FP_AUTHOR_REJECT missing mesh/clip/duration")); return false; }
    FAnimPoseEvaluationOptions ArmsOptions; // 使用当前网格骨长的完整离线姿势，不提取根运动。
    ArmsOptions.OptionalSkeletalMesh=Arms;
    FAnimPose Base; // 模板首帧；每个新帧从此复制，避免累计误差。
    UAnimPoseExtensions::GetAnimPoseAtTime(Template,0,ArmsOptions,Base);
    if (!Base.IsValid()||Arms->GetRefSkeleton().FindBoneIndex(TEXT("ik_hand_gun"))==INDEX_NONE) return false;
    const FTransform OldGrip=UAnimPoseExtensions::GetSocketPose(Base,TEXT("GripPoint"),EAnimPoseSpaces::World); // 已标定枪位置。
    const FTransform BaseAnchor(FQuat::Identity,OldGrip.GetLocation()+FVector(20.f,0.f,8.f)); // 只前移枪/手腕并抬8cm，肩根保持镜头下方；给下方弹匣交接留出可见范围，枪托仍远离近裁剪。
    const FTransform Right=UAnimPoseExtensions::GetBonePose(Base,TEXT("hand_r"),EAnimPoseSpaces::World); // 保留右手握把与指骨姿态。
    const TSharedPtr<FJsonObject> LeftTracks=DemoFPSequence::LoadLeftTracks(Model); // 已验证真实蒙皮接触的离线轨迹，禁止退回旧固定32度托枪手型。
    if (!LeftTracks.IsValid()) return false;
    const FVector RightLocal=RightGrip; // 由Breach真实握把与手掌联合标定；不能再套用模板枪GripPoint的相对腕点。
    TArray<FName> Bones; // 与姿势中每一条导出轨道对应；不删除模板的辅助骨。
    UAnimPoseExtensions::GetBoneNames(Base,Bones);
    USkeleton* Skeleton=Arms->GetSkeleton(); // 共享Skeleton只增加Slot，不修改参考姿势或已有动画。
    Skeleton->RegisterSlotNode(TEXT("FPAction")); Skeleton->SetSlotGroupName(TEXT("FPAction"),TEXT("DefaultGroup")); Skeleton->MarkPackageDirty();
    FAnimPoseEvaluationOptions GunOptions; // 实际导入枪动画的骨局部轨迹采样。
    GunOptions.OptionalSkeletalMesh=WeaponMesh;
    for (const FName Kind:{FName("Idle"),FName("Fire"),FName("Tactical"),FName("Empty")}) // 四枪各四段，换弹八套。
    {
        const bool bReload=Kind=="Tactical"||Kind=="Empty"; // 只换弹使用机械关键帧和左手路径。
        const bool bEmpty=Kind=="Empty"; // 冻结选择，和运行时Ammo==0契约一致。
        const float Seconds=bReload?Duration:Kind=="Fire"?.18f:1.f; // 源动作时长；Idle为一秒轻微呼吸循环。
        const int32 Frames=FMath::RoundToInt(Seconds*60.f); // 固定60Hz烘焙，运行时自由插值。
        const TSharedPtr<FJsonObject>* LeftClip=nullptr; // 当前片段的接触轨迹，由LeftTracks共享持有。
        const TArray<TSharedPtr<FJsonValue>>* LeftSamples=nullptr; // 含首尾的60Hz样本，与机械动画同phase。
        if (!LeftTracks->TryGetObjectField(Kind.ToString(),LeftClip)||!(*LeftClip)->TryGetArrayField(TEXT("samples"),LeftSamples)||LeftSamples->Num()!=Frames+1)
        { UE_LOG(LogFPSDemo,Error,TEXT("FP_LEFT_CONTACT_FRAME_MISMATCH %s/%s expected=%d"),*Model,*Kind.ToString(),Frames+1); return false; }
        UAnimSequence* Gun=bReload?Clips.FindChecked(Kind):nullptr; // 借用与本片段配对的机械动画。
        FAnimPose GunStart; // 首帧用于把设计手腕点转换到活动骨局部。
        if (Gun) UAnimPoseExtensions::GetAnimPoseAtTime(Gun,0,GunOptions,GunStart);
        const FTransform BoltStart=Gun&&Model==TEXT("Sniper")?UAnimPoseExtensions::GetBonePose(GunStart,TEXT("bolt"),EAnimPoseSpaces::World):FTransform::Identity; // 只有狙击右手需要跟随bolt，手枪没有此骨。
        const FVector BoltOffset=BoltStart.InverseTransformPosition(BoltGrip); // 右手跟随真实枪栓旋转和往复。
        const FString Suffix=Model+TEXT("_")+(bReload?TEXT("Reload_"):TEXT(""))+Kind.ToString(); // 资产稳定命名。
        UAnimSequence* Result=DemoFPSequence::Asset<UAnimSequence>(TEXT("/Game/Weapons/Animations/Arms/A_FP_")+Suffix); // 工具生成的可编辑真实动画。
        Result->SetSkeleton(Skeleton); Result->SetPreviewMesh(Arms);
        IAnimationDataController& Controller=Result->GetController(); // 引擎正式数据接口，负责压缩/派生数据失效。
        Controller.OpenBracket(FText::FromString(TEXT("Bake paired first person weapon animation")),false);
        Controller.InitializeModel(); Controller.SetFrameRate(FFrameRate(60,1),false); Controller.SetNumberOfFrames(FFrameNumber(Frames),false);
        Controller.RemoveAllBoneTracks(false);
        TArray<TArray<FVector3f>> Positions,Scales; // [骨][帧]的数据副本，只活到本次烘焙结束。
        TArray<TArray<FQuat4f>> Rotations; // 使用归一化旋转，避免压缩阶段的非法四元数。
        Positions.SetNum(Bones.Num()); Scales.SetNum(Bones.Num()); Rotations.SetNum(Bones.Num());
        for (int32 Frame=0;Frame<=Frames;++Frame) // 包含最终关键帧，长度是Frames/60。
        {
            const float P=static_cast<float>(Frame)/Frames; // 本片段归一化进度0..1。
            const float Lift=bReload?DemoFPSequence::Ease(P,0,.10f)*(1.f-DemoFPSequence::Ease(P,.88f,1.f)):0.f; // 举枪段与复位段平滑。
            const float Kick=Kind=="Fire"?FMath::Sin(PI*P)*FMath::Exp(-4.f*P):0.f; // 枪与双手共同后坐，不改变玩法射线。
            const float Breath=Kind=="Idle"?.08f*FMath::Sin(2*PI*P):0.f; // 循环首尾相同，厘米级呼吸。
            const float Weight=Model==TEXT("Shotgun")?1.15f:Model==TEXT("Pistol")?.7f:1.f; // 只影响动作幅度，不改变装填时长。
            const FTransform Anchor(FRotator(2.f*Lift, -5.f*Lift,-10.f*Lift*Weight).Quaternion(),BaseAnchor.GetLocation()+FVector(-2.f*Lift-2.f*Kick,-4.f*Lift,3.f*Lift+Breath)); // 限制举枪/侧倾幅度，保留换匣可读性并避免肩肘或狙击镜进入画面中央。
            FAnimPose Pose=Base; // 当前帧独占副本，修改不污染模板与其他枪。
            FVector LeftWrist; // 枪局部左腕厘米目标，含活动弹匣朝向与外侧避让路径。
            FQuat LeftLocalRotation; // 枪局部真实转腕四元数，不是固定托枪姿态。
            TMap<FName,FQuat> FingerDeltas; // 本帧15根指骨的抓握/张开增量。
            if (!DemoFPSequence::ReadLeftFrame((*LeftSamples)[Frame],LeftWrist,LeftLocalRotation,FingerDeltas))
            { Controller.CloseBracket(false); UE_LOG(LogFPSDemo,Error,TEXT("FP_LEFT_CONTACT_INVALID %s/%s frame=%d"),*Model,*Kind.ToString(),Frame); return false; }
            const FVector LeftTarget=Anchor.TransformPosition(LeftWrist); // 组件空间目标，与本帧武器锚点同一次计算。
            FVector RightTarget=Anchor.TransformPosition(RightLocal); // 右手默认始终保持握把相对位置。
            const FQuat LeftRotation=Anchor.GetRotation()*LeftLocalRotation; // 弹匣旋转与腕旋转已同时进入作者轨迹。
            FQuat RightRotation=Anchor.GetRotation()*Right.GetRotation();
            if (bReload)
            {
                FAnimPose GunPose; // 读取同phase真实机械动作，不凭另一套估计动画重建弹匣位置。
                UAnimPoseExtensions::GetAnimPoseAtTime(Gun,P*Gun->GetPlayLength(),GunOptions,GunPose);
                // 左手的转腕/收指/抽插匣/拉机柄由同phase的接触轨迹负责；右手狙击拉栓保留独立控制。
                if (bEmpty&&Model==TEXT("Sniper"))
                {
                    const float BoltAlpha=DemoFPSequence::Ease(P,.59f,.67f)*(1.f-DemoFPSequence::Ease(P,.88f,.97f)); // 右手离握把操作枪栓，左手已回护木。
                    const FTransform Bolt=UAnimPoseExtensions::GetBonePose(GunPose,TEXT("bolt"),EAnimPoseSpaces::World); // 机械动画驱动的实际拉栓点。
                    RightTarget=FMath::Lerp(RightTarget,Anchor.TransformPosition(Bolt.TransformPosition(BoltOffset)),BoltAlpha)
                        +Anchor.TransformVectorNoScale(FVector(0.f,FMath::Sin(PI*BoltAlpha)*BoltReachClearance,0.f)); // 先向外绕开机匣再抓栓，端点不偏离真实抓点。
                    RightRotation=FQuat::Slerp(RightRotation,Anchor.GetRotation()*FRotator(-15,0,35).Quaternion()*Right.GetRotation(),BoltAlpha);
                }
            }
            DemoFPSequence::ArmIK(Pose,TEXT("l"),LeftTarget,LeftRotation,FVector(30.f,-18.f,-28.f)); // 与公共主图支撑手肘平面一致。
            DemoFPSequence::ArmIK(Pose,TEXT("r"),RightTarget,RightRotation,RightElbowOffset); // 右肩仍保持原位，仅改变符合骨长的肘/腕姿态。
            for (const auto& Finger:FingerDeltas) // 在腕部解算之后写局部收指，保留原骨长与所有蒙皮辅助骨。
            {
                if (!Bones.Contains(Finger.Key))
                { Controller.CloseBracket(false); UE_LOG(LogFPSDemo,Error,TEXT("FP_LEFT_CONTACT_BONE_MISSING %s"),*Finger.Key.ToString()); return false; }
                FTransform Local=UAnimPoseExtensions::GetBonePose(Pose,Finger.Key,EAnimPoseSpaces::Local); // 当前原模板局部值副本。
                Local.SetRotation((Local.GetRotation()*Finger.Value).GetNormalized());
                UAnimPoseExtensions::SetBonePose(Pose,Local,Finger.Key,EAnimPoseSpaces::Local);
            }
            UAnimPoseExtensions::SetBonePose(Pose,Anchor,TEXT("ik_hand_gun"),EAnimPoseSpaces::World);
            UAnimPoseExtensions::SetBonePose(Pose,UAnimPoseExtensions::GetBonePose(Pose,TEXT("hand_l"),EAnimPoseSpaces::World),TEXT("ik_hand_l"),EAnimPoseSpaces::World);
            UAnimPoseExtensions::SetBonePose(Pose,UAnimPoseExtensions::GetBonePose(Pose,TEXT("hand_r"),EAnimPoseSpaces::World),TEXT("ik_hand_r"),EAnimPoseSpaces::World);
            for (int32 Index=0;Index<Bones.Num();++Index) // 转为骨局部数据，子骨保留原有长度和层级。
            {
                FTransform Local=UAnimPoseExtensions::GetBonePose(Pose,Bones[Index],EAnimPoseSpaces::Local); // 单帧值副本。
                Local.NormalizeRotation(); Positions[Index].Add(FVector3f(Local.GetTranslation()));
                Rotations[Index].Add(FQuat4f(Local.GetRotation())); Scales[Index].Add(FVector3f(Local.GetScale3D()));
            }
        }
        for (int32 Index=0;Index<Bones.Num();++Index) // 通过数据控制器写轨道，压缩由引擎负责。
        {
            if (!Controller.AddBoneCurve(Bones[Index],false)||!Controller.SetBoneTrackKeys(Bones[Index],Positions[Index],Rotations[Index],Scales[Index],false))
            { Controller.CloseBracket(false); UE_LOG(LogFPSDemo,Error,TEXT("FP_AUTHOR_TRACK_FAILED %s"),*Bones[Index].ToString()); return false; }
        }
        Controller.NotifyPopulated(); Controller.CloseBracket(false); Result->PostEditChange(); Result->MarkPackageDirty();
        if (Kind!="Idle") DemoFPSequence::Montage(Result,Suffix);
        UE_LOG(LogFPSDemo,Log,TEXT("FP_SEQUENCE_READY %s duration=%.3f frames=%d bones=%d"),*Suffix,Result->GetPlayLength(),Frames,Bones.Num());
    }
    return true;
}
