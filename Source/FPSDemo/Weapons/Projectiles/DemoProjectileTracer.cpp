// 职责：以弹种实例MID显示已确认分段弹道及原生接触短闪。使用：本地/远端同样式，纯视觉Actor统一接受Session/阶段清理，无碰撞/伤害。 本人首段使用发射帧最终可见枪口。
#include "Weapons/Projectiles/DemoProjectileTracer.h"
#include "Weapons/Projectiles/DemoProjectileConfig.h"
#include "Weapons/DemoWeaponBase.h" // 首段短显示窗弱借可见Socket和既有遮挡检查。
#include "Weapons/Projectiles/DemoProjectilePresentation.h"
#include "Game/Flow/DemoCombatSession.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Debug/DemoLog.h"
#include "CoreGlobals.h"
#include "Kismet/GameplayStatics.h" // VIEW-SAFE读取本机首个玩家相机，只读。
#include "Camera/PlayerCameraManager.h"

ADemoProjectileTracer::ADemoProjectileTracer()
{
    DEMO_LOG_CALL();
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork; // 相机/动画求值后再处理正在显现的枪口近端。
    PrimaryActorTick.bStartWithTickEnabled = false; // 飞行由真实弹推动；仅独立淡出按需启Tick。
    bReplicates = false; // 服务器本地或客户端收到真实路径消息后各自创建纯表现，不复制碰撞/伤害权限。
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TracerMesh"));
    SetRootComponent(Mesh);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCastShadow(false);
    Mesh->SetOwnerNoSee(false);
    Mesh->SetOnlyOwnerSee(false);
    Mesh->SetReceivesDecals(false);
    Mesh->bUseAsOccluder = false; // 亮条不参与其他几何的遮挡优化。
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube")); // 引擎100cm立方体硬引用，Cook自动收集。
    Mesh->SetStaticMesh(Cube.Object);
    SetActorEnableCollision(false);
    SetActorHiddenInGame(true);
}

bool ADemoProjectileTracer::Initialize(const FDemoProjectileConfig& Config, const FVector& InOrigin, const FDemoAmmoVisualStyle& Style, bool bRicochet)
{
    DEMO_LOG_CALL();
    FString Error; // 借用完整子弹配置校验，非法视觉数值不能产生NaN组件变换。
    if (bInitialized || !Config.bEnableTracer || !Config.Validate(Error) || InOrigin.ContainsNaN() || !Mesh->GetStaticMesh())
    {
        UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_TRACER_REJECT config/mesh/origin %s"), *Error);
        return false;
    }
    Origin = InOrigin;
    LastPosition = InOrigin;
    const FDemoAmmoVisualStyle Safe=DemoAmmoVisuals::IsValid(Style) ? Style : DemoAmmoVisuals::Default(); // 拷贝纯视觉值，坏样式只回退本弹普通外观。
    if (!DemoAmmoVisuals::IsValid(Style)) UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_TRACER_STYLE_INVALID normal fallback"));
    const FDemoAmmoTracerSettings Settings=DemoAmmoVisuals::TracerSettings(Config,Safe,bRicochet); // 与合同测试共用实际生产组合，不复制公式作假验证。
    Width = Settings.Width;
    MaximumLength = Settings.Length;
    FadeSeconds = Settings.FadeSeconds;
    SetActorLocation(InOrigin);
    Mesh->SetHiddenInGame(false);
    Mesh->SetVisibility(true);
    UMaterialInterface* BaseMaterial = Config.TracerMaterial ? Config.TracerMaterial.Get() : Mesh->GetMaterial(0); // 空配置仍显示默认Mesh材质，但没有可调发光效果。
    if (BaseMaterial)
    {
        Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
        if (Material)
        {
            Material->SetScalarParameterValue(TEXT("TracerOpacity"), 1.f);
            Material->SetVectorParameterValue(TEXT("TracerColor"),Settings.Color);
            Material->SetScalarParameterValue(TEXT("CoreIntensity"),Settings.CoreIntensity);
            Material->SetScalarParameterValue(TEXT("TracerShape"),Safe.Shape);
            Mesh->SetMaterial(0, Material);
        }
    }
    bInitialized = true;
    if (UDemoCombatSession* Session=UDemoCombatSession::FindActive(GetWorld())) // 原弹销毁后本表现仍独立注册，不会在换关残留亮条。
        if (!Session->RegisterCleanup(this,FSimpleDelegate::CreateUObject(this,&ADemoProjectileTracer::CancelForSession))) return false;
    SetLifeSpan(Config.MaxLifeSeconds + FadeSeconds + 1.f); // 兜底覆盖弹本体寿命；Finish会缩短到独立淡出秒数。
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_TRACER_READY material=%s length=%.1f width=%.1f fade=%.3f"),
        *GetNameSafe(BaseMaterial), MaximumLength, Width, FadeSeconds);
    return true;
}

void ADemoProjectileTracer::UpdateTravel(const FVector& Position, float TravelledDistance)
{
    DEMO_LOG_TICK();
    if (!bInitialized || bFinishing || Position.ContainsNaN() || !FMath::IsFinite(TravelledDistance) || TravelledDistance <= 0.f) return;
    const FVector Delta = Position - LastPosition; // 世界cm，仅计算实际已经到达的方向，绝不读取瞄准目标。
    if (Delta.IsNearlyZero()) return;
    const FVector Direction = (Position-Origin).GetSafeNormal(); // 每段直线独立Origin；表面接触与球心有半径差时也严格锚定本段，不因沿用上步速度把尾端推回转折点后。
    const float Length = DemoProjectilePresentation::SegmentLength(Origin,Position,TravelledDistance,MaximumLength); // 新段独立Origin保证出射条绝不越过弹射点。
    if (Length <= UE_KINDA_SMALL_NUMBER) return;
    // 引擎Cube以中心为原点，100cm边长；前端严格落在本次实际球心，后端只覆盖已经飞过的路径。近镜头部分由PlaceSegment裁剪/限宽。
    LastPosition = Position;
    if (!PlaceSegment(Position - Direction * Length, Position)) return;
    bHasTravel = true;
    SetActorHiddenInGame(false);
}
bool ADemoProjectileTracer::PlaceSegment(FVector Start, const FVector& End)
{
    DEMO_LOG_TICK();
    constexpr float NearCm=50.f; // 镜头前不绘制的距离cm：FP缩放枪口离眼仅十几厘米，近于此的部分透视失真成楔子。
    constexpr float MaxViewAngle=.006f; // 横截面最大视角(弧度)，1600px宽/90°水平FOV约5px；远处仍用作者宽度。
    float WidthCm=Width*CrossSectionScale;
    ViewWidthCap=TNumericLimits<float>::Max();
    const APlayerCameraManager* Camera=GetWorld() && GetNetMode()!=NM_DedicatedServer ? UGameplayStatics::GetPlayerCameraManager(this,0) : nullptr; // 本机首个本地玩家视点，只读。
    if (Camera)
    {
        const FVector Eye=Camera->GetCameraLocation();
        if (FVector::DistSquared(End,Eye)<=FMath::Square(NearCm)) { SetActorHiddenInGame(true); return false; } // 整段贴脸：不画。
        if (FVector::DistSquared(Start,Eye)<FMath::Square(NearCm))
        {
            // 解|Start+u*D-Eye|=NearCm的出射根（c<0保证唯一正根），把近端推到镜头半米之外。
            const FVector D=End-Start, W=Start-Eye;
            const double A=FVector::DotProduct(D,D), B=2.*FVector::DotProduct(W,D), C=FVector::DotProduct(W,W)-FMath::Square(NearCm);
            if (A>UE_SMALL_NUMBER) Start+=D*FMath::Clamp((-B+FMath::Sqrt(FMath::Max(0.,B*B-4.*A*C)))/(2.*A),0.,1.);
        }
        const float Closest=FVector::Distance(FMath::ClosestPointOnSegment(Eye,Start,End),Eye); // 段上离镜头最近点，侧向掠过也正确。
        ViewWidthCap=FMath::Max(.05f,Closest*MaxViewAngle);
        WidthCm=FMath::Min(WidthCm,ViewWidthCap);
    }
    const FVector Delta=End-Start;
    const float Length=static_cast<float>(Delta.Size());
    if (Length<=UE_KINDA_SMALL_NUMBER) { SetActorHiddenInGame(true); return false; }
    SetActorLocationAndRotation(Start+Delta*.5f,Delta.Rotation());
    SetActorScale3D(DemoProjectilePresentation::TracerMeshScale(Length,WidthCm,1.f)); // WidthCm已含CrossSectionScale与视角上限，不再二次缩放。
    return true;
}
void ADemoProjectileTracer::UpdateSegment(const FVector& Start,const FVector& End)
{
    DEMO_LOG_TICK();
    if (!bInitialized || bFinishing || Start.ContainsNaN() || End.ContainsNaN()) return;
    const FVector Delta=End-Start; // 已确认两个视觉样本，不能外推到准星或下一帧。
    const float Length=FMath::Min(MaximumLength,static_cast<float>(Delta.Size()));
    if (Length<=UE_KINDA_SMALL_NUMBER) return;
    const FVector Direction=Delta.GetSafeNormal();
    LastPosition=End;
    if (!PlaceSegment(End-Direction*Length,End)) return; // 近镜头裁剪/限宽；整段贴脸时本次不显示。
    bHasTravel=true; SetActorHiddenInGame(false);
}





bool ADemoProjectileTracer::BindLaunchDisplay(ADemoWeaponBase* Weapon,UPrimitiveComponent* Gun,const FVector& PhysicalOrigin,float Radius,float MuzzleScale,float Length)
{
    DEMO_LOG_TICK();
    // 调用方已按实际已飞路程、武器显示比例与准星留白给出世界厘米。
    if (!bInitialized || !Gun || !Weapon || !FMath::IsFinite(Length) || Length<=UE_KINDA_SMALL_NUMBER
        || !AttachToComponent(Gun,FAttachmentTransformRules::KeepWorldTransform,Weapon->Config.MuzzleFlashSocket))
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("PROJECTILE_AXIAL_FLASH_SKIP invalid span/attachment")); return false; }
    const FTransform Socket=Gun->GetSocketTransform(Weapon->Config.MuzzleFlashSocket);
    bMuzzleDisplay=true; LaunchWeapon=Weapon; PhysicalMuzzle=PhysicalOrigin; MuzzleQueryRadius=Radius;
    SetCrossSectionScale(MuzzleScale);
    SetActorLocationAndRotation(Socket.GetLocation()+Socket.GetUnitAxis(EAxis::X)*(Length*.5f),Socket.GetRotation());
    SetActorScale3D(DemoProjectilePresentation::TracerMeshScale(Length,Width,CrossSectionScale));
    bHasTravel=true; SetActorHiddenInGame(false);
    return true;
}
void ADemoProjectileTracer::SetCrossSectionScale(float Scale)
{
    DEMO_LOG_TICK();
    if (bFinishing) return;
    CrossSectionScale=FMath::IsFinite(Scale) && Scale>0.f && Scale<=1.f?Scale:1.f;
    if (bHasTravel)
    {
        FVector Current=GetActorScale3D(); // 已确定的长轴/位置/旋转保持，后续Update仍从作者Width重算，绝不累乘。
        Current.Y=FMath::Min(Width*CrossSectionScale,ViewWidthCap)/100.f; Current.Z=Current.Y; // 不突破PlaceSegment的近镜头视角上限。
        SetActorScale3D(Current);
    }
}
void ADemoProjectileTracer::CancelForSession()
{
    DEMO_LOG_CALL();
    SetActorHiddenInGame(true); SetActorTickEnabled(false); Destroy(); // 纯表现，不触发碰撞、伤害或奖励。
}

void ADemoProjectileTracer::Finish(float FadeOverride)
{
    DEMO_LOG_CALL();
    if (FMath::IsFinite(FadeOverride) && FadeOverride>=0.f) FadeSeconds=FMath::Max(FadeSeconds,FMath::Clamp(FadeOverride,0.f,1.f)); // 迟到弹射事实只延长，不重置累计秒/帧或反向增亮。
    if (bFinishing) return;
    bFinishing = true;
    if (!bInitialized || !bHasTravel || FadeSeconds <= UE_KINDA_SMALL_NUMBER)
    {
        Destroy();
        return;
    }
    FadeElapsed = 0.f;
    FinishFrame = GFrameCounter;
    SetActorTickEnabled(true);
    // 改由Tick淡出后释放：寿命Timer若在下一帧直接消费0.06秒，15fps时仍可能没有任何可见帧。
    // 初始完成帧及下一游戏帧都保留，再累计World秒；实际是否被相机看到仍取决于遮挡/视锥。
    SetLifeSpan(0.f);
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_TRACER_FINISH fade=%.3f"), FadeSeconds);
}

void ADemoProjectileTracer::CancelAll(UWorld* World)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs world=%s"), __FUNCTION__, *GetNameSafe(World));
    if (!World) return;
    for (TActorIterator<ADemoProjectileTracer> It(World); It; ++It) // 包含原弹已结束的淡出Actor，清场不得留下战斗表现。
        It->Destroy();
}

bool ADemoProjectileTracer::IsFinishing() const { DEMO_LOG_TICK(); return bFinishing; }

void ADemoProjectileTracer::Tick(float DeltaSeconds)
{
    DEMO_LOG_TICK();
    Super::Tick(DeltaSeconds);
    if (!bFinishing || !bHasTravel) return;
    if (bMuzzleDisplay && GFrameCounter>FinishFrame)
    {
        FVector Muzzle; // 当前已经可见的枪口；首段最远已飞端点冻结，绝不让整条弹道追随枪。
        if (GFrameCounter>FinishFrame+2 || !LaunchWeapon.IsValid()
            || !LaunchWeapon->TryGetLaunchDisplayMuzzle(PhysicalMuzzle,MuzzleQueryRadius,Muzzle))
        {
            SetActorHiddenInGame(true); Destroy(); return;
        }
        // 两端随同一枪口刚体移动，剩余世界尾迹由独立Actor显示。
        return;
    }
    if (GFrameCounter <= FinishFrame + 1) return; // 不能在创建/完成同帧或第一个后续游戏帧提前删掉纯表现。
    FadeElapsed += DeltaSeconds;
    const float Opacity = FMath::Min(LastOpacity,FMath::Clamp(1.f - FadeElapsed / FMath::Max(FadeSeconds, UE_KINDA_SMALL_NUMBER), 0.f, 1.f)); // 延长已淡出段不反向增亮，纯表现不修改Gameplay状态。
    LastOpacity=Opacity;
    if (Material) Material->SetScalarParameterValue(TEXT("TracerOpacity"), Opacity);
    if (Opacity <= 0.f) Destroy();
}

void ADemoProjectileTracer::LifeSpanExpired()
{
    DEMO_LOG_CALL();
    Destroy();
}

void ADemoProjectileTracer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DEMO_LOG_CALL();
    SetActorTickEnabled(false);
    Material = nullptr;
    if (ContactSound) { ContactSound->Stop(); ContactSound->DestroyComponent(); ContactSound=nullptr; }
    if (ContactEffect)
    {
        if (UParticleSystemComponent* Cascade=Cast<UParticleSystemComponent>(ContactEffect)) Cascade->DeactivateImmediate();
        else if (UNiagaraComponent* Niagara=Cast<UNiagaraComponent>(ContactEffect)) Niagara->DeactivateImmediate();
        ContactEffect->DestroyComponent(); ContactEffect=nullptr; // 含world-space已出生粒子；不能等AutoDestroy自然退场。
    }
    Super::EndPlay(EndPlayReason);
}

ADemoProjectileTracer* ADemoProjectileTracer::SpawnContact(UWorld* World, const FDemoProjectileConfig& Config, const FDemoAmmoVisualStyle& Style, const FVector& Point, const FVector& Direction, bool bRicochet)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs world=%s ricochet=%d"), __FUNCTION__,*GetNameSafe(World),bRicochet);
    if (!World || World->GetNetMode()==NM_DedicatedServer) return nullptr; // Dedicated从不分配纯视觉Actor或音频组件。
    if (Point.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero())
    { UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_CONTACT_REJECT nonfinite point/direction")); return nullptr; }
    if (!(bRicochet ? Config.RicochetEffect.Get() : Style.ImpactEffect.Get()) && !(bRicochet && Config.RicochetSound))
    { UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_CONTACT_SKIP optional effect/sound empty")); return nullptr; }
    FActorSpawnParameters Params; // World owns this nonreplicated Actor；碰撞永远关闭，生成不影响物理路径。
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADemoProjectileTracer* Contact=World->SpawnActor<ADemoProjectileTracer>(Point,Direction.Rotation(),Params); // 真实接触点定向，不从后续目标/准星外推。
    if (!Contact) { UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_CONTACT_REJECT spawn failed")); return nullptr; }
    if (!Contact->InitializeContact(Config,Style,Point,Direction,bRicochet)) { Contact->Destroy(); return nullptr; }
    return Contact;
}

bool ADemoProjectileTracer::InitializeContact(const FDemoProjectileConfig& Config, const FDemoAmmoVisualStyle& Style, const FVector& Point, const FVector& Direction, bool bRicochet)
{
    DEMO_LOG_CALL();
    if (bInitialized) { UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_CONTACT_REJECT already initialized")); return false; }
    const FDemoAmmoVisualStyle Safe=DemoAmmoVisuals::IsValid(Style) ? Style : DemoAmmoVisuals::Default(); // 接触与在飞亮条按同一冻结样式，不读取当前武器。
    UFXSystemAsset* Asset=bRicochet ? Config.RicochetEffect.Get() : Safe.ImpactEffect.Get(); // 仅强借用配置资产；组件属于本Actor。
    Mesh->SetVisibility(false); Mesh->SetHiddenInGame(true); // 接触模式仅显示CPU sprite/Niagara，不让默认Cube形成巨大亮块。
    SetActorLocationAndRotation(Point,Direction.Rotation());
    SetActorScale3D(FVector::OneVector);
    if (UParticleSystem* Cascade=Cast<UParticleSystem>(Asset))
    {
        UParticleSystemComponent* Component=NewObject<UParticleSystemComponent>(this); // 本Actor拥有，关闭自动激活后先配置MID再发粒子。
        Component->SetAutoActivate(false); Component->bAutoDestroy=false; Component->SetTemplate(Cascade);
        Component->SetupAttachment(Mesh); Component->RegisterComponent();
        ContactEffect=Component;
    }
    else if (UNiagaraSystem* Niagara=Cast<UNiagaraSystem>(Asset))
    {
        UNiagaraComponent* Component=NewObject<UNiagaraComponent>(this); // 无池化复用；立即清理需要包含该次所有world-space粒子。
        Component->SetAutoActivate(false); Component->SetAutoDestroy(false); Component->SetAsset(Niagara);
        Component->SetupAttachment(Mesh); Component->RegisterComponent();
        ContactEffect=Component;
    }
    else if (Asset) UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_CONTACT_UNSUPPORTED asset=%s; segmented tracer remains"),*GetNameSafe(Asset));
    if (ContactEffect)
    {
        ContactEffect->SetCollisionEnabled(ECollisionEnabled::NoCollision); ContactEffect->SetCastShadow(false);
        DemoAmmoVisuals::ApplyEffect(ContactEffect,Safe,bRicochet ? Config.RicochetCoreBoost : 1.f);
    }
    if (bRicochet && Config.RicochetSound)
    {
        ContactSound=NewObject<UAudioComponent>(this); // 本Actor拥有，切阶段EndPlay停止；音频可空，不改变命中事实。
        ContactSound->SetAutoActivate(false); ContactSound->bAutoDestroy=false; ContactSound->bStopWhenOwnerDestroyed=true;
        ContactSound->SetupAttachment(Mesh); ContactSound->SetSound(Config.RicochetSound); ContactSound->RegisterComponent();
    }
    if (!ContactEffect && !ContactSound) { UE_LOG(LogFPSDemo, Warning, TEXT("PROJECTILE_CONTACT_REJECT no supported effect/sound")); return false; }
    bInitialized=true;
    if (UDemoCombatSession* Session=UDemoCombatSession::FindActive(GetWorld()))
        if (!Session->RegisterCleanup(this,FSimpleDelegate::CreateUObject(this,&ADemoProjectileTracer::CancelForSession))) return false;
    SetActorHiddenInGame(false);
    if (ContactEffect) ContactEffect->Activate(true);
    if (ContactSound) ContactSound->Play();
    SetLifeSpan(.22f); // 作者CPU粒子0.15秒；Actor .22秒固定兜底，清场仍通过CancelAll/Session立即撤销。
    UE_LOG(LogFPSDemo, Log, TEXT("PROJECTILE_CONTACT_READY ricochet=%d asset=%s point=%s"),bRicochet,*GetNameSafe(Asset),*Point.ToCompactString());
    return true;
}
