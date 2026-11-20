// 职责：在 Unreal Editor 内生成真实 UMG/CommonUI Blueprint 与控件树。PowerShell 参数须整体引用 '-run=FPSDemoEditor.DemoCreateCommonUI'；默认只读复用，显式 -RepairDefaultFonts 只修复丢失字体，uasset 均由引擎保存。
#include "UI/DemoCreateCommonUICommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h" // 编译后直接核对默认 EWidgetTickFrequency，避免只凭父类猜测动画可运行。
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Debug/DemoLog.h"
#include "Engine/Font.h" // UFont 是可随资产持久化的 UObject；CoreStyle 的临时 CompositeFont 不能写入 Widget Blueprint。
#include "Fonts/SlateFontInfo.h" // 修复时完整复制字号、描边、材质等字体设置，仅替换丢失的 UObject 来源。
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UI/Common/DemoUIRoot.h"
#include "UI/DemoUIStyle.h" // 与现有Canvas及原生UMG回退共用中性灰主题。
#include "UI/Screens/DemoCasinoScreen.h"
#include "UI/Screens/DemoConfirmScreen.h"
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Screens/DemoLobbyScreen.h"
#include "UI/Screens/DemoSessionScreen.h"
#include "UI/Screens/DemoSettingsScreen.h"
#include "UI/Screens/DemoPlayerStatusScreen.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

namespace DemoCommonUIAuthoring
{
    /** 加载本引擎实际存在的字体资源；返回 UObject 供 FSlateFontInfo 序列化，失败必须阻止生成/修复保存。 */
    UFont* LoadDefaultFont()
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonUIAuthoring.LoadDefaultFont"));
        UFont* Font=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")); // 引擎 Content/EngineFonts/Roboto.uasset，不依赖本机安装字体或临时共享指针。
        if(!Font) { UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_FONT resource missing: /Engine/EngineFonts/Roboto.Roboto")); }
        return Font;
    }

    /** Blueprint 是本次新建资产；创建 Root 所需的两个真实 CommonUI 栈，具名字段由 native BindWidgetOptional 读取。 */
    void BuildRoot(UWidgetBlueprint* Blueprint)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonUIAuthoring.BuildRoot"));
        UWidgetTree* Tree=Blueprint->WidgetTree; // Blueprint 拥有模板控件树；运行时为每个本地玩家复制。
        UOverlay* Layers=Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("RootLayers")); // Overlay 顺序决定模态位于菜单之上。
        Layers->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        UCommonActivatableWidgetStack* Menu=Tree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(),TEXT("MenuStack")); // 本地菜单层，必须保留名称和类型。
        UCommonActivatableWidgetStack* Modal=Tree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(),TEXT("ModalStack")); // 本地顶层确认/文本输入层，不能与 MenuStack 合并。
        for(UCommonActivatableWidgetStack* Stack:{Menu,Modal})
        {
            Stack->bIsVariable=true;
            Stack->SetTransitionDuration(0.f); // 结构切换立即完成；玩法揭晓动画由页面内部表现层驱动。
            UOverlaySlot* Slot=Layers->AddChildToOverlay(Stack); // 仅本控件的布局槽，不跨编辑器操作保留。
            Slot->SetHorizontalAlignment(HAlign_Fill);
            Slot->SetVerticalAlignment(VAlign_Fill);
        }
        Modal->SetVisibility(ESlateVisibility::Collapsed); // 无弹窗时不覆盖菜单焦点或指针；UIManager 按 Flow 显示。
        Tree->RootWidget=Layers;
    }

    /** Tree 为当前资产控件树；Name 是原生绑定字段，Title/Size/Color 仅为 Designer 默认展示，不进入交易逻辑。 */
    UTextBlock* CreateHeading(UWidgetTree* Tree,FName Name,const FString& Title,int32 Size,FLinearColor Color)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonUIAuthoring.CreateHeading name=%s"),*Name.ToString());
        UTextBlock* Label=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name); // 同名控件由 native 页面绑定，Blueprint 实例持有。
        Label->bIsVariable=true;
        Label->SetText(FText::FromString(Title));
        UFont* Font=LoadDefaultFont(); // 必须保存 FontObject，不能把仅进程有效的 CoreStyle CompositeFont 指针写入模板。
        if(!Font) return nullptr;
        Label->SetFont(FSlateFontInfo(Font,Size,TEXT("Regular")));
        Label->SetAutoWrapText(true);
        Label->SetColorAndOpacity(FSlateColor(Color));
        return Label;
    }

    /** Blueprint 是新建页面，IsConfirm 决定设计尺寸；具名控件是稳定布局插槽，字体资源缺失返回 false，不能保存已知不可渲染页面。 */
    bool BuildPage(UWidgetBlueprint* Blueprint,bool IsConfirm)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonUIAuthoring.BuildPage confirm=%d"),IsConfirm);
        UWidgetTree* Tree=Blueprint->WidgetTree; // 资产持有模板，不创建玩家或调用业务对象。
        UBorder* Backdrop=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("FullscreenBackdrop")); // 全屏遮罩吸收下层指针。
        Backdrop->SetBrushColor(IsConfirm?DemoUI::Overlay:FLinearColor::Transparent);
        Backdrop->SetPadding(FMargin(24.f));
        UScaleBox* Scale=Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("ResponsiveScale")); // 小视口只缩小，不放大字体纹理。
        Scale->SetStretch(EStretch::ScaleToFit);
        Scale->SetStretchDirection(EStretchDirection::DownOnly);
        Backdrop->SetContent(Scale);
        USizeBox* Size=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("PanelBounds")); // Designer 中可调整的 DPI 前页面边界。
        Size->SetWidthOverride(IsConfirm?720.f:1080.f);
        Size->SetHeightOverride(IsConfirm?500.f:640.f);
        Scale->SetContent(Size);
        UBorder* Surface=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("PanelSurface")); // Designer 可编辑面板颜色与内边距。
        Surface->SetBrushColor(DemoUI::Surface.CopyWithNewOpacity(.45f));
        Surface->SetPadding(FMargin(24.f));
        Size->SetContent(Surface);
        UVerticalBox* Column=Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PageColumn")); // 标题/副标题自动高度，正文填满其余空间。
        Surface->SetContent(Column);
        UTextBlock* Heading=CreateHeading(Tree,TEXT("PageHeading"),IsConfirm?TEXT("操作确认"):TEXT("终端页面"),28,FLinearColor(.96f,.96f,.94f)); // 运行时由原生 SetHeading 替换文本。
        UTextBlock* Subtitle=CreateHeading(Tree,TEXT("PageSubtitle"),TEXT("运行时显示当前余额与提示"),16,FLinearColor(1.f,.76f,.28f)); // 只是 Designer 样例，不包含测试模式文案。
        if(!Heading||!Subtitle) return false;
        Column->AddChildToVerticalBox(Heading)->SetPadding(FMargin(4.f,5.f));
        Column->AddChildToVerticalBox(Subtitle)->SetPadding(FMargin(4.f,5.f));
        UScrollBox* Scroll=Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("PageScroll")); // 低分辨率和长文本始终可滚动访问。
        Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always);
        UVerticalBoxSlot* ScrollSlot=Column->AddChildToVerticalBox(Scroll); // 占据面板剩余高度，避免正文把确认按钮挤出屏幕。
        ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ScrollSlot->SetPadding(FMargin(0.f,12.f,0.f,0.f));
        UVerticalBox* Body=Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PageBody")); // 原生页面只在此插入真实控件；请把永久装饰放在其外部。
        Body->bIsVariable=true;
        Scroll->AddChild(Body);
        Tree->RootWidget=Backdrop;
        return true;
    }

    /** Blueprint 必须沿用 ExpectedParent，IsRoot 选择绑定契约；RequireCompiled 在编译后/复用时验证动画 Tick，新树编译前只检查绑定。 */
    bool Validate(UWidgetBlueprint* Blueprint,UClass* ExpectedParent,bool IsRoot,bool RequireCompiled=true)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonUIAuthoring.Validate asset=%s"),*GetNameSafe(Blueprint));
        if(!Blueprint||Blueprint->ParentClass!=ExpectedParent||!Blueprint->WidgetTree||!Blueprint->WidgetTree->RootWidget)
        {
            UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_ASSET invalid blueprint parent/tree asset=%s expected=%s"),*GetNameSafe(Blueprint),*GetNameSafe(ExpectedParent));
            return false;
        }
        UWidgetTree* Tree=Blueprint->WidgetTree; // 本函数只验证，不调用 Modify/Compile/Save 覆盖人工布局。
        if(IsRoot)
        {
            const UOverlay* Layers=Cast<UOverlay>(Tree->RootWidget); // 根层序必须表达菜单在下、模态在上。
            const UCommonActivatableWidgetStack* Menu=Cast<UCommonActivatableWidgetStack>(Tree->FindWidget(TEXT("MenuStack"))); // 需要真实 Stack，不能用同名普通 Panel 冒充。
            const UCommonActivatableWidgetStack* Modal=Cast<UCommonActivatableWidgetStack>(Tree->FindWidget(TEXT("ModalStack"))); // 模态控件不得与菜单共用同一个实例。
            if(!Layers||!Menu||!Modal||Menu==Modal||Menu->GetParent()!=Layers||Modal->GetParent()!=Layers
                ||Layers->GetChildIndex(Menu)>=Layers->GetChildIndex(Modal)||!FMath::IsNearlyZero(Menu->GetTransitionDuration())||!FMath::IsNearlyZero(Modal->GetTransitionDuration()))
            {
                UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_ASSET root requires ordered MenuStack/ModalStack with zero transition: %s"),*Blueprint->GetPathName());
                return false;
            }
        }
        else if(!Cast<UTextBlock>(Tree->FindWidget(TEXT("PageHeading")))||!Cast<UTextBlock>(Tree->FindWidget(TEXT("PageSubtitle")))||!Cast<UVerticalBox>(Tree->FindWidget(TEXT("PageBody"))))
        {
            UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_ASSET page requires PageHeading/TextBlock, PageSubtitle/TextBlock, PageBody/VerticalBox: %s"),*Blueprint->GetPathName());
            return false;
        }
        if(RequireCompiled&&(ExpectedParent->IsChildOf(UDemoCasinoScreen::StaticClass())||ExpectedParent->IsChildOf(UDemoTerminalScreen::StaticClass())||ExpectedParent->IsChildOf(UDemoPlayerStatusScreen::StaticClass()))) // 属性页依赖NativeTick刷新GAS文本，同样不能被蓝图关闭。
        {
            // UE5.4 编译器只检查直接 native parent 的 DisableNativeTick 元数据；不修改该元数据或强行改生成类，防止遮掩页面真实配置错误。
            const UWidgetBlueprintGeneratedClass* Generated=Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass); // 读取最终编译产物，而非仅猜测 C++ NativeTick 覆写能自动生效。
            const UUserWidget* Defaults=Generated?Generated->GetDefaultObject<UUserWidget>():nullptr; // 只读默认 TickFrequency，Auto 才允许局部 UMG 动画逐帧更新。
            const bool NativeDisabled=ExpectedParent->HasMetaData(TEXT("DisableNativeTick")); // 日志明确区分元数据抑制和蓝图 TickFrequency=Never。
            if(!Generated||!Generated->ClassRequiresNativeTick()||!Defaults||Defaults->GetDesiredTickFrequency()!=EWidgetTickFrequency::Auto)
            {
                UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_ASSET native animation tick disabled asset=%s nativeMetadataDisabled=%d classRequiresNativeTick=%d autoFrequency=%d"),
                    *Blueprint->GetPathName(),NativeDisabled,Generated&&Generated->ClassRequiresNativeTick(),Defaults&&Defaults->GetDesiredTickFrequency()==EWidgetTickFrequency::Auto);
                return false;
            }
            UE_LOG(LogFPSDemo,Display,TEXT("COMMON_UI_ASSET_TICK_VERIFIED %s nativeMetadataDisabled=%d classRequiresNativeTick=1 autoFrequency=1"),*Blueprint->GetPathName(),NativeDisabled);
        }
        return true;
    }

    /** 显式修复模式只处理两标题：FontObject 为空且 CompositeFont 失效时替换字体来源，保留字号/描边/材质/布局；返回修复数量，-1 为资源失败。 */
    int32 RepairMissingDefaultFonts(UWidgetBlueprint* Blueprint)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonUIAuthoring.RepairMissingDefaultFonts %s"),*Blueprint->GetPathName());
        int32 Changed=0; // 当前资产实际修复的标题数量；为 0 时不编译、不保存。
        UFont* Resource=nullptr; // 仅首次发现缺失时加载，随后由控件的 FontObject 持有。
        for(const TCHAR* Name:{TEXT("PageHeading"),TEXT("PageSubtitle")})
        {
            UTextBlock* Label=Cast<UTextBlock>(Blueprint->WidgetTree->FindWidget(FName(Name))); // Validate 已保证类型和名称，仍检查以拒绝不完整人工改动。
            if(!Label) { UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_FONT_REPAIR missing required text %s"),Name); return -1; }
            FSlateFontInfo Font=Label->GetFont(); // 完整复制，不能重建默认结构而丢失人工字号、outline、字距或材质。
            if(Font.FontObject||Font.CompositeFont.IsValid())
            {
                UE_LOG(LogFPSDemo,Log,TEXT("COMMON_UI_FONT_REPAIR_SKIPPED asset=%s widget=%s reason=font_source_present"),*Blueprint->GetPathName(),Name);
                continue;
            }
            if(!Resource) Resource=LoadDefaultFont();
            if(!Resource) return -1;
            if(Changed==0) Blueprint->Modify(); // 只有明确命中本次已知丢失字体条件才修改资产。
            Label->Modify();
            Font.FontObject=Resource;
            if(Font.TypefaceFontName.IsNone()) Font.TypefaceFontName=TEXT("Regular"); // 原 Typeface 名存在时保留，不改人工字重选择。
            Label->SetFont(Font);
            ++Changed;
            UE_LOG(LogFPSDemo,Display,TEXT("COMMON_UI_FONT_REPAIRED asset=%s widget=%s font=%s size=%.2f outline=%d layout_preserved=1"),
                *Blueprint->GetPathName(),Name,*Resource->GetPathName(),Font.Size,Font.OutlineSettings.OutlineSize);
        }
        return Changed;
    }

    /** 仅新建或显式命中字体修复的 Blueprint 可调用；Parent/IsRoot 保持既有验证契约，IsNew 控制 AssetRegistry 注册，不保存其他包。 */
    bool CompileAndSave(UWidgetBlueprint* Blueprint,UClass* Parent,bool IsRoot,const FString& PackageName,bool IsNew)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonUIAuthoring.CompileAndSave %s new=%d"),*PackageName,IsNew);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
        FKismetEditorUtilities::CompileBlueprint(Blueprint);
        if(Blueprint->Status==BS_Error||!Blueprint->GeneratedClass||!Blueprint->GeneratedClass->IsChildOf(Parent))
        {
            UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_ASSET blueprint compilation failed: %s"),*Blueprint->GetPathName());
            return false;
        }
        if(!Validate(Blueprint,Parent,IsRoot)) return false; // 编译成功仍须验证 NativeTick，不能保存不可动画的资产。
        if(IsNew) FAssetRegistryModule::AssetCreated(Blueprint);
        Blueprint->MarkPackageDirty();
        const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension()); // /Game 映射到本项目 Content，序列化始终由引擎完成。
        if(!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true))
        {
            UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_ASSET target directory unavailable: %s"),*Filename);
            return false;
        }
        FSavePackageArgs SaveArgs; // 只提交当前新建/显式修复的包，保留其他未提交的编辑器内容。
        SaveArgs.TopLevelFlags=RF_Public|RF_Standalone;
        SaveArgs.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(Blueprint->GetOutermost(),Blueprint,*Filename,SaveArgs))
        {
            UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_ASSET SavePackage failed: %s"),*Filename);
            return false;
        }
        return true;
    }

    /** PackageName 为固定路径；默认只读复用，RepairFonts 仅允许修复两标题的双空字体来源，IsConfirm 只影响新建默认尺寸。 */
    bool CreateOrValidate(const FString& PackageName,UClass* Parent,bool IsRoot,bool IsConfirm=false,bool RepairFonts=false)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] CommonUIAuthoring.CreateOrValidate %s"),*PackageName);
        const FString Name=FPackageName::GetLongPackageAssetName(PackageName); // 对象名与包叶名保持一致，运行时软类路径稳定。
        const FString ObjectPath=PackageName+TEXT(".")+Name; // 只加载实际资产；GeneratedClass 由编译器创建。
        if(FPackageName::DoesPackageExist(PackageName)||FindObject<UObject>(nullptr,*ObjectPath))
        {
            UWidgetBlueprint* Existing=LoadObject<UWidgetBlueprint>(nullptr,*ObjectPath); // 默认保留人工 Designer 工作；只有显式字体修复且命中条件才编译保存。
            if(!Validate(Existing,Parent,IsRoot)||!Existing->GeneratedClass||!Existing->GeneratedClass->IsChildOf(Parent)||Existing->Status==BS_Error)
            {
                UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_ASSET existing asset invalid; preserved without overwrite: %s"),*ObjectPath);
                return false;
            }
            if(RepairFonts&&!IsRoot)
            {
                const int32 Changed=RepairMissingDefaultFonts(Existing); // 只修复原生绑定的两标题，不遍历或重排任何人工布局。
                if(Changed<0) return false;
                if(Changed>0)
                {
                    if(!CompileAndSave(Existing,Parent,IsRoot,PackageName,false)) return false;
                    UE_LOG(LogFPSDemo,Display,TEXT("COMMON_UI_FONT_REPAIR_SAVED asset=%s fields=%d layout_preserved=1"),*ObjectPath,Changed);
                    return true;
                }
            }
            UE_LOG(LogFPSDemo,Display,TEXT("COMMON_UI_ASSET_REUSED %s"),*ObjectPath);
            return true;
        }
        UPackage* Package=CreatePackage(*PackageName); // 只为明确目标创建 UObject 包，不写二进制资产文件。
        UWidgetBlueprint* Blueprint=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(Parent,Package,*Name,BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass(),TEXT("DemoCreateCommonUI"))); // 正式编辑器 Blueprint 类型及生成类。
        if(!Blueprint)
        {
            UE_LOG(LogFPSDemo,Error,TEXT("COMMON_UI_ASSET CreateBlueprint failed: %s"),*ObjectPath);
            return false;
        }
        if(!Blueprint->WidgetTree) Blueprint->WidgetTree=NewObject<UWidgetTree>(Blueprint,TEXT("WidgetTree"),RF_Transactional|RF_ArchetypeObject);
        if(IsRoot) BuildRoot(Blueprint); else if(!BuildPage(Blueprint,IsConfirm)) return false;
        if(!Validate(Blueprint,Parent,IsRoot,false)) return false; // 新树尚未编译，不把临时生成类状态当作最终 Tick 结论。
        if(!CompileAndSave(Blueprint,Parent,IsRoot,PackageName,true)) return false;
        UE_LOG(LogFPSDemo,Display,TEXT("COMMON_UI_ASSET_CREATED %s"),*ObjectPath);
        return true;
    }
}

UDemoCreateCommonUICommandlet::UDemoCreateCommonUICommandlet()
{
    DEMO_LOG_CALL();
    IsClient=false; IsServer=false; IsEditor=true;
    LogToConsole=true; ShowErrorCount=true;
}

int32 UDemoCreateCommonUICommandlet::Main(const FString& Params)
{
    DEMO_LOG_CALL();
    const bool RepairFonts=FParse::Param(*Params,TEXT("RepairDefaultFonts")); // 只接受显式开关；默认重复执行不会重写任何已有资产。
    if(RepairFonts) { UE_LOG(LogFPSDemo,Display,TEXT("COMMON_UI_FONT_REPAIR requested: only missing PageHeading/PageSubtitle font sources; existing layout preserved")); }
    bool Succeeded=true; // 任一资产失败都会让命令行失败，仍检查其余目标以一次汇总错误。
    Succeeded&=DemoCommonUIAuthoring::CreateOrValidate(TEXT("/Game/UI/Common/WBP_UIRoot"),UDemoUIRoot::StaticClass(),true,false,RepairFonts);
    Succeeded&=DemoCommonUIAuthoring::CreateOrValidate(TEXT("/Game/UI/Screens/WBP_Terminal"),UDemoTerminalScreen::StaticClass(),false,false,RepairFonts);
    Succeeded&=DemoCommonUIAuthoring::CreateOrValidate(TEXT("/Game/UI/Screens/WBP_Casino"),UDemoCasinoScreen::StaticClass(),false,false,RepairFonts);
    Succeeded&=DemoCommonUIAuthoring::CreateOrValidate(TEXT("/Game/UI/Screens/WBP_Confirm"),UDemoConfirmScreen::StaticClass(),false,true,RepairFonts);
    Succeeded&=DemoCommonUIAuthoring::CreateOrValidate(TEXT("/Game/UI/Screens/WBP_Lobby"),UDemoLobbyScreen::StaticClass(),false,false,RepairFonts);
    Succeeded&=DemoCommonUIAuthoring::CreateOrValidate(TEXT("/Game/UI/Screens/WBP_Session"),UDemoSessionScreen::StaticClass(),false,false,RepairFonts);
    Succeeded&=DemoCommonUIAuthoring::CreateOrValidate(TEXT("/Game/UI/Screens/WBP_Settings"),UDemoSettingsScreen::StaticClass(),false,false,RepairFonts);
    Succeeded&=DemoCommonUIAuthoring::CreateOrValidate(TEXT("/Game/UI/Screens/WBP_PlayerStatus"),UDemoPlayerStatusScreen::StaticClass(),false,false,RepairFonts);
    if(Succeeded) { UE_LOG(LogFPSDemo,Display,TEXT("DEMO_COMMON_UI_ASSETS_SUCCESS count=8 existing_layouts_preserved")); }
    else { UE_LOG(LogFPSDemo,Error,TEXT("DEMO_COMMON_UI_ASSETS_FAILED see validation/compile/save diagnostics")); }
    return Succeeded?0:1;
}
