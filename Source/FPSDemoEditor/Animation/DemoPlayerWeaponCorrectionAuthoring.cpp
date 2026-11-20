#include "Animation/DemoPlayerWeaponCorrectionAuthoring.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotify.h" // 签名记录Notify真实类而非复制后改变的内嵌对象路径。
#include "Animation/AnimNotifies/AnimNotifyState.h" // 状态Notify的起止时间和类均属于必须保留的结构。
#include "Animation/Skeleton.h" // 签名记录共享Skeleton引用。
#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Curves/CurveFloat.h" // 混入混出曲线引用属于原Montage配置。
#include "Debug/DemoLog.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h" // 签名采用无空白固定字段写法，避免缩进变化影响两轮比较。
#include "Serialization/JsonWriter.h" // 固定字段顺序直接写JSON，便于两轮独立进程逐字段复核。
#include "UObject/Package.h"

UAnimSequence* UDemoPlayerWeaponCorrectionAuthoring::GetFullMontageSequence(const UAnimMontage* Montage)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Montage || Montage->SlotAnimTracks.Num()!=1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1)
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_MONTAGE_SOURCE_REJECT missing or not single slot/segment")); return nullptr; }
    const FAnimSegment& Segment=Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0]; // 同步只读借用，不修改源片段或引用。
    UAnimSequence* Sequence=Cast<UAnimSequence>(Segment.GetAnimReference()); // 返回资源包拥有的引用，只允许原速完整Sequence作为制作基线。
    if (!Sequence || !FMath::IsNearlyZero(Segment.StartPos,.0001f) || !FMath::IsNearlyZero(Segment.AnimStartTime,.0001f)
        || !FMath::IsNearlyEqual(Segment.AnimEndTime,Sequence->GetPlayLength(),.001f)
        || Segment.LoopingCount!=1 || !FMath::IsNearlyEqual(Segment.AnimPlayRate,1.f,.0001f))
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_MONTAGE_SOURCE_REJECT trimmed/scaled/looped source %s"),*GetPathNameSafe(Montage)); return nullptr; }
    return Sequence;
}

FString UDemoPlayerWeaponCorrectionAuthoring::GetMontageStructureSignature(const UAnimMontage* Montage)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Montage || Montage->HasAnyFlags(RF_NeedLoad|RF_NeedPostLoad|RF_NeedPostLoadSubobjects))
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_CORRECTION_SIGNATURE_REJECT resource is missing or still needs load")); return FString(); }
    FString Signature; // 独立值返回Python；不跨帧保存借用UObject指针。
    const TSharedRef<TJsonWriter<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>> Writer=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Signature); // 同步栈内写入，固定数组/字段顺序。
    Writer->WriteObjectStart();
    Writer->WriteValue(TEXT("length_seconds"),Montage->GetPlayLength());
    Writer->WriteValue(TEXT("skeleton"),GetPathNameSafe(Montage->GetSkeleton()));
    Writer->WriteValue(TEXT("rate_scale"),Montage->RateScale);
    Writer->WriteValue(TEXT("pending_load_flags"),0); // 上方已验证三个标记均清除，报告不能把未PostLoad资产当作有效签名。
    Writer->WriteValue(TEXT("blend_in_seconds"),Montage->BlendIn.GetBlendTime());
    Writer->WriteValue(TEXT("blend_out_seconds"),Montage->BlendOut.GetBlendTime());
    Writer->WriteValue(TEXT("blend_in_option"),static_cast<int32>(Montage->BlendIn.GetBlendOption()));
    Writer->WriteValue(TEXT("blend_out_option"),static_cast<int32>(Montage->BlendOut.GetBlendOption()));
    Writer->WriteValue(TEXT("blend_in_curve"),GetPathNameSafe(Montage->BlendIn.GetCustomCurve()));
    Writer->WriteValue(TEXT("blend_out_curve"),GetPathNameSafe(Montage->BlendOut.GetCustomCurve()));
    Writer->WriteValue(TEXT("blend_out_trigger_seconds"),Montage->BlendOutTriggerTime);
    Writer->WriteValue(TEXT("auto_blend_out"),Montage->bEnableAutoBlendOut);
    Writer->WriteArrayStart(TEXT("slots"));
    for (const FSlotAnimationTrack& Slot:Montage->SlotAnimTracks) // 保持原槽顺序及每段边界，不将相同count误认为结构相同。
    {
        Writer->WriteObjectStart(); Writer->WriteValue(TEXT("name"),Slot.SlotName.ToString()); Writer->WriteArrayStart(TEXT("segments"));
        for (const FAnimSegment& Segment:Slot.AnimTrack.AnimSegments) // Sequence引用是唯一允许替换项，另由导入报告记录/检查其真实路径。
        {
            Writer->WriteObjectStart(); Writer->WriteValue(TEXT("start_seconds"),Segment.StartPos);
            Writer->WriteValue(TEXT("animation_start_seconds"),Segment.AnimStartTime); Writer->WriteValue(TEXT("animation_end_seconds"),Segment.AnimEndTime);
            Writer->WriteValue(TEXT("play_rate"),Segment.AnimPlayRate); Writer->WriteValue(TEXT("loop_count"),Segment.LoopingCount); Writer->WriteObjectEnd();
        }
        Writer->WriteArrayEnd(); Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd(); Writer->WriteArrayStart(TEXT("notifies"));
    for (const FAnimNotifyEvent& Event:Montage->Notifies) // 类使用Class路径，不能使用复制后outer变化的Notify实例路径。
    {
        Writer->WriteObjectStart(); Writer->WriteValue(TEXT("name"),Event.NotifyName.ToString());
        Writer->WriteValue(TEXT("time_seconds"),Event.GetTime()); Writer->WriteValue(TEXT("trigger_seconds"),Event.GetTriggerTime());
        Writer->WriteValue(TEXT("end_trigger_seconds"),Event.GetEndTriggerTime()); Writer->WriteValue(TEXT("duration_seconds"),Event.GetDuration());
        Writer->WriteValue(TEXT("notify_class"),Event.Notify?Event.Notify->GetClass()->GetPathName():FString());
        Writer->WriteValue(TEXT("notify_state_class"),Event.NotifyStateClass?Event.NotifyStateClass->GetClass()->GetPathName():FString());
        Writer->WriteValue(TEXT("track"),Event.TrackIndex); Writer->WriteValue(TEXT("tick_type"),static_cast<int32>(Event.MontageTickType.GetValue()));
        Writer->WriteValue(TEXT("link_method"),static_cast<int32>(Event.GetLinkMethod())); Writer->WriteValue(TEXT("slot_index"),Event.GetSlotIndex());
        Writer->WriteValue(TEXT("segment_index"),Event.GetSegmentIndex()); Writer->WriteValue(TEXT("trigger_weight"),Event.TriggerWeightThreshold);
        Writer->WriteValue(TEXT("trigger_chance"),Event.NotifyTriggerChance); Writer->WriteValue(TEXT("dedicated_server"),Event.bTriggerOnDedicatedServer);
        Writer->WriteValue(TEXT("follower"),Event.bTriggerOnFollower); Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd(); Writer->WriteArrayStart(TEXT("sections"));
    for (const FCompositeSection& Section:Montage->CompositeSections) // Section名称/时间/后继共同决定流程，不能只比数量。
    {
        Writer->WriteObjectStart(); Writer->WriteValue(TEXT("name"),Section.SectionName.ToString()); Writer->WriteValue(TEXT("time_seconds"),Section.GetTime());
        Writer->WriteValue(TEXT("next_name"),Section.NextSectionName.ToString()); Writer->WriteValue(TEXT("link_method"),static_cast<int32>(Section.GetLinkMethod()));
        Writer->WriteValue(TEXT("slot_index"),Section.GetSlotIndex()); Writer->WriteValue(TEXT("segment_index"),Section.GetSegmentIndex()); Writer->WriteObjectEnd();
    }
    Writer->WriteArrayEnd(); Writer->WriteObjectEnd(); Writer->Close();
    return Signature;
}

UAnimMontage* UDemoPlayerWeaponCorrectionAuthoring::CopyMontage(UAnimMontage* Source, UAnimSequence* Replacement, const FString& PackagePath)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    if (!Source || !Replacement || !PackagePath.StartsWith(TEXT("/Game/Player/Animations/WeaponCorrections/"))
        || !FPackageName::IsValidLongPackageName(PackagePath) || Source->GetSkeleton()!=Replacement->GetSkeleton()
        || Source->SlotAnimTracks.Num()!=1 || Source->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1)
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_CORRECTION_MONTAGE_REJECT invalid source/skeleton/path/single-segment structure")); return nullptr; }
    FAssetCompilingManager::Get().FinishAllCompilation(); // 同步离线复制，不替换仍在后台构建的数据。
    const FAnimSegment& Original=Source->SlotAnimTracks[0].AnimTrack.AnimSegments[0]; // 只读原片段，不能改它的起止/速率/循环数。
    const UAnimSequence* OriginalSequence=Cast<UAnimSequence>(Original.GetAnimReference()); // 只允许替换完整单段Sequence，不能展开未知嵌套Composite。
    if (!OriginalSequence || !FMath::IsNearlyEqual(OriginalSequence->GetPlayLength(),Replacement->GetPlayLength(),.001f)
        || Replacement->GetSamplingFrameRate().AsDecimal()<119.99 || Original.AnimStartTime<0.f || Original.AnimEndTime>Replacement->GetPlayLength()+.001f)
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_CORRECTION_MONTAGE_REJECT duration/frame-rate original=%.6f new=%.6f fps=%.3f"),OriginalSequence?OriginalSequence->GetPlayLength():-1.f,Replacement->GetPlayLength(),Replacement->GetSamplingFrameRate().AsDecimal()); return nullptr; }
    const FString SourceSignature=GetMontageStructureSignature(Source); // 复制前冻结完整槽/通知/Section/时长签名，后续不能仅比较count。
    if (SourceSignature.IsEmpty()) return nullptr;
    const FString Name=FPackageName::GetLongPackageAssetName(PackagePath); // 固定叶名，不追加会破坏配置引用的随机后缀。
    UPackage* Package=CreatePackage(*PackagePath); // 本次复制结果归Player专属包，原Weapons包保持只读。
    Package->FullyLoad();
    UAnimMontage* Existing=LoadObject<UAnimMontage>(nullptr,*(PackagePath+TEXT(".")+Name)); // 重跑时复用资源对象身份。
    FObjectDuplicationParameters Parameters(Source,Package); // 引擎完整序列化复制，包括Notify内嵌对象，不手工漏拷属性。
    Parameters.DestName=FName(*Name); Parameters.ApplyFlags=RF_Public|RF_Standalone;
    if (Existing) { Existing->Modify(); Parameters.DuplicationSeed.Add(Source,Existing); } // 引擎支持以现有根为复制种子；引用不失效，不删文件。
    UAnimMontage* Result=Cast<UAnimMontage>(StaticDuplicateObjectEx(Parameters)); // 同步游戏线程复制，失败不配置任何蓝图。
    if (!Result) { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_CORRECTION_MONTAGE_REJECT duplication failed")); return nullptr; }
    const EObjectFlags PendingLoadFlags=RF_NeedLoad|RF_NeedPostLoad|RF_NeedPostLoadSubobjects; // 仅检查生命周期标记，绝不手工ClearFlags伪造已初始化状态。
    const uint32 BeforePostLoad=static_cast<uint32>(Result->GetFlags()&PendingLoadFlags); // 记录首次复制与DuplicationSeed复用根的实际差异。
    Result->ConditionalPostLoad(); // UE5.4对DuplicationSeed根跳过自动PostLoad；换段前完成真实Montage加载/链接修复。
    if (!ensureAlwaysMsgf(!Result->HasAnyFlags(PendingLoadFlags),TEXT("Correction montage still needs load after ConditionalPostLoad: %s"),*Result->GetPathName()))
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_CORRECTION_MONTAGE_REJECT pending load flags=%u"),static_cast<uint32>(Result->GetFlags()&PendingLoadFlags)); return nullptr; }
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_CORRECTION_MONTAGE_POSTLOAD existing=%d before=%u after=0 asset=%s"),Existing?1:0,BeforePostLoad,*Result->GetPathName());
    if (!Existing) FAssetRegistryModule::AssetCreated(Result);
    Result->Modify(); Result->SlotAnimTracks[0].AnimTrack.AnimSegments[0].SetAnimReference(Replacement,false); // 只替换引用，bInitialize=false保留片段所有时间参数。
    Result->PostEditChange(); Result->MarkPackageDirty();
    if (Result->SlotAnimTracks.Num()!=1 || Result->SlotAnimTracks[0].AnimTrack.AnimSegments.Num()!=1
        || Result->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference()!=Replacement
        || GetMontageStructureSignature(Result)!=SourceSignature)
    { UE_LOG(LogFPSDemo,Error,TEXT("PLAYER_CORRECTION_MONTAGE_REJECT structure changed during duplication")); return nullptr; }
    UE_LOG(LogFPSDemo,Display,TEXT("PLAYER_CORRECTION_MONTAGE source=%s result=%s sequence=%s seconds=%.6f"),*Source->GetPathName(),*Result->GetPathName(),*Replacement->GetPathName(),Result->GetPlayLength());
    return Result;
}
