// 职责：本地能力与天赋的实时视觉反馈（冲刺/治疗边缘脉冲、核心强化准星角标、能力触发短提示）。
// 使用：由DrawHUD每帧只读调用；仅观测权威属性/GAS/RunBuild边沿变化，不修改玩法真值、不复制、不写档。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "UI/R02/DemoUITheme.h"
#include "Characters/DemoCharacter.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerState.h"
#include "Game/DemoGameState.h"
#include "GAS/DemoAttributeSet.h"
#include "GAS/DemoAbilitySystemComponent.h"
#include "GAS/DemoTags.h"
#include "GAS/Talents/DemoTalentMoveEffect.h"
#include "Progression/Builds/DemoRunBuildComponent.h"
#include "Progression/Builds/DemoRunBuildTypes.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Engine/World.h"
#include "RenderUtils.h"
#include "Debug/DemoLog.h"

namespace
{
    /** 向本帧三角形表追加一个渐变外缘梯形（外缘Outer到内缘透明），不跨帧持有。 */
    void AddAbilityEdge(TArray<FCanvasUVTri>& Triangles, const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& Outer)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[AbilityHUD] %hs"), __FUNCTION__);
        const FLinearColor Clear = Outer.CopyWithNewOpacity(0.f);
        auto PushTri = [&Triangles](const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FLinearColor& C0, const FLinearColor& C1, const FLinearColor& C2)
        {
            FCanvasUVTri& Tri = Triangles.AddDefaulted_GetRef();
            Tri.V0_Pos = P0; Tri.V1_Pos = P1; Tri.V2_Pos = P2;
            Tri.V0_UV = Tri.V1_UV = Tri.V2_UV = FVector2D::ZeroVector;
            Tri.V0_Color = C0; Tri.V1_Color = C1; Tri.V2_Color = C2;
        };
        PushTri(A, B, C, Outer, Outer, Clear);
        PushTri(A, C, D, Outer, Clear, Clear);
    }

    /** 推入一条有界能力触发短提示，最多保留3条，避免连杀/连射刷屏。 */
    void PushAbilityToast(TArray<FDemoAbilityToast>& Toasts, const FString& Text, const FLinearColor& Color, double Now, float Duration = 1.75f)
    {
        UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[AbilityHUD] %hs"), __FUNCTION__);
        if (Text.IsEmpty()) return;
        // 若与最新一条同类文案相同且仍在活跃期，刷新寿命即可，不重复堆叠。
        if (Toasts.Num() > 0 && Toasts.Last().Text == Text)
        {
            Toasts.Last().ExpireAt = Now + Duration;
            Toasts.Last().Color = Color;
            return;
        }
        if (Toasts.Num() >= 3) Toasts.RemoveAt(0);
        FDemoAbilityToast& Entry = Toasts.AddDefaulted_GetRef();
        Entry.Text = Text;
        Entry.Color = Color;
        Entry.ExpireAt = Now + Duration;
    }
}

bool ADemoHUD::IsCoreEmpoweredNow(const ADemoCharacter* Player, const ADemoGameState* State) const
{
    DEMO_LOG_TICK();
    if (!Player || !State || State->Phase != EDemoPhase::Combat) return false;
    const ADemoPlayerState* PS = Player->GetPlayerState<ADemoPlayerState>();
    const UDemoRunBuildComponent* Build = PS ? PS->GetRunBuildComponent() : nullptr;
    if (!Build || !Build->HasSelectedCore()) return false;
    const FDemoRunBuildRuntimeView& Runtime = Build->GetRuntimeView();
    const float ServerTime = State->GetServerWorldTimeSeconds();
    const bool bTimeValid = Runtime.ServerEndTime > ServerTime;
    if (bTimeValid && Runtime.ChargedShots > 0) return true;
    const FName Action = Runtime.ActionState;
    if (bTimeValid && (Action == TEXT("Rhythm.Credits") || Action == TEXT("Crossfire.Gun") || Action == TEXT("Crossfire.Melee") || Action == TEXT("counter_ready") || Action == TEXT("Close.Tight")))
        return true;
    return Action == TEXT("AnchorLeave") || Action == TEXT("AnchorFire") || Action == TEXT("Rhythm.Ready") || Action == TEXT("Marksman.Ready");
}

void ADemoHUD::UpdateAbilityFeedback(const ADemoGameState* State, const ADemoCharacter* Player, const UDemoAttributeSet* Attributes)
{
    DEMO_LOG_TICK();
    const UWorld* World = GetWorld();
    if (!State || !Player || !Attributes || !World || Player->IsCoopEliminated()) return;
    const double Now = World->GetTimeSeconds();

    // 清理已过期的短提示。
    for (int32 Index = AbilityFeedback.Toasts.Num() - 1; Index >= 0; --Index)
    {
        if (Now >= AbilityFeedback.Toasts[Index].ExpireAt) AbilityFeedback.Toasts.RemoveAt(Index);
    }

    const UAbilitySystemComponent* ASC = Player->GetDemoASC();
    const UDemoWeaponComponent* Equipment = Player->GetWeaponComponent();
    const ADemoWeaponBase* Weapon = Equipment ? Equipment->GetActiveWeapon() : nullptr;
    const ADemoPlayerState* PS = Player->GetPlayerState<ADemoPlayerState>();
    const UDemoRunBuildComponent* Build = PS ? PS->GetRunBuildComponent() : nullptr;

    const float Health = Attributes->GetHealth();
    const int32 Ammo = Weapon ? Weapon->GetAmmo() : -1;
    const bool bReloading = ASC && ASC->HasMatchingGameplayTag(DemoTags::Reloading);
    const bool bDashCD = ASC && ASC->HasMatchingGameplayTag(DemoTags::DashCooldown);
    const bool bHealCD = ASC && ASC->HasMatchingGameplayTag(DemoTags::HealCooldown);

    int32 MoveStacks = 0;
    if (ASC)
    {
        FGameplayEffectQuery MoveQuery;
        MoveQuery.EffectDefinition = UDemoTalentMoveEffect::StaticClass();
        MoveStacks = ASC->GetActiveEffects(MoveQuery).Num();
    }

    int32 ChargedShots = 0;
    int32 PrecisionStacks = 0;
    FName ActionState = NAME_None;
    int32 ActionStacks = 0;
    if (Build && Build->HasSelectedCore() && State->Phase == EDemoPhase::Combat)
    {
        const FDemoRunBuildRuntimeView& Runtime = Build->GetRuntimeView();
        ChargedShots = Runtime.ChargedShots;
        PrecisionStacks = Runtime.PrecisionStacks;
        ActionState = Runtime.ActionState;
        ActionStacks = Runtime.ActionStacks;
    }

    // 切Pawn或切阶段时仅同步基线，不产生误报提示。
    const uint8 PhaseByte = static_cast<uint8>(State->Phase);
    if (AbilityFeedback.ObservedPawn.Get() != Player || AbilityFeedback.ObservedPhase != PhaseByte)
    {
        AbilityFeedback.ObservedPawn = Player;
        AbilityFeedback.ObservedWeapon = Weapon;
        AbilityFeedback.ObservedPhase = PhaseByte;
        AbilityFeedback.ObservedHealth = Health;
        AbilityFeedback.ObservedAmmo = Ammo;
        AbilityFeedback.bWasReloading = bReloading;
        AbilityFeedback.bWasDashCooldown = bDashCD;
        AbilityFeedback.bWasHealCooldown = bHealCD;
        AbilityFeedback.ObservedMoveStacks = MoveStacks;
        AbilityFeedback.ObservedChargedShots = ChargedShots;
        AbilityFeedback.ObservedPrecisionStacks = PrecisionStacks;
        AbilityFeedback.ObservedActionState = ActionState;
        AbilityFeedback.ObservedActionStacks = ActionStacks;
        AbilityFeedback.Toasts.Reset();
        return;
    }

    // 切枪时同步弹药基线，不把换出的枪弹量差误判为补弹。
    if (AbilityFeedback.ObservedWeapon.Get() != Weapon)
    {
        AbilityFeedback.ObservedWeapon = Weapon;
        AbilityFeedback.ObservedAmmo = Ammo;
        AbilityFeedback.bWasReloading = bReloading;
    }

    const FLinearColor HealColor(0.35f, 0.88f, 0.58f);
    const FLinearColor DashColor(0.62f, 0.88f, 1.0f);
    const FLinearColor CoreColor = IsR02GameplayPresentation() && R02HUDTheme ? R02HUDTheme->Ember : DemoUI::Gold;

    // 1. 冲刺与治疗冷却边沿检测
    if (!AbilityFeedback.bWasDashCooldown && bDashCD)
    {
        AbilityFeedback.DashFlashUntil = Now + 0.32;
    }
    else if (AbilityFeedback.bWasDashCooldown && !bDashCD)
    {
        AbilityFeedback.DashReadyFlashUntil = Now + 0.40;
    }

    const bool bHealJustActivated = (!AbilityFeedback.bWasHealCooldown && bHealCD);
    if (bHealJustActivated)
    {
        AbilityFeedback.HealFlashUntil = Now + 0.55;
    }
    else if (AbilityFeedback.bWasHealCooldown && !bHealCD)
    {
        AbilityFeedback.HealReadyFlashUntil = Now + 0.40;
    }

    // 2. 实际回血检测（Q治疗 / 战斗虹吸 / 回温装置 / 反击回路）
    const auto& MenuView = GetPlayerView();
    auto HasTalent = [&MenuView](FName TargetId) -> bool
    {
        for (const auto& Entry : MenuView.OwnedTalentViews)
        {
            if (Entry.Id == TargetId) return true;
        }
        return false;
    };

    if (AbilityFeedback.ObservedHealth >= 0.f && Health > AbilityFeedback.ObservedHealth + 0.5f)
    {
        const float Delta = Health - AbilityFeedback.ObservedHealth;
        AbilityFeedback.HealFlashUntil = Now + 0.55;
        AbilityFeedback.LastHealDelta = Delta;
        FString SourceLabel = TEXT("生命恢复");
        if (bHealJustActivated) SourceLabel = TEXT("治疗");
        else if (Build && Build->GetSnapshot().CoreId == TEXT("B07") && State->Phase == EDemoPhase::Combat) SourceLabel = TEXT("反击回血");
        else if (Build && Build->GetSnapshot().CoreId == TEXT("B08") && Build->GetSnapshot().Branch7 == TEXT("B08-C") && State->Phase == EDemoPhase::Combat) SourceLabel = TEXT("贴身护幕");
        else if (HasTalent(TEXT("siphon"))) SourceLabel = TEXT("战斗虹吸");
        PushAbilityToast(AbilityFeedback.Toasts, FString::Printf(TEXT("%s +%.0f HP"), *SourceLabel, Delta), HealColor, Now);
    }

    // 3. 非普通换弹的弹药补充检测（冲刺装填 / 协同补给 / 精准回收 / 节律供弹 / 独立闭环）
    const bool bReloadJustFinished = (AbilityFeedback.bWasReloading && !bReloading);
    if (Weapon && AbilityFeedback.ObservedAmmo >= 0 && Ammo > AbilityFeedback.ObservedAmmo && !bReloading && !bReloadJustFinished)
    {
        const int32 Gain = Ammo - AbilityFeedback.ObservedAmmo;
        FString SourceLabel = TEXT("能力补弹");
        if (Now < AbilityFeedback.DashFlashUntil + 0.65 && HasTalent(TEXT("dashload"))) SourceLabel = TEXT("冲刺装填");
        else if (Build && Build->GetSnapshot().CoreId == TEXT("B04") && State->Phase == EDemoPhase::Combat) SourceLabel = TEXT("节律供弹");
        else if (Build && Build->GetSnapshot().CoreId == TEXT("B03") && Build->GetSnapshot().Branch7 == TEXT("B03-C") && State->Phase == EDemoPhase::Combat) SourceLabel = TEXT("回收组件");
        else if (Build && Build->GetSnapshot().CoreId == TEXT("B05") && Build->GetSnapshot().Branch7 == TEXT("B05-C") && State->Phase == EDemoPhase::Combat) SourceLabel = TEXT("独立闭环");
        else if (Build && Build->GetSnapshot().CoreId == TEXT("B08") && Build->GetSnapshot().Branch3 == TEXT("B08-A") && State->Phase == EDemoPhase::Combat) SourceLabel = TEXT("双膛续压");
        else if (Build && Build->GetSnapshot().CoreId == TEXT("B09") && State->Phase == EDemoPhase::Combat) SourceLabel = TEXT("蓄势补弹");
        else if (HasTalent(TEXT("precision"))) SourceLabel = TEXT("精准回收");
        else if (HasTalent(TEXT("supply"))) SourceLabel = TEXT("协同补给");
        PushAbilityToast(AbilityFeedback.Toasts, FString::Printf(TEXT("%s +%d"), *SourceLabel, Gain), DemoUI::Accent, Now);
    }

    // 4. 移速增益检测（猎杀节奏 / 回撤推进）
    if (MoveStacks > AbilityFeedback.ObservedMoveStacks)
    {
        const bool bAssaultMove = Build && Build->GetSnapshot().CoreId == TEXT("B02") && Build->GetSnapshot().Branch7 == TEXT("B02-C") && State->Phase == EDemoPhase::Combat;
        PushAbilityToast(AbilityFeedback.Toasts, bAssaultMove ? TEXT("回撤推进 · 移速提升") : TEXT("猎杀节奏 · 移速提升"), DashColor, Now);
    }

    // 5. 核心流派充能/就绪状态边沿检测
    if (Build && Build->HasSelectedCore() && State->Phase == EDemoPhase::Combat)
    {
        const FName CoreId = Build->GetSnapshot().CoreId;
        const FString CoreName = DemoRunBuild::Title(CoreId).ToString();
        if (ChargedShots > AbilityFeedback.ObservedChargedShots)
        {
            AbilityFeedback.CorePulseUntil = Now + 0.45;
            PushAbilityToast(AbilityFeedback.Toasts, CoreName + TEXT(" · 强化就绪"), CoreColor, Now);
        }
        else if (PrecisionStacks > AbilityFeedback.ObservedPrecisionStacks)
        {
            AbilityFeedback.CorePulseUntil = Now + 0.25;
        }
        if (ActionState != AbilityFeedback.ObservedActionState)
        {
            if (ActionState == TEXT("Rhythm.Ready") || ActionState == TEXT("Rhythm.Credits"))
            {
                AbilityFeedback.CorePulseUntil = Now + 0.45;
                PushAbilityToast(AbilityFeedback.Toasts, TEXT("节律装填 · 节拍成功"), CoreColor, Now);
            }
            else if (ActionState == TEXT("Crossfire.Gun"))
            {
                AbilityFeedback.CorePulseUntil = Now + 0.45;
                PushAbilityToast(AbilityFeedback.Toasts, TEXT("交叉火力 · 切枪兑现"), CoreColor, Now);
            }
            else if (ActionState == TEXT("Crossfire.Melee"))
            {
                AbilityFeedback.CorePulseUntil = Now + 0.45;
                PushAbilityToast(AbilityFeedback.Toasts, TEXT("交叉火力 · 近战兑现"), CoreColor, Now);
            }
            else if (ActionState == TEXT("AnchorLeave") || ActionState == TEXT("AnchorFire"))
            {
                AbilityFeedback.CorePulseUntil = Now + 0.45;
                PushAbilityToast(AbilityFeedback.Toasts, TEXT("战术锚点 · 充能已满"), CoreColor, Now);
            }
            else if (ActionState == TEXT("counter_ready"))
            {
                AbilityFeedback.CorePulseUntil = Now + 0.45;
                PushAbilityToast(AbilityFeedback.Toasts, TEXT("反击回路 · 回收窗口"), CoreColor, Now);
            }
            else if (ActionState == TEXT("Close.Tight"))
            {
                AbilityFeedback.CorePulseUntil = Now + 0.45;
                PushAbilityToast(AbilityFeedback.Toasts, TEXT("抵近爆膛 · 散布收束"), CoreColor, Now);
            }
            else if (ActionState == TEXT("Marksman.Ready"))
            {
                AbilityFeedback.CorePulseUntil = Now + 0.45;
                PushAbilityToast(AbilityFeedback.Toasts, TEXT("蓄势贯杀 · 满蓄就绪"), CoreColor, Now);
            }
        }
    }

    // 更新本帧基线
    AbilityFeedback.ObservedHealth = Health;
    AbilityFeedback.ObservedAmmo = Ammo;
    AbilityFeedback.bWasReloading = bReloading;
    AbilityFeedback.bWasDashCooldown = bDashCD;
    AbilityFeedback.bWasHealCooldown = bHealCD;
    AbilityFeedback.ObservedMoveStacks = MoveStacks;
    AbilityFeedback.ObservedChargedShots = ChargedShots;
    AbilityFeedback.ObservedPrecisionStacks = PrecisionStacks;
    AbilityFeedback.ObservedActionState = ActionState;
    AbilityFeedback.ObservedActionStacks = ActionStacks;
}

void ADemoHUD::DrawAbilityEdgeFeedback(const ADemoGameState* State, const ADemoCharacter* Player, const UDemoAttributeSet* Attributes)
{
    DEMO_LOG_TICK();
    const UWorld* World = GetWorld();
    if (!Canvas || !State || !Player || !Attributes || !World || Player->IsCoopEliminated()) return;
    const double Now = World->GetTimeSeconds();
    const float W = Canvas->ClipX, H = Canvas->ClipY;

    // 冲刺左右侧速度边缘光（柔和不刺眼，最多覆盖边缘8%宽度）
    if (Now < AbilityFeedback.DashFlashUntil)
    {
        const float Alpha = FMath::Clamp(static_cast<float>((AbilityFeedback.DashFlashUntil - Now) / 0.32), 0.f, 1.f);
        const float Border = FMath::Min(54.f * UIScale, W * 0.08f);
        const FLinearColor DashTint = FLinearColor(0.62f, 0.88f, 1.0f).CopyWithNewOpacity(0.13f * Alpha);
        TArray<FCanvasUVTri> Triangles; Triangles.Reserve(4);
        AddAbilityEdge(Triangles, FVector2D(0, 0), FVector2D(0, H), FVector2D(Border, H - Border), FVector2D(Border, Border), DashTint);
        AddAbilityEdge(Triangles, FVector2D(W, 0), FVector2D(W, H), FVector2D(W - Border, H - Border), FVector2D(W - Border, Border), DashTint);
        FCanvasTriangleItem Item(Triangles, GWhiteTexture);
        Item.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Item);
    }

    // 治疗柔和翡翠绿外缘脉冲
    if (Now < AbilityFeedback.HealFlashUntil)
    {
        const float Alpha = FMath::Clamp(static_cast<float>((AbilityFeedback.HealFlashUntil - Now) / 0.55), 0.f, 1.f);
        const float Border = FMath::Min(62.f * UIScale, FMath::Min(W, H) * 0.09f);
        const FLinearColor HealTint = FLinearColor(0.32f, 0.86f, 0.56f).CopyWithNewOpacity(0.14f * Alpha);
        TArray<FCanvasUVTri> Triangles; Triangles.Reserve(8);
        AddAbilityEdge(Triangles, FVector2D(0, 0), FVector2D(W, 0), FVector2D(W - Border, Border), FVector2D(Border, Border), HealTint);
        AddAbilityEdge(Triangles, FVector2D(W, 0), FVector2D(W, H), FVector2D(W - Border, H - Border), FVector2D(W - Border, Border), HealTint);
        AddAbilityEdge(Triangles, FVector2D(W, H), FVector2D(0, H), FVector2D(Border, H - Border), FVector2D(W - Border, H - Border), HealTint);
        AddAbilityEdge(Triangles, FVector2D(0, H), FVector2D(0, 0), FVector2D(Border, Border), FVector2D(Border, H - Border), HealTint);
        FCanvasTriangleItem Item(Triangles, GWhiteTexture);
        Item.BlendMode = SE_BLEND_Translucent;
        Canvas->DrawItem(Item);
    }
}

void ADemoHUD::DrawAbilityProcToasts(const ADemoCharacter* Player, const UDemoAttributeSet* Attributes)
{
    DEMO_LOG_TICK();
    const UWorld* World = GetWorld();
    if (!Canvas || !Player || !Attributes || !World || Player->IsCoopEliminated()) return;
    const double Now = World->GetTimeSeconds();
    const float Bottom = ViewHeight - 132.f;

    // 左下生命读数上方绘制能力触发短提示（最多3条）
    float ToastY = Bottom - 32.f;
    for (int32 Index = AbilityFeedback.Toasts.Num() - 1; Index >= 0; --Index)
    {
        const FDemoAbilityToast& Toast = AbilityFeedback.Toasts[Index];
        const float Remaining = static_cast<float>(Toast.ExpireAt - Now);
        if (Remaining <= 0.f) continue;
        const float Alpha = FMath::Clamp(Remaining / 0.45f, 0.f, 1.f);
        const float Rise = (1.75f - FMath::Min(Remaining, 1.75f)) * 4.f;
        Label(Toast.Text, 44.f, ToastY - Rise, 14.f, Toast.Color.CopyWithNewOpacity(Alpha));
        ToastY -= 20.f;
    }

    // 若当前没有滚动提示，且已拥有至少1项天赋，显示轻量常驻天赋计数提示
    if (AbilityFeedback.Toasts.IsEmpty())
    {
        const int32 TalentCount = GetPlayerView().OwnedTalentViews.Num();
        if (TalentCount > 0)
        {
            const FLinearColor MutedColor = IsR02GameplayPresentation() && R02HUDTheme ? R02HUDTheme->Muted : DemoUI::Muted;
            Label(FString::Printf(TEXT("已激活天赋 ×%d  ·  [TAB] 查看"), TalentCount), 44.f, Bottom - 24.f, 12.f, MutedColor);
        }
    }
}
