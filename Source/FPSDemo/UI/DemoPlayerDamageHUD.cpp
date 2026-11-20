// 职责：S1 R02本人受击Canvas反馈：余烬边缘/世界来向弧、真实HP损失发丝残影和危险/减速/救险结果。
// 使用：DrawHUD分背景与信息两次调用；仅借用PC受击组件已衰减快照和原ASC，不创建资产、UObject、Timer或任何伤害权威。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/DemoHUDReadout.h"
#include "Game/DemoGameState.h"
#include "Combat/Feedback/Player/DemoPlayerDamageFeedbackComponent.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "GAS/DemoAttributeSet.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/World.h"
#include "RenderUtils.h"
#include "Debug/DemoLog.h"

namespace
{
    /** 本帧有界顶点表追加三角形；A/B/C为实际视口像素，ColorA/B/C为各顶点透明色，不持有Canvas或World引用。 */
    void AddDamageTriangle(TArray<FCanvasUVTri>& Triangles,const FVector2D& A,const FVector2D& B,const FVector2D& C,FLinearColor ColorA,FLinearColor ColorB,FLinearColor ColorC)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[DamageCanvas] %hs"),__FUNCTION__);
        FCanvasUVTri& Triangle=Triangles.AddDefaulted_GetRef(); // Canvas提交时复制；本地表只在当前DrawHUD中存在。
        Triangle.V0_Pos=A; Triangle.V1_Pos=B; Triangle.V2_Pos=C;
        Triangle.V0_UV=Triangle.V1_UV=Triangle.V2_UV=FVector2D::ZeroVector;
        Triangle.V0_Color=ColorA; Triangle.V1_Color=ColorB; Triangle.V2_Color=ColorC;
    }

    /** 外侧AB到内侧CD的渐变四边形；像素点沿周向排列，Outer包含外缘透明度，内缘透明，表仍为当前帧借用。 */
    void AddDamageEdge(TArray<FCanvasUVTri>& Triangles,const FVector2D& A,const FVector2D& B,const FVector2D& C,const FVector2D& D,FLinearColor Outer)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[DamageCanvas] %hs"),__FUNCTION__);
        const FLinearColor Clear=Outer.CopyWithNewOpacity(0.f); // 保留色相只把Alpha归零，避免中心残留全屏色块。
        AddDamageTriangle(Triangles,A,B,C,Outer,Outer,Clear);
        AddDamageTriangle(Triangles,A,C,D,Outer,Clear,Clear);
    }

    /** 按固定段数追加纯色环弧；Center为像素中心，半径为像素，Start/End为屏幕弧度，Segments限制临时顶点成本。 */
    void AddDamageArc(TArray<FCanvasUVTri>& Triangles,const FVector2D& Center,float Inner,float Outer,float Start,float End,int32 Segments,FLinearColor Color)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[DamageCanvas] %hs"),__FUNCTION__);
        for(int32 Segment=0;Segment<Segments;++Segment)
        {
            const float A=FMath::Lerp(Start,End,float(Segment)/Segments),B=FMath::Lerp(Start,End,float(Segment+1)/Segments); // 当前环段端点弧度，不跨帧缓存。
            const FVector2D U(FMath::Cos(A),FMath::Sin(A)),V(FMath::Cos(B),FMath::Sin(B)); // 屏幕向右为0，上为-PI/2。
            AddDamageTriangle(Triangles,Center+U*Inner,Center+U*Outer,Center+V*Outer,Color,Color,Color);
            AddDamageTriangle(Triangles,Center+U*Inner,Center+V*Outer,Center+V*Inner,Color,Color,Color);
        }
    }

    /** 提交当前有限三角形，不加载材质；Canvas为本帧HUD借用，Triangles提交后即可释放。 */
    void SubmitDamageTriangles(UCanvas* Canvas,const TArray<FCanvasUVTri>& Triangles)
    {
        UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[DamageCanvas] %hs"),__FUNCTION__);
        if(!Canvas||Triangles.IsEmpty()) return;
        FCanvasTriangleItem Item(Triangles,GWhiteTexture); // 复用引擎白贴图，顶点色完成透明渐变。
        Item.BlendMode=SE_BLEND_Translucent;
        Canvas->DrawItem(Item);
    }
}

void ADemoHUD::DrawPlayerDamageFeedback(ADemoPlayerController* PC,bool bBackgroundOnly)
{
    DEMO_LOG_TICK();
    const ADemoCharacter* Pawn=PC?Cast<ADemoCharacter>(PC->GetPawn()):nullptr; // 实际观战目标不能使用旧Pawn的受击包络。
    const UDemoAttributeSet* Attributes=Pawn?Pawn->GetDemoAttributes():nullptr; // HP仍是原PS ASC真值，UI不据回执自行扣血。
    if(!Canvas||!PC||!PC->IsLocalController()||PC->HasBlockingOverlay()||PC->IsUpgradeMenuOpen()||PC->IsNextLevelConfirmationOpen()||!Pawn||PC->GetViewTarget()!=Pawn||Pawn->IsCoopEliminated()||!Attributes||Attributes->GetHealth()<=0.f)
    { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("DAMAGE HUD bypass inactive own living view/menu")); return; }
    const UDemoPlayerDamageFeedbackComponent* Feedback=UDemoPlayerDamageFeedbackComponent::Find(PC); // 本人PC唯一默认子对象，不扫描场景。
    if(!Feedback) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("DAMAGE HUD bypass missing feedback component")); return; }
    const FDemoPlayerDamageView& View=Feedback->GetView(); // root组件已处理菜单、阶段、代次、去重、容量和包络时间。
    const ADemoGameState* State=GetWorld()?GetWorld()->GetGameState<ADemoGameState>():nullptr; // 本帧只读关卡进度，计算D03药剂侵蚀边缘强度。
    float SerumStrain=0.f; // 第6关起随深入核心区递增的静态余烬红边缘强度[0,0.06]，地狱难度略强，不遮挡中心视线。
    if(bBackgroundOnly&&State&&State->Phase==EDemoPhase::Combat&&State->LevelNumber>=6)
    {
        const float DepthProgress=FMath::Clamp(float(State->LevelNumber-5)/5.f,0.f,1.f); // 第6~10关线性递增[0.2..1.0]。
        SerumStrain=DepthProgress*(State->Difficulty==EDemoDifficulty::Hell?.06f:.04f); // 保持克制，避免干扰真实受击或亮底阅读。
    }
    if(!View.bVisible&&SerumStrain<=KINDA_SMALL_NUMBER) { UE_LOG(LogFPSDemo,VeryVerbose,TEXT("DAMAGE HUD bypass hidden view")); return; }
    const FLinearColor Ink(FColor(233,227,215)),Ember(FColor(180,71,47)); // S0 R02前景；危险仍由文字/形状与真实反馈同时表达，不改强度配置或回执。

    if(bBackgroundOnly)
    {
        const float LowAlpha=View.bVisible?FMath::Clamp(View.LowHealthAlpha,0.f,1.f):0.f; // 持续危险状态不闪烁，与短促受击Flash分别表达。
        const float DamageOpacity=View.bVisible?(View.EdgeStrength+LowAlpha*.045f):0.f; // 仅在受击包络有效时计入伤害透明度。
        const float Opacity=FMath::Clamp(FMath::Max(DamageOpacity,SerumStrain),0.f,.3f); // 药剂侵蚀取最大而非叠爆，不改变受击上限。
        if(Opacity<=KINDA_SMALL_NUMBER) return;
        const float W=Canvas->ClipX,H=Canvas->ClipY; // 外缘使用实际像素，超宽/4:3也不拉伸中心信息。
        const float Border=FMath::Min(66.f*UIScale,FMath::Min(W,H)*.1f); // 最多覆盖屏幕短边10%，中央场景始终透明。
        const FLinearColor Tint=Ember.CopyWithNewOpacity(Opacity); // 原反馈包络/渐变面积不变，按R02余烬色显示；不增加背景图、白闪或全屏曝光跳变。
        TArray<FCanvasUVTri> Triangles; Triangles.Reserve(8); // 四个互不重叠的梯形边框，共八个三角形。
        AddDamageEdge(Triangles,FVector2D(0,0),FVector2D(W,0),FVector2D(W-Border,Border),FVector2D(Border,Border),Tint);
        AddDamageEdge(Triangles,FVector2D(W,0),FVector2D(W,H),FVector2D(W-Border,H-Border),FVector2D(W-Border,Border),Tint);
        AddDamageEdge(Triangles,FVector2D(W,H),FVector2D(0,H),FVector2D(Border,H-Border),FVector2D(W-Border,H-Border),Tint);
        AddDamageEdge(Triangles,FVector2D(0,H),FVector2D(0,0),FVector2D(Border,Border),FVector2D(Border,H-Border),Tint);
        SubmitDamageTriangles(Canvas,Triangles);
        return;
    }

    const FVector2D Center(Canvas->ClipX*.5f,Canvas->ClipY*.5f); // 准星外的来向层，独立于命中目标十字与节律装填条。
    const float Yaw=PC->PlayerCameraManager?PC->PlayerCameraManager->GetCameraRotation().Yaw:PC->GetControlRotation().Yaw; // 仅用当前相机Yaw，回执保存世界来向不跟踪攻击者。
    const float Radius=112.f*UIScale; // 方向弧的内径在装填提示条外；按等比设计单位保持宽高比。
    TArray<FCanvasUVTri> Triangles; Triangles.Reserve(FMath::Min(View.Indicators.Num(),4)*48); // 最多192三角形；没有来向时不为持续低血分配顶点表。
    for(int32 Index=0;Index<FMath::Min(View.Indicators.Num(),4);++Index)
    {
        const FDemoPlayerDamageIndicator& Indicator=View.Indicators[Index]; // 当前本机只读槽，不持有来源Actor。
        const float Strength=FMath::Clamp(Indicator.Strength,0.f,1.f); // 组件已衰减，不在绘制中再推进计时。
        if(Strength<=KINDA_SMALL_NUMBER) continue;
        const FLinearColor Color=Ember.CopyWithNewOpacity(Strength*.9f); // 世界来向投影/形状保持原路，只将高饱和橙红改为R02余烬色。
        const FVector Direction=FVector(Indicator.WorldDirection.X,Indicator.WorldDirection.Y,0.f).GetSafeNormal(); // 高度不影响水平来向；垂直或未知来源退回无方向环。
        if(Indicator.bOmni||Direction.IsNearlyZero())
        {
            for(int32 Cardinal=0;Cardinal<4;++Cardinal)
            {
                const float Angle=Cardinal*HALF_PI; // 四段等长虚线环表示全向/未知，无假箭头。
                AddDamageArc(Triangles,Center,Radius,Radius+3.f*UIScale,Angle-.32f,Angle+.32f,6,Color.CopyWithNewOpacity(Color.A*.7f));
            }
            continue;
        }
        const float Relative=FMath::FindDeltaAngleDegrees(Yaw,FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X))); // 正前0、右侧90；转镜头时持续更新。
        const float Angle=FMath::DegreesToRadians(Relative)-HALF_PI; // 正前映射到屏幕上方，不依赖Actor自身朝向。
        AddDamageArc(Triangles,Center,Radius,Radius+4.f*UIScale,Angle-.30f,Angle+.30f,6,Color);
        const FVector2D Out(FMath::Cos(Angle),FMath::Sin(Angle)),Side(-Out.Y,Out.X); // 外指箭头朝实际来向，横向只用于箭头底边。
        const FVector2D Tip=Center+Out*(Radius+15.f*UIScale),Base=Center+Out*(Radius+7.f*UIScale); // 固定小箭头，避免与准星杀敌确认混淆。
        AddDamageTriangle(Triangles,Tip,Base+Side*4.f*UIScale,Base-Side*4.f*UIScale,Color,Color,Color);
    }
    SubmitDamageTriangles(Canvas,Triangles);

    const float RealFraction=DemoHUDReadout::Fraction(Attributes->GetHealth(),Attributes->GetMaxHealth()); // 与原HUD实血条严格同源。
    const float TrailFraction=FMath::Max(RealFraction,DemoHUDReadout::Fraction(View.HealthTrail,View.HealthTrailMax)); // 只允许残影高于当前HP，不画治疗假损失。
    if(TrailFraction>RealFraction+KINDA_SMALL_NUMBER)
        R02HUDLine(48.f+248.f*RealFraction,ViewHeight-44.f,248.f*(TrailFraction-RealFraction),1.f,Ember.CopyWithNewOpacity(.82f)); // 与Status实血条同为1物理像素，只覆盖真实已损失区间，不动实血值或包络。

    const float CenterX=ViewWidth*.5f,StatusY=FMath::Min(ViewHeight*.5f+158.f,ViewHeight-164.f); // 留开中心/节律条与底部技能区，窄视口同样使用设计坐标。
    if(View.LowHealthAlpha>KINDA_SMALL_NUMBER)
    {
        Label(View.bCritical?TEXT("!  生命危急"):TEXT("!  生命偏低"),CenterX,StatusY,View.bCritical?18.f:16.f,Ember.CopyWithNewOpacity(FMath::Clamp(View.LowHealthAlpha,.5f,1.f)),true); // 固定非闪烁文字，颜色之外仍能辨认危险。
    }
    if(View.bSlowed) Label(TEXT("↓  移动减速"),CenterX,StatusY+24.f,14,Ink,true); // 只读实际Slowed Tag，UI不决定速度或持续时间。
    if(GetWorld()&&GetWorld()->GetTimeSeconds()<View.OutcomeEndTime)
    {
        if(View.Outcome==TEXT("Rescue")) Label(TEXT("救险触发"),CenterX,StatusY-24.f,16,Ink,true);
        else if(View.Outcome==TEXT("Evaded")) Label(TEXT("闪避成功"),CenterX,StatusY-24.f,16,Ink,true); // 回执不含实际恢复量；只显示成功，不把满血时钳制为0的治疗伪报为+5。
    }
}
