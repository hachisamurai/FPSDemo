// 职责：实现R05 120Hz/R07 240Hz候选采样同步；固定路径/骨架，复用UE原生通知，调用方负责源门、保存与片长/键间回读。
#include "Animation/DemoLonggunElbowAuthoring.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Debug/DemoLog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "MovieScene.h"
#include "MovieSceneSequence.h"
#include "MovieSceneTimeHelpers.h" // 公开帧时钟迁移保留未编辑通道；不依赖AnimationData私有类或修改引擎。
#include "MovieSceneSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"

namespace
{
    /** 仅R07固定240Hz，历史R05仍120Hz；不提供任意采样率或改变真实秒数的游戏配置入口。 */
    int32 ElbowCandidateRate(const UAnimSequence* Sequence)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ElbowCandidateRate"));
        return Sequence && Sequence->GetOutermost()->GetName().StartsWith(TEXT("/Game/Player/Candidates/GunHoldElbow20261109/R07JoinedComplete/"))?240:120;
    }
    /** 只准本任务历史R05或待独立准入的R07隔离序列；不允许正式目录/任意外部资产由本接口改写。 */
    bool ElbowCandidatePath(const UAnimSequence* Sequence)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ElbowCandidatePath"));
        if(!Sequence)return false;
        const FString Package=Sequence->GetOutermost()->GetName(); // 同步本次借用，不保活或跨线程保存。
        return Package.StartsWith(TEXT("/Game/Player/Candidates/GunHoldElbow20261109/R05ActualAttachment/Animations/A_Elbow_"))
            || Package.StartsWith(TEXT("/Game/Player/Candidates/GunHoldElbow20261109/R07JoinedComplete/Animations/A_Elbow_"));
    }
    /** 已审固定采样JSON中单个局部Q回读；只接受有限非零单位旋转，不执行外部代码。 */
    bool ElbowReadQuat(const TArray<TSharedPtr<FJsonValue>>& Rows,int32 Index,FQuat& Out)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] ElbowReadQuat"));
        const TSharedPtr<FJsonObject> Row=Rows.IsValidIndex(Index)?Rows[Index]->AsObject():nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Q=nullptr;
        if(!Row || !Row->TryGetArrayField(TEXT("q"),Q) || Q->Num()!=4)return false;
        Out=FQuat((*Q)[0]->AsNumber(),(*Q)[1]->AsNumber(),(*Q)[2]->AsNumber(),(*Q)[3]->AsNumber());
        if(Out.ContainsNaN() || Out.SizeSquared()<.99 || Out.SizeSquared()>1.01)return false;
        Out.Normalize();return true;
    }
}

int32 UDemoLonggunElbowAuthoring::PrepareSampling(UAnimSequence* Sequence)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs sequence=%s"),__FUNCTION__,*GetNameSafe(Sequence)); // 静态Editor接口无this，采用与既有作者桥相同的DemoLog类别入口埋点。
    const int32 TargetRate=ElbowCandidateRate(Sequence); // 由固定候选族决定，不接受未核任意帧率。
    if(!IsInGameThread() || !ElbowCandidatePath(Sequence) || !Sequence->GetSkeleton() || Sequence->GetSkeleton()->GetPathName()!=TEXT("/Game/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin") || !Sequence->GetDataModel() || Sequence->GetDataModel()->GetFrameRate()!=FFrameRate(TargetRate,1) || Sequence->AdditiveAnimType!=AAT_None)
    {UE_LOG(LogFPSDemo,Error,TEXT("GUN_ELBOW_SAMPLING_REJECT thread/path/skeleton/model/rate/absolute"));return 0;}
    Sequence->bAllowFrameStripping=false; // 保留已核120/240Hz曲率；服务器动作片长/事件时钟不变。
    Sequence->GetController().NotifyPopulated(); // 5.4仅C++公开；bracket中同步压缩目标，避免新源再次按旧60Hz采样。
    const int32 Rate=FMath::RoundToInt(Sequence->GetSamplingFrameRate().AsDecimal()); // 回读实际目标，不能只记录写入意图。
    UE_LOG(LogFPSDemo,Log,TEXT("GUN_ELBOW_SAMPLING_PREPARED sequence=%s target_hz=%d"),*Sequence->GetPathName(),Rate);
    return Rate;
}

FString UDemoLonggunElbowAuthoring::BakeReviewedKeys(UAnimSequence* Candidate,UAnimSequence* Source,const FString& TracksJson)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs candidate=%s source=%s"),__FUNCTION__,*GetNameSafe(Candidate),*GetNameSafe(Source)); // 静态函数无this，沿同一DemoLog类别。
    TSharedPtr<FJsonObject> Data;const TSharedRef<TJsonReader<>> Reader=TJsonReaderFactory<>::Create(TracksJson);
    if(!IsInGameThread() || !ElbowCandidatePath(Candidate) || !Source || Candidate==Source || !Source->GetDataModel() || !Candidate->GetDataModel() || Candidate->GetSkeleton()!=Source->GetSkeleton() || !FJsonSerializer::Deserialize(Reader,Data) || !Data || Data->GetStringField(TEXT("source_asset"))!=Source->GetPathName())
    {UE_LOG(LogFPSDemo,Error,TEXT("GUN_ELBOW_KEYS_REJECT identity/model/json"));return FString();}
    const TSharedPtr<FJsonObject> Tracks=Data->GetObjectField(TEXT("tracks")); // 调用方源门已固定文件哈希，这里仍核三轨及采样布局。
    const IAnimationDataModel* Model=Source->GetDataModel();const int32 TargetRate=ElbowCandidateRate(Candidate);const FFrameRate Rate=Model->GetFrameRate(),Target(TargetRate,1); // 真实秒数由Rate转换保留，R07源Q已独立从保存Action导出240Hz。
    UE_LOG(LogFPSDemo,Log,TEXT("GUN_ELBOW_MODEL_TYPES source=%s candidate=%s distinct=%d"),*Source->GetDataModelInterface().GetObject()->GetClass()->GetPathName(),*Candidate->GetDataModelInterface().GetObject()->GetClass()->GetPathName(),Source->GetDataModelInterface().GetObject()!=Candidate->GetDataModelInterface().GetObject());
    const FFrameTime Last=FFrameRate::TransformTime(FFrameTime(Model->GetNumberOfFrames()),Rate,Target);const int32 Count=Last.GetFrame().Value;
    if(!Tracks || Tracks->Values.Num()!=3 || Data->GetIntegerField(TEXT("fps"))!=TargetRate || !Rate.IsMultipleOf(Target) || !FMath::IsNearlyZero(Last.GetSubFrame()) || Count<1 || Data->GetIntegerField(TEXT("sample_count"))!=Count+1) // 本地IsMultipleOf为Other是自身整数倍：60.IsMultipleOf(120/240)。
    {UE_LOG(LogFPSDemo,Error,TEXT("GUN_ELBOW_KEYS_REJECT track count/source sampling source=%d/%d target=%d frames=%d json_keys=%d subframe=%.9f"),Rate.Numerator,Rate.Denominator,TargetRate,Count,Data->GetIntegerField(TEXT("sample_count")),Last.GetSubFrame());return FString();}
    TArray<FName> Names;Model->GetBoneTrackNames(Names); // 真正原曲线，不能用重定向/求值后Pose反写原数据。
    const TSet<FName> Required={TEXT("upperarm_l"),TEXT("lowerarm_l"),TEXT("hand_l")};
    const bool bNoEdit=Data->HasTypedField<EJson::Boolean>(TEXT("no_edit_control")) && Data->GetBoolField(TEXT("no_edit_control")); // 仅未保存探针允许无编辑转换对照，不能生成冒充修复的正式候选。
    if(bNoEdit && !Candidate->GetName().EndsWith(TEXT("UnsavedProbe"))){UE_LOG(LogFPSDemo,Error,TEXT("GUN_ELBOW_KEYS_REJECT no-edit control must remain unsaved probe"));return FString();}
    const TSet<FName> Edited=bNoEdit?TSet<FName>{}:Required;
    for(const FName Name:Required)if(!Names.Contains(Name) || !Tracks->HasTypedField<EJson::Array>(Name.ToString())){UE_LOG(LogFPSDemo,Error,TEXT("GUN_ELBOW_KEYS_REJECT missing reviewed track %s"),*Name.ToString());return FString();}
    if(Model->GetCurveData().FloatCurves.Num()!=0 || Model->GetCurveData().TransformCurves.Num()!=0){UE_LOG(LogFPSDemo,Error,TEXT("GUN_ELBOW_KEYS_REJECT non-bone curves require separate timing review"));return FString();}
    UMovieSceneSequence* SequencerModel=Cast<UMovieSceneSequence>(Candidate->GetDataModelInterface().GetObject());UMovieScene* Scene=SequencerModel?SequencerModel->GetMovieScene():nullptr; // 已实核当前正式模型为AnimationSequencerDataModel。
    if(!Scene || Scene->GetTickResolution()!=Rate){UE_LOG(LogFPSDemo,Error,TEXT("GUN_ELBOW_KEYS_REJECT expected Sequencer model/tick contract"));return FString();}
    IAnimationDataController& Controller=Candidate->GetController();Controller.OpenBracket(NSLOCTEXT("GunElbow","RawKeys","Reviewed longgun Q with exact original raw-key preservation"),false);
    const TRange<FFrameNumber> OriginalRange=Scene->GetPlaybackRange(); // 通用迁移会连播放范围一起倍频，而Controller按旧显示率再次换算；临时恢复旧范围避免双倍换算。
    UE::MovieScene::TimeHelpers::MigrateFrameTimes(Rate,Target,Scene);Scene->SetPlaybackRange(OriginalRange); // 原通道值与插值保留，键时刻整数倍迁移，不把所有Quat重编码成Euler。
    Controller.SetFrameRate(Target,false);bool bOK=PrepareSampling(Candidate)==TargetRate;
    // 该实际模型在子帧Blend两侧Transform；只搬Euler时间会改变中间帧。先直接保存原整数Euler值，再只插入原运行曲线的新半键。
    // 原通道可能只有Default，UpdateBoneCurveKeys会ClearDefault；显式原整数键防止新半键的Euler舍入反过来改变原键。
    for(UMovieSceneSection* Section:Scene->GetAllSections())for(FMovieSceneFloatChannel* Channel:Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>())
        for(int32 Frame=0;Frame<Model->GetNumberOfKeys();++Frame)
        {
            const FFrameNumber Time=FFrameRate::TransformTime(FFrameTime(Frame),Rate,Target).GetFrame();float Value=0.f;
            if(Channel->GetData().FindKey(Time)==INDEX_NONE && Channel->Evaluate(FFrameTime(Time),Value))Channel->AddLinearKey(Time,Value); // 只补缺失原时刻，原已有键/默认物理值不经Quat往返。
        }
    for(const FName Name:Names)if(!Edited.Contains(Name))for(int32 Frame=0;Frame<=Count && bOK;++Frame)
    {
        const FFrameTime Time=FFrameRate::TransformTime(FFrameTime(Frame),Target,Rate);if(Time.GetSubFrame()==0.f)continue;
        const FTransform Value=Model->EvaluateBoneTrackTransform(Name,Time,Source->Interpolation); // 原60Hz运行的Transform混合，不直接取Euler中间值。
        bOK=Controller.UpdateBoneTrackKeys(Name,FInt32Range(Frame,Frame+1),TArray<FVector3f>{FVector3f(Value.GetTranslation())},TArray<FQuat4f>{FQuat4f(Value.GetRotation())},TArray<FVector3f>{FVector3f(Value.GetScale3D())},false); // 只插新半键，原整数键不会被Setter重编码。
    }
    for(const FName Name:Names)
    {
        if(!Edited.Contains(Name))continue; // 原未编辑Euler/位置/缩放通道只迁移时间，禁止重写。
        TArray<FVector3f> Positions,Scales;TArray<FQuat4f> Rotations;Positions.Reserve(Count+1);Scales.Reserve(Count+1);Rotations.Reserve(Count+1); // 原动画轨为float32，显式按存储格式写入，不经Pose重定向/分解。
        for(int32 Frame=0;Frame<=Count && bOK;++Frame)
        {
            const FFrameTime SourceTime=FFrameRate::TransformTime(FFrameTime(Frame),Target,Rate); // 精确整数/半键，避免float片长比例带入二次时移。
            FTransform Value=SourceTime.GetSubFrame()==0.f?Model->GetBoneTrackTransform(Name,SourceTime.GetFrame()):Model->EvaluateBoneTrackTransform(Name,SourceTime,Source->Interpolation);
            if(Edited.Contains(Name))
            {
                const TArray<TSharedPtr<FJsonValue>>& Rows=Tracks->GetArrayField(Name.ToString());FQuat Q;
                bOK=ElbowReadQuat(Rows,Frame,Q);if(bOK)Value.SetRotation(Q); // 已审保存Action的120/240Hz回读样本，按同键索引，不再次解算或改原T/S。
            }
            Positions.Add(FVector3f(Value.GetTranslation()));Rotations.Add(FQuat4f(Value.GetRotation()));Scales.Add(FVector3f(Value.GetScale3D()));
        }
        bOK=bOK && Controller.SetBoneTrackKeys(Name,Positions,Rotations,Scales,false);
        if(!bOK)break;
    }
    Controller.CloseBracket(false);
    if(!bOK){UE_LOG(LogFPSDemo,Error,TEXT("GUN_ELBOW_KEYS_REJECT raw key write"));return FString();}
    double PositionError=0.,RotationError=0.,ScaleError=0.,EditedPositionError=0.,EditedScaleError=0.; // 未编辑原键零差；三个指定Q对应的T/S保持最初1e-5原有精度门。
    FName WitnessBone=NAME_None;int32 WitnessFrame=INDEX_NONE;FQuat WitnessA=FQuat::Identity,WitnessB=FQuat::Identity,WitnessRaw=FQuat::Identity; // 拒绝时记录最大原整数键差的实际分量/存储转换，避免猜归一化或时钟原因。
    const IAnimationDataModel* Result=Candidate->GetDataModel();
    for(const FName Name:Names)for(int32 Frame=0;Frame<Model->GetNumberOfKeys();++Frame)
    {
        const FTransform Raw=Model->GetBoneTrackTransform(Name,FFrameNumber(Frame));const FTransform A=Raw;
        const int32 NewFrame=FFrameRate::TransformTime(FFrameTime(Frame),Rate,Target).GetFrame().Value;const FTransform B=Result->GetBoneTrackTransform(Name,FFrameNumber(NewFrame)); // 比较原整数键，不把旧模型Quat混合误作真实Euler半键。
        if(Edited.Contains(Name)){EditedPositionError=FMath::Max(EditedPositionError,(A.GetTranslation()-B.GetTranslation()).GetAbsMax());EditedScaleError=FMath::Max(EditedScaleError,(A.GetScale3D()-B.GetScale3D()).GetAbsMax());}
        else{PositionError=FMath::Max(PositionError,(A.GetTranslation()-B.GetTranslation()).GetAbsMax());ScaleError=FMath::Max(ScaleError,(A.GetScale3D()-B.GetScale3D()).GetAbsMax());}
        if(!Edited.Contains(Name))
        {
            const FQuat D=A.GetRotation()-B.GetRotation(),S=A.GetRotation()+B.GetRotation(); // 同时接受四元数正负双覆盖，分量值仍须逐值相同。
            const double Minus=FMath::Max(FMath::Max(FMath::Abs(D.X),FMath::Abs(D.Y)),FMath::Max(FMath::Abs(D.Z),FMath::Abs(D.W))),Plus=FMath::Max(FMath::Max(FMath::Abs(S.X),FMath::Abs(S.Y)),FMath::Max(FMath::Abs(S.Z),FMath::Abs(S.W)));
            const double Error=FMath::Min(Minus,Plus);
            if(Error>RotationError){RotationError=Error;WitnessBone=Name;WitnessFrame=NewFrame;WitnessA=A.GetRotation();WitnessB=B.GetRotation();WitnessRaw=Raw.GetRotation();}
        }
    }
    if(PositionError!=0. || RotationError!=0. || ScaleError!=0. || EditedPositionError>1e-5 || EditedScaleError>1e-5 || Result->GetNumberOfFrames()!=Count || FMath::Abs(Result->GetPlayLength()-Model->GetPlayLength())>1e-9)
    {UE_LOG(LogFPSDemo,Error,TEXT("GUN_ELBOW_KEYS_REJECT original raw key preservation p=%.9f original_q=%.9f s=%.9f edited_p=%.9f edited_s=%.9f bone=%s target_frame=%d A=[%.12f,%.12f,%.12f,%.12f] B=[%.12f,%.12f,%.12f,%.12f] source_raw=[%.12f,%.12f,%.12f,%.12f]"),PositionError,RotationError,ScaleError,EditedPositionError,EditedScaleError,*WitnessBone.ToString(),WitnessFrame,WitnessA.X,WitnessA.Y,WitnessA.Z,WitnessA.W,WitnessB.X,WitnessB.Y,WitnessB.Z,WitnessB.W,WitnessRaw.X,WitnessRaw.Y,WitnessRaw.Z,WitnessRaw.W);return FString();}
    TSharedPtr<FJsonObject> Report=MakeShared<FJsonObject>();Report->SetNumberField(TEXT("source_frames"),Model->GetNumberOfFrames());Report->SetNumberField(TEXT("target_frames"),Count);Report->SetNumberField(TEXT("source_hz"),Rate.AsDecimal());Report->SetNumberField(TEXT("target_hz"),TargetRate);Report->SetNumberField(TEXT("duration_seconds_double"),Model->GetPlayLength());Report->SetNumberField(TEXT("original_key_translation_delta_cm"),PositionError);Report->SetNumberField(TEXT("original_key_quaternion_component_delta"),RotationError);Report->SetNumberField(TEXT("original_key_scale_delta"),ScaleError);Report->SetBoolField(TEXT("all_raw_original_and_new_halfkeys_checked"),true);
    Report->SetBoolField(TEXT("non_edited_euler_channels_time_migrated_not_rewritten"),true);Report->SetNumberField(TEXT("edited_track_translation_delta_cm"),EditedPositionError);Report->SetNumberField(TEXT("edited_track_scale_delta"),EditedScaleError);
    Report->SetBoolField(TEXT("original_integer_euler_keys_seeded_unchanged"),true);Report->SetBoolField(TEXT("new_halfkeys_from_original_transform_interpolation"),true);Report->SetBoolField(TEXT("no_edit_conversion_control"),bNoEdit);Report->SetNumberField(TEXT("non_bone_float_curves"),Model->GetCurveData().FloatCurves.Num());Report->SetNumberField(TEXT("non_bone_transform_curves"),Model->GetCurveData().TransformCurves.Num());
    Report->SetBoolField(TEXT("all_raw_original_and_new_halfkeys_checked"),false); // 原整数键由此接口核，半键真实Euler求值由Python逐点回读，不能用旧Quat混合冒充。
    FString Json;const TSharedRef<TJsonWriter<>> Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(Report.ToSharedRef(),Writer);
    UE_LOG(LogFPSDemo,Log,TEXT("GUN_ELBOW_RAW_KEYS_PRESERVED %s"),*Json);return Json;
}
