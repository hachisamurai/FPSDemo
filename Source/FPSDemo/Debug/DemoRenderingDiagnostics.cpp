// 职责：开发/Editor的一次性只读渲染体检命令；使用 Demo.Render.Check，在下一正常本机主View记录PP栈/曝光/光源热点。
// 依赖：Engine公开SceneViewExtension、PP Volume、Light API；不含Renderer私有头、不加渲染pass、不写CVar/ShowFlags/资产。
// Shipping完全不编译命令/扩展。曝光为公开getter的最后完成GPU回读；GPU页/SurfaceCache占用无公开getter时明确不可用。
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#include "Camera/PlayerCameraManager.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "World/Rooms/DemoRoomAnchor.h"
#include "World/Rooms/DemoRoomStreamingSubsystem.h"
#include "Debug/DemoStaticLog.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "Misc/ScopeLock.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "UnrealClient.h"

namespace
{
    /** PP输出只记录与本次重构有关的值及override；Source/Settings同步借用，不修改任何PP。 */
    void PrintSettings(const TCHAR* Source,const FPostProcessSettings& Settings)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_PP source=%s method=%d min=%g max=%g bias=%g speeds=[%g,%g] exposure_override=[%d,%d,%d,%d,%d,%d] local=[%g,%g,%g] bloom=[%g,%g] vignette=%g temperature=%g lumen=[scene:%g gather:%g reflection:%g trace_cm:%g leak:%g]"),
            Source,static_cast<int32>(Settings.AutoExposureMethod),Settings.AutoExposureMinBrightness,Settings.AutoExposureMaxBrightness,
            Settings.AutoExposureBias,Settings.AutoExposureSpeedUp,Settings.AutoExposureSpeedDown,
            Settings.bOverride_AutoExposureMethod,Settings.bOverride_AutoExposureMinBrightness,Settings.bOverride_AutoExposureMaxBrightness,
            Settings.bOverride_AutoExposureBias,Settings.bOverride_AutoExposureSpeedUp,Settings.bOverride_AutoExposureSpeedDown,
            Settings.LocalExposureHighlightContrastScale,Settings.LocalExposureShadowContrastScale,Settings.LocalExposureDetailStrength,
            Settings.BloomIntensity,Settings.BloomThreshold,Settings.VignetteIntensity,Settings.WhiteTemp,
            Settings.LumenSceneLightingQuality,Settings.LumenFinalGatherQuality,Settings.LumenReflectionQuality,Settings.LumenMaxTraceDistance,Settings.LumenSkylightLeaking);
        for(const FWeightedBlendable& Blendable:Settings.WeightedBlendables.Array) // 当前PP所声明的混合资产，不推断其内部shader或最终执行pass。
            UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_BLENDABLE source=%s object=%s weight=%g"),Source,*GetPathNameSafe(Blendable.Object),Blendable.Weight);
    }

    /** 只读CVar字符串和设置来源；Name来自下方编译期白名单，不执行console输入。 */
    void PrintCVar(const TCHAR* Name)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        const IConsoleVariable* Variable=IConsoleManager::Get().FindConsoleVariable(Name); // ConsoleManager拥有，同步只读借用。
        if(!Variable) { UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_CONFIG name=%s status=unavailable"),Name); return; }
        UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_CONFIG name=%s value=%s flags=0x%x kind=configuration_not_GPU_measurement"),Name,*Variable->GetString(),static_cast<uint32>(Variable->GetFlags()));
    }

    /** GT读取同一View位置的World有序PP Volume及Camera缓存；不调用OverridePostProcessSettings，不重算/替代最终混合。 */
    void PrintPostProcessStack(UWorld* World,const FSceneView& View)
    {
        DEMO_LOG_STATIC_CALL(World);
        const FVector Origin=View.ViewMatrices.GetViewOrigin(); // 真正主View世界cm，非命令触发时可能过期的Pawn位置。
        int32 StackIndex=0; // World数组的实际低至高优先级顺序，包括本机Transient组件。
        UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_STACK origin=%s world=%s frame=%llu order=world_volumes_then_camera_base_then_main_camera_then_camera_override finalPP_is_authoritative"),*Origin.ToString(),*GetNameSafe(World),View.Family?View.Family->FrameCounter:0ull);
        for(IInterface_PostProcessVolume* Volume:World->PostProcessVolumes) // World只在GT当前帧借用；不保存接口/UObject到RT。
        {
            if(!Volume) continue;
            const FPostProcessVolumeProperties Properties=Volume->GetProperties(); // 本帧enabled/bounds/weight/priority快照。
            float Distance=0.f; // 形状至视点距离cm；负值按引擎含义视为不可参与。
            float Weight=Properties.bIsEnabled?FMath::Clamp(Properties.BlendWeight,0.f,1.f):0.f; // 当前空间贡献，不等于单字段最终影响。
            if(!Properties.bIsUnbound)
            {
                Volume->EncompassesPoint(Origin,0.f,&Distance);
                if(Distance<0.f||Distance>Properties.BlendRadius) Weight=0.f;
                else if(Properties.BlendRadius>=1.f) Weight*=1.f-Distance/Properties.BlendRadius;
            }
            UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_VOLUME index=%d name=%s enabled=%d unbound=%d priority=%g radius_cm=%g distance_cm=%g effective_weight=%g"),
                StackIndex++,*Volume->GetDebugName(),Properties.bIsEnabled,Properties.bIsUnbound,Properties.Priority,Properties.BlendRadius,Distance,Weight);
            if(Weight>0.f&&Properties.Settings) PrintSettings(*Volume->GetDebugName(),*Properties.Settings);
        }
        bool bFoundCamera=false; // 玩家索引/当前ViewTarget双重身份匹配；观战同样使用真实CameraManager。
        for(FConstPlayerControllerIterator It=World->GetPlayerControllerIterator();It;++It)
        {
            const APlayerController* Controller=It->Get(); // GT当前World拥有的弱迭代引用。
            if(!Controller||!Controller->IsLocalController()||Controller->GetViewTarget()!=View.ViewActor||!Controller->PlayerCameraManager) continue;
            const ULocalPlayer* LocalPlayer=Controller->GetLocalPlayer(); // 5.4 LocalPlayer把ControllerId写入View.PlayerIndex，不能按数组下标猜测分屏身份。
            if(!LocalPlayer||LocalPlayer->GetControllerId()!=View.PlayerIndex) continue;
            const APlayerCameraManager* Camera=Controller->PlayerCameraManager; // GT当前同步借用；不改相机状态。
            const FMinimalViewInfo& Info=Camera->GetCameraCacheView(); // 已有真正相机缓存；不触发Camera重新计算。
            const TArray<FPostProcessSettings>* CachedSettings=nullptr; // Camera拥有，作用域仅本函数。
            const TArray<float>* CachedWeights=nullptr; // 与CachedSettings平行，所有访问验证长度。
            const TArray<EViewTargetBlendOrder>* CachedOrders=nullptr; // Base/Override在LocalPlayer中的应用阶段。
            Camera->GetCachedPostProcessBlends(CachedSettings,CachedWeights,CachedOrders);
            UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_CAMERA controller=%s target=%s main_weight=%g cached_blends=%d"),*GetNameSafe(Controller),*GetNameSafe(View.ViewActor),Info.PostProcessBlendWeight,CachedSettings?CachedSettings->Num():0);
            if(CachedSettings&&CachedWeights&&CachedOrders)
                for(int32 Index=0;Index<CachedSettings->Num();++Index) // 基/覆盖层分别带order输出，最终以真实View为准。
                    if(CachedWeights->IsValidIndex(Index)&&CachedOrders->IsValidIndex(Index))
                    {
                        const FString Label=FString::Printf(TEXT("CameraCache[%d] order=%d weight=%g"),Index,static_cast<int32>((*CachedOrders)[Index]),(*CachedWeights)[Index]); // 本次输出名称，非存档ID。
                        PrintSettings(*Label,(*CachedSettings)[Index]);
                    }
            PrintSettings(TEXT("MainCamera"),Info.PostProcessSettings); bFoundCamera=true;
        }
        if(!bFoundCamera) UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_CAMERA status=unavailable reason=no_matching_local_ViewTarget"));
        PrintSettings(TEXT("ActualFinalView"),View.FinalPostProcessSettings);
        UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_STACK_LIMIT custom_blendable_internals_and_other_view_extension_edits_have_no_public_provenance_trace; finalPP_and_listed_sources_only; no_VisualizePostProcessStack_flag_changed"));
    }

    /** GT一次查询实际Anchor组件/流送身份，不加载Profile或推算当前曝光；Hub原Volume由PP栈单列。 */
    void PrintRoomAtmospheres(UWorld* World)
    {
        DEMO_LOG_STATIC_CALL(World);
        const UDemoRoomStreamingSubsystem* Streaming=World->GetSubsystem<UDemoRoomStreamingSubsystem>(); // 同World只读服务，不触发加载/提交。
        for (TActorIterator<ADemoRoomAnchor> It(World); It; ++It)
        {
            FName RoomId; FTransform Slot; bool bCommitted=false; // committed含退役保留，不冒称当前Gameplay Active。
            const bool bKnown=Streaming && Streaming->GetRoomRenderContext(It->GetLevel(),RoomId,Slot,bCommitted);
            TInlineComponentArray<UPostProcessComponent*> Volumes; It->GetComponents(Volumes); // 只枚举当前已存在的自有瞬态PP。
            UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_ROOM anchor=%s room=%s known=%d committed_or_retiring=%d local_pp_components=%d"),
                *It->GetPathName(),*RoomId.ToString(),bKnown,bCommitted,Volumes.Num());
            for (const UPostProcessComponent* Volume:Volumes)
                if (Volume && Volume->IsRegistered())
                    UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_ROOM_PP path=%s enabled=%d unbound=%d priority=%g parent_shape=%s"),
                        *Volume->GetPathName(),Volume->bEnabled,Volume->bUnbound,Volume->Priority,*GetPathNameSafe(Volume->GetAttachParent()));
        }
    }

    /** CPU粗筛光源/投影需求及采样热点；World/View只在GT借用；视锥相交不声称GPU可见/实际投影/遮挡剔除结果。 */
    void PrintLightDemand(UWorld* World,const FSceneView& View)
    {
        DEMO_LOG_STATIC_CALL(World);
        TArray<const ULocalLightComponent*> LocalLights; // 本调用栈内借用的视锥相关局部光；最多512，超出明确标截断。
        TArray<FVector> Samples; // 最多257个候选cm点：主相机及视锥内静态网格bounds中心；不保证覆盖真实最大热点。
        Samples.Add(View.ViewMatrices.GetViewOrigin());
        int32 Total=0,Local=0,Directional=0,ShadowLocal=0,DynamicShadowLocal=0,VolumetricLocal=0,ShadowedVolumetricLocal=0,FaceUpperBound=0; // 全World有效灯及当前视锥的CPU需求计数；Face不含方向光clipmap。
        bool bTruncated=false; // CPU诊断工作上限，不影响渲染对象或游戏灯光。
        for(TActorIterator<AActor> It(World);It;++It)
        {
            TInlineComponentArray<ULightComponent*> Lights; It->GetComponents(Lights); // 仅当前Actor已存在组件。
            for(const ULightComponent* Light:Lights)
            {
                if(!Light||!Light->IsRegistered()||!Light->IsVisible()||!Light->bAffectsWorld||Light->Intensity<=0.f) continue;
                ++Total;
                if(Light->GetLightType()==LightType_Directional) { ++Directional; continue; }
                const ULocalLightComponent* LocalLight=Cast<ULocalLightComponent>(Light); // SkyLight非ULightComponent，此处不算局部光。
                if(!LocalLight) continue;
                const FSphere Sphere=LocalLight->GetBoundingSphere(); // 点/聚光/矩形的保守世界球，cm。
                if(!View.ViewFrustum.IntersectSphere(Sphere.Center,Sphere.W)) continue;
                ++Local;
                if(LocalLight->CastShadows) { ++ShadowLocal; FaceUpperBound+=LocalLight->GetLightType()==LightType_Point?6:1; }
                if(LocalLight->CastShadows&&LocalLight->CastDynamicShadows&&LocalLight->Mobility!=EComponentMobility::Static) ++DynamicShadowLocal;
                if(LocalLight->VolumetricScatteringIntensity>0.f) { ++VolumetricLocal; if(LocalLight->CastShadows&&LocalLight->bCastVolumetricShadow) ++ShadowedVolumetricLocal; }
                if(LocalLights.Num()<512) LocalLights.Add(LocalLight); else bTruncated=true;
                if(Local<=64) UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_LIGHT name=%s type=%d mobility=%d origin=%s radius_cm=%g shadows=%d dynamic=%d volume_scatter=%g volume_shadow=%d"),
                    *LocalLight->GetPathName(),static_cast<int32>(LocalLight->GetLightType()),static_cast<int32>(LocalLight->Mobility),*LocalLight->GetComponentLocation().ToString(),Sphere.W,
                    LocalLight->CastShadows,LocalLight->CastDynamicShadows,LocalLight->VolumetricScatteringIntensity,LocalLight->bCastVolumetricShadow);
            }
            if(Samples.Num()>=257) continue;
            TInlineComponentArray<UStaticMeshComponent*> Meshes; It->GetComponents(Meshes); // 静态网格热点候选；不遍历顶点/重建bounds。
            for(const UStaticMeshComponent* Mesh:Meshes)
            {
                if(Samples.Num()>=257) break;
                if(Mesh&&Mesh->IsRegistered()&&Mesh->IsVisible()&&View.ViewFrustum.IntersectBox(Mesh->Bounds.Origin,Mesh->Bounds.BoxExtent)) Samples.Add(Mesh->Bounds.Origin);
            }
        }
        int32 MaxOverlap=0,MaxShadowOverlap=0; // 当前有限采样的两个独立最大值，不可称全场景精确极大值。
        FVector OverlapPoint=Samples[0],ShadowPoint=Samples[0]; // 两类最大采样点cm，用于定位作者灯范围。
        for(const FVector& Sample:Samples)
        {
            int32 Overlap=0,ShadowOverlap=0; // 当前点的CPU light-bounds相交计数；无几何遮挡或lighting-channel判断。
            const FBoxSphereBounds PointBounds(Sample,FVector(1.),1.); // 1cm微小探针，不发送场景碰撞/渲染请求。
            for(const ULocalLightComponent* Light:LocalLights)
                if(Light->AffectsBounds(PointBounds)) { ++Overlap; if(Light->CastShadows) ++ShadowOverlap; }
            if(Overlap>MaxOverlap) { MaxOverlap=Overlap; OverlapPoint=Sample; }
            if(ShadowOverlap>MaxShadowOverlap) { MaxShadowOverlap=ShadowOverlap; ShadowPoint=Sample; }
        }
        UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_LIGHT_SUMMARY world_effective_lights=%d world_directional=%d frustum_local=%d cast_shadows_local=%d dynamic_shadow_local=%d volumetric_local=%d shadowed_volumetric_local=%d local_shadow_face_upper_bound=%d actual_GPU_projection_count=unavailable list_limit=64 hotspot_light_limit=512 truncated=%d"),
            Total,Directional,Local,ShadowLocal,DynamicShadowLocal,VolumetricLocal,ShadowedVolumetricLocal,FaceUpperBound,bTruncated);
        UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_HOTSPOT samples=%d local_overlap=%d at_cm=%s shadow_overlap=%d at_cm=%s kind=CPU_sampled_bounds_ignores_occlusion_channels not_exact_global_maximum"),Samples.Num(),MaxOverlap,*OverlapPoint.ToString(),MaxShadowOverlap,*ShadowPoint.ToString());
    }

    /** 一次命令的线程安全状态；无UObject所有权。GT填入主View身份，RT只比地址，不解引用任何UObject。 */
    struct FRenderCheckState
    {
        FCriticalSection Mutex; // 保护下面GT/RT一次交接字段；日志在解锁后输出。
        bool bGTDone=false; // 找到真实主View并输出GT检查后置true；防止逐帧重复。
        bool bRTDone=false; // 只有同一Family主View曝光读回后置true；无GPU/无后处理时保留未完成事实。
        uint64 Frame=0; // GT选定Family观察帧，不是曝光GPU生产帧。
        int32 PlayerIndex=INDEX_NONE; // 同一family的玩家索引，避免分屏另一玩家的曝光混入。
        const void* MainTarget=nullptr; // 真实Viewport渲染目标身份；只比较地址。
        const void* ViewActor=nullptr; // GT记录的主ViewActor身份；RT只比较地址。
        float LuminanceMax=1.f; // GT读取本帧镜头标定的白点系数；RT仅用纯值，避免读非RenderThreadSafe默认曝光CVar。
        double RequestedAt=0.; // 单调秒；超5秒未消费则停止扩展并输出unavailable。
    };

    /** 短命只读World扩展：下一正常主View作一次GT检查及RT公开曝光观测，完成后不再活跃。 */
    class FRenderCheckExtension final:public FWorldSceneViewExtension
    {
    public:
        /** World仅由基类弱持有；Shared持有纯值，不将任何GT UObject交给RT解析。 */
        FRenderCheckExtension(const FAutoRegister& Register,UWorld* World,const TSharedRef<FRenderCheckState,ESPMode::ThreadSafe>& Shared)
            :FWorldSceneViewExtension(Register,World),State(Shared) { DEMO_LOG_STATIC_CALL(World); }
        /** 不修改Family；必需回调的高频埋点。 */
        virtual void SetupViewFamily(FSceneViewFamily& Family) override { DEMO_LOG_STATIC_TICK(nullptr); }
        /** 不修改View；最终PP在BeginRenderViewFamily读取。 */
        virtual void SetupView(FSceneViewFamily& Family,FSceneView& View) override { DEMO_LOG_STATIC_TICK(nullptr); }
        /** GT只在真实World/主Viewport匹配后选择一次View；所有UObject检查、PP栈和光源读取留在本线程。 */
        virtual void BeginRenderViewFamily(FSceneViewFamily& Family) override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            UWorld* ObservedWorld=GetWorld(); // 基类弱World只在GT解析；PIE退出后为空。
            if(!ObservedWorld||!ObservedWorld->GetGameViewport()||Family.RenderTarget!=ObservedWorld->GetGameViewport()->Viewport) return;
            {
                FScopeLock Lock(&State->Mutex); // 只保护一次标记，扫描World前即释放。
                if(State->bGTDone) return;
            }
            for(const FSceneView* View:Family.Views) // 只选择该Family第一个正常本机主View，分屏逐玩家运行可扩充，当前明确日志PlayerIndex。
            {
                if(!View||View->bIsSceneCapture||View->bIsReflectionCapture||View->bIsPlanarReflection||!View->IsPrimarySceneView()) continue;
                {
                    FScopeLock Lock(&State->Mutex); // GT冻结一次View身份；RT无World/Actor解引用。
                    State->bGTDone=true; State->Frame=Family.FrameCounter; State->PlayerIndex=View->PlayerIndex;
                    State->MainTarget=Family.RenderTarget; State->ViewActor=View->ViewActor;
                    const IConsoleVariable* Lens=IConsoleManager::Get().FindConsoleVariable(TEXT("r.EyeAdaptation.LensAttenuation")); // GT同步只读镜头标定。
                    const IConsoleVariable* Extended=IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange")); // GT只读默认EV语义。
                    State->LuminanceMax=(Extended&&Extended->GetInt()!=0&&Lens) ? .78f/FMath::Max(.01f,Lens->GetFloat()) : 1.f;
                }
                PrintPostProcessStack(ObservedWorld,*View); PrintRoomAtmospheres(ObservedWorld); PrintLightDemand(ObservedWorld,*View);
                return;
            }
        }
        /** RT在正常主View后处理前只读最后完成的曝光；getter与本帧有延迟，不创建RDG/GPU readback或屏幕副本。 */
        virtual void PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder,const FSceneView& View,const FPostProcessingInputs& Inputs) override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            if(!View.Family||View.bIsSceneCapture||View.bIsReflectionCapture||View.bIsPlanarReflection) return;
            float LuminanceMax=1.f; // 同一GT观测帧冻结的纯值，RT不解引用任何UObject或非线程安全CVar。
            {
                FScopeLock Lock(&State->Mutex); // 纯值身份核对，不调用任何UObject方法。
                if(!State->bGTDone||State->bRTDone||State->Frame!=View.Family->FrameCounter||State->PlayerIndex!=View.PlayerIndex||State->MainTarget!=View.Family->RenderTarget||State->ViewActor!=View.ViewActor) return;
                State->bRTDone=true;
                LuminanceMax=State->LuminanceMax;
            }
            const float Exposure=View.GetLastEyeAdaptationExposure(); // 引擎最后完成GPU回读比例；0/无buffer不能换算有效EV。
            const bool bValid=View.HasValidEyeAdaptationBuffer()&&FMath::IsFinite(Exposure)&&Exposure>0.f; // buffer存在不表示当前GPU帧已完成。
            const float EffectiveEV=bValid?-FMath::Log2(Exposure*LuminanceMax):0.f; // 含曝光补偿的等效适应EV100；并非场景Average/Target EV100。
            UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_EXPOSURE status=%s observed_family_frame=%llu player=%d last_completed_exposure=%g effective_adapted_EV100=%g Lmax=%g camera_cut=%d origin_cm=%s source=public_delayed_readback GPU_source_frame=unknown scene_average_target_EV100=unavailable compensation_included=1"),
                bValid?TEXT("available_delayed"):TEXT("unavailable"),View.Family->FrameCounter,View.PlayerIndex,Exposure,EffectiveEV,LuminanceMax,View.bCameraCut,*View.ViewMatrices.GetViewOrigin().ToString());
            UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_END snapshot_only no_CVar_ShowFlag_asset_network_save_or_scene_writes"));
        }
    protected:
        /** GT活跃门；完成或5秒超时即停止，不在后台长期扫描。基类继续执行World匹配。 */
        virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override
        {
            DEMO_LOG_STATIC_TICK(nullptr);
            FScopeLock Lock(&State->Mutex); // GT/RT共享完成标记，所保护字段均为纯值。
            if(State->bRTDone) return false;
            if(FPlatformTime::Seconds()-State->RequestedAt>5.)
            {
                State->bRTDone=true;
                UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_SKIP reason=NoMatchingRenderedMainViewWithin5Seconds exposure=unavailable GT_stack_collected=%d"),State->bGTDone);
                return false;
            }
            return FWorldSceneViewExtension::IsActiveThisFrame_Internal(Context);
        }
    private:
        TSharedRef<FRenderCheckState,ESPMode::ThreadSafe> State; // 扩展/RT回调保活小状态，生命周期不依赖原World。
    };

    TSharedPtr<FRenderCheckExtension,ESPMode::ThreadSafe> CurrentCheck; // 仅GT命令替换/模块卸载释放；引擎持有已入队帧引用直至RT回调完成。

    /** 控制台同步入口；无参数、仅有本机正常Viewport的游戏World；Output只在同步作用域使用，不捕获到异步回调。 */
    void RequestRenderCheck(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output)
    {
        DEMO_LOG_STATIC_CALL(World);
        if(Args.Num()!=0||!World||!World->IsGameWorld()||World->GetNetMode()==NM_DedicatedServer||!World->GetGameViewport()||!World->GetGameViewport()->Viewport)
        {
            Output.Log(TEXT("RENDER_CHECK_REJECT reason=UsageOrNoLocalGameViewport usage=Demo.Render.Check (Editor/Development normal -game -windowed or PIE)"));
            UE_LOG(LogFPSDemo,Warning,TEXT("RENDER_CHECK_REJECT reason=UsageOrNoLocalGameViewport")); return;
        }
        const TCHAR* Names[]={ // 只读配置白名单：容量/开关与实测利用率严格分开；保护描边值一起输出。
            TEXT("r.CustomDepth"),TEXT("r.CustomDepthTemporalAAJitter"),TEXT("r.AntiAliasingMethod"),TEXT("r.ScreenPercentage"),
            TEXT("sg.ShadowQuality"),TEXT("sg.GlobalIlluminationQuality"),TEXT("sg.ReflectionQuality"),TEXT("sg.PostProcessQuality"),
            TEXT("r.Shadow.Virtual.Enable"),TEXT("r.Shadow.Virtual.MaxPhysicalPages"),TEXT("r.Shadow.Virtual.Cache"),TEXT("r.Shadow.Virtual.NonNanite.IncludeInCoarsePages"),
            TEXT("r.Lumen.DiffuseIndirect.Allow"),TEXT("r.Lumen.Reflections.Allow"),TEXT("r.LumenScene.SurfaceCache.AtlasSize"),TEXT("r.LumenScene.DirectLighting.UpdateFactor"),TEXT("r.LumenScene.Radiosity.UpdateFactor"),
            TEXT("r.Lumen.ScreenProbeGather.ScreenTraces"),TEXT("r.Lumen.Reflections.ScreenTraces"),TEXT("r.EyeAdaptation.LensAttenuation"),TEXT("r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange")};
        for(const TCHAR* Name:Names) PrintCVar(Name);
        UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_GPU VSM_page_usage=unavailable Lumen_SurfaceCache_residency=unavailable actual_shadow_projection_count=unavailable reason=UE5.4_public_API_has_no_GPU_counters; configuration_capacity_is_not_utilization"));
        UE_LOG(LogFPSDemo,Display,TEXT("RENDER_CHECK_GPU_TOOLS manual_separate_capture_only: r.Shadow.Virtual.ShowStats 1; r.LumenScene.DumpStats 1; r.LumenScene.Stats 1; Editor_ViewMode_Lumen_SurfaceCache; command_does_not_execute_them"));
        const TSharedRef<FRenderCheckState,ESPMode::ThreadSafe> Shared=MakeShared<FRenderCheckState,ESPMode::ThreadSafe>(); // 本次请求独立纯值；重复命令不复用旧帧曝光。
        Shared->RequestedAt=FPlatformTime::Seconds();
        CurrentCheck=FSceneViewExtensions::NewExtension<FRenderCheckExtension>(World,Shared);
        Output.Log(TEXT("RENDER_CHECK_ARMED next_main_view_only; records in LogFPSDemo; GPU exposure may be delayed/unavailable; no rendering settings changed."));
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice RenderCheckCommand(TEXT("Demo.Render.Check"),TEXT("Editor/Development read-only one-shot main-view PP/exposure/light diagnostic. GPU-only counters explicitly unavailable; never changes CVar/showflags/assets."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&RequestRenderCheck)); // 模块静态注册/自动注销；静态委托无捕获；Shipping预处理移除全部接口。
}
#endif
