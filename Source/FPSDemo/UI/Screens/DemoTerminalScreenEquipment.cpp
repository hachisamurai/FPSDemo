// 职责：R04终端武器/弹药的目录、产品大图、基础参数对比与异步软资源。使用：DemoTerminalScreen按Flow子页调用，只读CDO和拥有者报价。
// 关键依赖：DemoUI字体/配色、CommonUI原动作、Config.Icon及可配置产品图。R02另文件只替表现；武器目录仅预览、明确装配直接提交原装备请求，弹药购买仍保确认；销毁取消弱绑定请求。
#include "UI/Screens/DemoTerminalScreen.h"
#include "UI/Common/DemoMenuButton.h"
#include "UI/Flow/DemoMenuFlowComponent.h"
#include "UI/DemoUIStyle.h"
#include "Player/DemoPlayerController.h"
#include "Player/DemoPlayerProfile.h"
#include "Player/DemoCloudSync.h"
#include "Characters/DemoCharacter.h"
#include "Weapons/DemoWeaponComponent.h"
#include "Weapons/DemoWeaponBase.h"
#include "Weapons/DemoWeaponCatalog.h"
#include "Weapons/Ammo/DemoAmmoComponent.h"
#include "Weapons/Ammo/DemoAmmoCatalog.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Debug/DemoLog.h"

UDemoTerminalScreen::UDemoTerminalScreen(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
    DEMO_LOG_CALL();
    // 只保存软路径；资源首次进入武器页才批量请求，CDO不做磁盘IO。产品图来自实际四枪配对源，不替换Config.Icon。
    WeaponPresentationImages={
        TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/UI/RedesignR04/Textures/T_Product_Pistol.T_Product_Pistol"))),
        TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/UI/RedesignR04/Textures/T_Product_Rifle.T_Product_Rifle"))),
        TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/UI/RedesignR04/Textures/T_Product_Shotgun.T_Product_Shotgun"))),
        TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/UI/RedesignR04/Textures/T_Product_Sniper.T_Product_Sniper")))
    };
    WeaponPresentationUVs={FVector4(0,0,1,1),FVector4(0,.28f,1,.72f),FVector4(0,.28f,1,.72f),FVector4(0,.28f,1,.72f)};
}

void UDemoTerminalScreen::NoteWeaponInspection(int32 Index)
{
    DEMO_LOG_CALL();
    if(Index<0||Index>3)
    { UE_LOG(LogFPSDemo,Warning,TEXT("UIR_EQUIPMENT rejected presentation index=%d"),Index); return; }
    SelectedWeaponIndex=Index;
    UE_LOG(LogFPSDemo,Log,TEXT("UIR_EQUIPMENT presentation weapon=%d; equipment unchanged"),Index);
}

int32 UDemoTerminalScreen::GetPresentationWeaponIndex() const
{
    DEMO_LOG_TICK();
    return SelectedWeaponIndex;
}

UVerticalBox* UDemoTerminalScreen::BuildEquipmentColumns(UVerticalBox*& OutCatalog)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Content=AddRow(PageBody); // 当前树拥有的左目录/中产品双列，右参数使用共用固定侧栏。
    USizeBox* CatalogBounds=WidgetTree->ConstructWidget<USizeBox>(); // 固定目录宽度，不随某项长文或大图尺寸挤动。
    CatalogBounds->SetWidthOverride(260.f);
    auto* CatalogSlot=Content->AddChildToHorizontalBox(CatalogBounds); // 目录自动宽度，中央列填充剩余空间。
    CatalogSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    CatalogSlot->SetPadding(FMargin(0,0,28,0));
    OutCatalog=WidgetTree->ConstructWidget<UVerticalBox>();
    CatalogBounds->SetContent(OutCatalog);
    return AddColumn(Content);
}

void UDemoTerminalScreen::AddEquipmentStat(UPanelWidget* Parent,const FString& Title,const FString& Value)
{
    DEMO_LOG_CALL();
    UHorizontalBox* Row=AddRow(Parent); // 本条真实参数名称与值，不创建虚假的强度评分。
    UTextBlock* Caption=AddText(Row,Title,14,DemoUI::Muted); // 紧凑参数标题保留单位，单行不挤占CTA的初屏位置。
    UTextBlock* Number=AddText(Row,Value,26,DemoUI::Ink); // 值仍来自只读CDO或权威快照；26号读数与407逻辑高正文匹配。
    bool bPureNumber=!Value.IsEmpty(); // 只有独立ASCII数值可用UE固定数字格；散弹的“8 × 8”必须保留普通混排。
    for(const TCHAR Character:Value) if((Character<TEXT('0')||Character>TEXT('9'))&&Character!=TEXT('.')&&Character!=TEXT('-')&&Character!=TEXT('+')) { bPureNumber=false; break; }
    Number->SetFont(bPureNumber?DemoUI::NumberFont(26):DemoUI::DisplayFont(26,TEXT("Medium")));
    Caption->SetAutoWrapText(false); Caption->SetWrapTextAt(0.f);
    Number->SetAutoWrapText(false); Number->SetWrapTextAt(0.f);
    if(auto* CaptionSlot=Cast<UHorizontalBoxSlot>(Caption->Slot)) CaptionSlot->SetPadding(FMargin(0)); // 避免两列共用助手各加6px上下再叠四行。
    if(auto* NumberSlot=Cast<UHorizontalBoxSlot>(Number->Slot)) NumberSlot->SetPadding(FMargin(0));
    Number->SetJustification(ETextJustify::Right);
    UBorder* Rule=WidgetTree->ConstructWidget<UBorder>(); // 1px组织线，不把数值当作伪造归一化进度。
    Rule->SetPadding(FMargin(0)); Rule->SetBrushColor(DemoUI::Edge);
    USizeBox* RuleBounds=WidgetTree->ConstructWidget<USizeBox>(); RuleBounds->SetHeightOverride(1.f); RuleBounds->SetContent(Rule);
    auto* RuleSlot=Parent->AddChild(RuleBounds); // 当前参数容器拥有，在页面重建时一起释放。
    if(auto* Vertical=Cast<UVerticalBoxSlot>(RuleSlot)) Vertical->SetPadding(FMargin(0,3,0,5));
}

void UDemoTerminalScreen::AddEquipmentArtwork(UPanelWidget* Parent,UTexture2D* Texture,int32 ProductIndex,bool bProduct,bool bDimmed)
{
    DEMO_LOG_CALL();
    if(!Texture)
    { UE_LOG(LogFPSDemo,Log,TEXT("UIR_EQUIPMENT artwork pending index=%d"),ProductIndex); AddText(Parent,TEXT("展示图加载中"),17,DemoUI::Muted); return; }
    USizeBox* Bounds=WidgetTree->ConstructWidget<USizeBox>(); // 仅控制展示行高度，宽度跟随中列可用区域。
    Bounds->SetHeightOverride(UsesR02Presentation()?(ProductIndex>=0?270.f:128.f):(ProductIndex>=0?148.f:160.f)); // R02 C04真枪大图展台放大至270，弹药图保持128。
    auto* ArtworkSlot=Parent->AddChild(Bounds); // 当前树拥有，不持有玩家/异步状态。
    if(auto* Vertical=Cast<UVerticalBoxSlot>(ArtworkSlot)) Vertical->SetPadding(ProductIndex>=0?FMargin(0,4,0,6):FMargin(0,15,0,18));
    UScaleBox* Fit=WidgetTree->ConstructWidget<UScaleBox>(); // 等比缩放，禁止按现成目录Icon的正方形拉伸枪械。
    Fit->SetStretch(EStretch::ScaleToFit); Fit->SetStretchDirection(EStretchDirection::DownOnly); Bounds->SetContent(Fit);
    UImage* Image=WidgetTree->ConstructWidget<UImage>(); // 纹理由页面强引用数组/目录保活，图像不负责加载。
    Image->SetBrushFromTexture(Texture,true); // 必须取源尺寸；默认false会保留32×32 Brush，DownOnly将大图误缩为小图标。
    if(bProduct&&WeaponPresentationUVs.IsValidIndex(ProductIndex))
    {
        const FVector4 UV=WeaponPresentationUVs[ProductIndex]; // 只接受有限、合法的归一化矩形；自定义展示图可在蓝图重配。
        if(FMath::IsFinite(UV.X)&&FMath::IsFinite(UV.Y)&&FMath::IsFinite(UV.Z)&&FMath::IsFinite(UV.W)&&UV.X>=0&&UV.Y>=0&&UV.Z<=1&&UV.W<=1&&UV.Z>UV.X&&UV.W>UV.Y)
        {
            FSlateBrush Brush=Image->GetBrush(); // 仅绘制时取已核对透明留白，不改变原纹理、模型或动画源。
            Brush.SetUVRegion(FBox2D(FVector2D(UV.X,UV.Y),FVector2D(UV.Z,UV.W)));
            Brush.ImageSize=FVector2D(Texture->GetSizeX()*(UV.Z-UV.X),Texture->GetSizeY()*(UV.W-UV.Y));
            Image->SetBrush(Brush);
        }
        else UE_LOG(LogFPSDemo,Warning,TEXT("UIR_EQUIPMENT invalid product UV index=%d; using full image"),ProductIndex);
    }
    Image->SetColorAndOpacity(bDimmed?FLinearColor(.90f,.90f,.90f,1.f):FLinearColor::White); // 锁定仍走Slate去色，避免重复压低亮度/Alpha使轮廓消失。
    Image->SetIsEnabled(!bDimmed); // 只让图像使用Slate灰阶禁用效果；父目录按钮仍可点击，锁定Tips不受影响。
    auto* ImageSlot=Cast<UScaleBoxSlot>(Fit->AddChild(Image)); // 居中且保持原比例，透明源也不会挤动目录。
    if(ImageSlot) { ImageSlot->SetHorizontalAlignment(HAlign_Center); ImageSlot->SetVerticalAlignment(VAlign_Center); }
}

void UDemoTerminalScreen::AddEquipmentComparison(UPanelWidget* Parent,const ADemoWeaponBase* Selected,const ADemoWeaponBase* Baseline,bool EmptyPrimary)
{
    DEMO_LOG_CALL();
    if(!Selected||!Baseline)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("UIR_EQUIPMENT comparison waiting for catalog definitions"));
        AddText(Parent,TEXT("装备基准同步中；基础参数就绪后显示对比。"),16,DemoUI::Muted); return;
    }
    const FString BaselineName=Baseline->Config.DisplayName.ToString(); // 快照指定主武器的目录CDO，不取手中实例或队友装备。
    UTextBlock* Heading=AddText(Parent,EmptyPrimary?FString::Printf(TEXT("未装配主武器  /  以固定副武器「%s」为基准"),*BaselineName):FString::Printf(TEXT("对比当前主武器  /  %s"),*BaselineName),16,DemoUI::Gold);
    if(auto* HeadingSlot=Cast<UVerticalBoxSlot>(Heading->Slot)) HeadingSlot->SetPadding(FMargin(4,3));
    const TCHAR* Titles[]={TEXT("属性"),TEXT("基准"),TEXT("当前预览"),TEXT("差值")}; // 四列均为同口径基础配置，差值不冒充完整性能评级。
    UHorizontalBox* Header=AddRow(Parent); // 当前树短表，不创建可点击元素或改变右侧原装备CTA。
    for(const TCHAR* Title:Titles)
    {
        UTextBlock* Cell=AddText(Header,Title,13,DemoUI::Muted); // 文本字号按R04表头处理，值列保持右齐。
        Cell->SetAutoWrapText(false);
        if(auto* CellSlot=Cast<UHorizontalBoxSlot>(Cell->Slot)) CellSlot->SetPadding(FMargin(4,2));
    }
    const TCHAR* Labels[]={TEXT("伤害 / 单弹丸"),TEXT("射速 / RPM"),TEXT("弹匣 / 发"),TEXT("装填 / 秒")}; // 伤害必须按单弹丸比较，散弹弹丸数在下行另列。
    const float Reference[]={Baseline->Config.BaseDamage,Baseline->Config.RoundsPerMinute,static_cast<float>(Baseline->Config.MagazineCapacity),Baseline->Config.ReloadSeconds}; // 同一个参考CDO的四项原始配置。
    const float Preview[]={Selected->Config.BaseDamage,Selected->Config.RoundsPerMinute,static_cast<float>(Selected->Config.MagazineCapacity),Selected->Config.ReloadSeconds}; // 当前预览CDO，不含武器实例或个人GAS增益。
    for(int32 Index=0;Index<4;++Index)
    {
        const float Difference=Preview[Index]-Reference[Index]; // 仅同名基础字段相减，不计算DPS、TTK或总伤害。
        const bool Changed=!FMath::IsNearlyZero(Difference,.01f); // 微小浮点噪声显示0，不虚构精度收益。
        const FString ReferenceValue=Index==3?FString::Printf(TEXT("%.1f"),Reference[Index]):FString::Printf(TEXT("%.0f"),Reference[Index]); // 秒数保留一位，其余用目录面板相同的整数精度。
        const FString PreviewValue=Index==3?FString::Printf(TEXT("%.1f"),Preview[Index]):FString::Printf(TEXT("%.0f"),Preview[Index]);
        const FString Delta=Changed?(Index==3?FString::Printf(TEXT("%+.1f"),Difference):FString::Printf(TEXT("%+.0f"),Difference)):FString(TEXT("—")); // 固定格式字面量保持UE编译期格式检查有效。
        const FString Values[]={Labels[Index],ReferenceValue,PreviewValue,Delta}; // 本行四个短值，只在构建时存在。
        UHorizontalBox* Row=AddRow(Parent); // 行高由真实字体控制，避免说明字符串改变四列基线。
        for(int32 Column=0;Column<4;++Column)
        {
            const FLinearColor Color=Column==3&&Changed?DemoUI::Gold:Column==2?DemoUI::Ink:DemoUI::Muted; // 香槟色仅表示数值有变化，不暗示整把武器更优。
            UTextBlock* Cell=AddText(Row,Values[Column],15,Color);
            if(Column>0&&Values[Column]!=TEXT("—")) Cell->SetFont(DemoUI::NumberFont(17));
            Cell->SetAutoWrapText(false); Cell->SetWrapTextAt(0.f);
            if(auto* CellSlot=Cast<UHorizontalBoxSlot>(Cell->Slot)) CellSlot->SetPadding(FMargin(4,1));
        }
    }
    UTextBlock* Note=AddText(Parent,FString::Printf(TEXT("基础配置，不含个人增益  ·  弹丸数：基准 %d / 预览 %d"),Baseline->GetPelletCount(),Selected->GetPelletCount()),13,DemoUI::Muted); // 弹丸数显式列出，不能把单弹丸差值当总伤害优劣。
    if(auto* NoteSlot=Cast<UVerticalBoxSlot>(Note->Slot)) NoteSlot->SetPadding(FMargin(4,4,4,1));
}

void UDemoTerminalScreen::PrepareWeaponIcons(const UDemoWeaponComponent* Equipment)
{
    DEMO_LOG_CALL();
    TArray<FSoftObjectPath> Paths; // 前四项来自Config.Icon，后四项只供大图；同一批请求，不在Tick同步加载。
    for(int32 Index=0;Index<4;++Index)
    {
        const ADemoWeaponBase* Definition=Equipment?Equipment->GetCatalogWeapon(Index):nullptr; // 借用已缓存武器CDO，不生成锁定武器Actor。
        Paths.Add(Definition?Definition->Config.Icon.ToSoftObjectPath():FSoftObjectPath());
    }
    for(int32 Index=0;Index<4;++Index) Paths.Add(WeaponPresentationImages.IsValidIndex(Index)?WeaponPresentationImages[Index].ToSoftObjectPath():FSoftObjectPath());
    if(Paths==WeaponIconPaths) return;
    if(WeaponIconLoadHandle) { WeaponIconLoadHandle->CancelHandle(); WeaponIconLoadHandle.Reset(); }
    WeaponIconPaths=MoveTemp(Paths); WeaponIcons.SetNum(WeaponIconPaths.Num());
    TArray<FSoftObjectPath> PendingPaths; // 当前请求的去重列表，不向外部存储公开目录信息。
    for(int32 Index=0;Index<WeaponIconPaths.Num();++Index)
    {
        WeaponIcons[Index]=Cast<UTexture2D>(WeaponIconPaths[Index].ResolveObject());
        if(!WeaponIconPaths[Index].IsNull()&&!WeaponIcons[Index]) PendingPaths.AddUnique(WeaponIconPaths[Index]);
    }
    if(PendingPaths.IsEmpty()) return;
    // UObject弱绑定，无Lambda捕获；StreamableManager在游戏线程调用，NativeDestruct显式取消避免旅行迟到。
    WeaponIconLoadHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(PendingPaths,FStreamableDelegate::CreateUObject(this,&UDemoTerminalScreen::OnWeaponIconsLoaded));
    if(!WeaponIconLoadHandle) UE_LOG(LogFPSDemo,Warning,TEXT("UMG_TERMINAL weapon icon request failed; text fallback remains usable"));
}

void UDemoTerminalScreen::OnWeaponIconsLoaded()
{
    DEMO_LOG_CALL();
    for(int32 Index=0;Index<WeaponIconPaths.Num();++Index)
    {
        WeaponIcons[Index]=Cast<UTexture2D>(WeaponIconPaths[Index].ResolveObject());
        if(!WeaponIconPaths[Index].IsNull()&&!WeaponIcons[Index]) UE_LOG(LogFPSDemo,Warning,TEXT("UMG_TERMINAL missing %s %s; readable fallback remains"),Index<4?TEXT("Config.Icon"):TEXT("product artwork"),*WeaponIconPaths[Index].ToString());
    }
    WeaponIconLoadHandle.Reset();
    ++WeaponIconRevision; // 下次协调器刷新重建一次，不在异步回调里修改正在构建的Widget树。
}

void UDemoTerminalScreen::BuildWeapons(const ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    if(UsesR02Presentation()) { BuildR02Weapons(Player); return; } // 只替换已授权S1的表现；原资源、目录选择和详情意图保持同一路由。
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 解锁/主槽采用服务器已经确认的拥有者值。
    const UDemoWeaponComponent* Equipment=Player?Player->GetWeaponComponent():nullptr; // Avatar可在重生阶段暂缺。
    PrepareWeaponIcons(Equipment);
    SetHeading(TEXT("武器库"),TEXT("查看基础配置  /  主武器 [1]  /  固定手枪 [2]"));
    if(SelectedWeaponIndex<0||SelectedWeaponIndex>3)
    {
        const int32 EquippedIndex=DemoWeaponCatalog::IndexOf(View.PrimaryWeaponId); // 初始只投影服务器已确认主枪，空主槽展示默认手枪。
        SelectedWeaponIndex=EquippedIndex>=1&&EquippedIndex<=3?EquippedIndex:0;
    }
    UVerticalBox* Catalog=nullptr; // BuildEquipmentColumns输出本次树中的目录列，不跨重建保存。
    UVerticalBox* Hero=BuildEquipmentColumns(Catalog); // 中央区展示真实模型名称与产品图。
    for(int32 Index=0;Index<4;++Index)
    {
        const ADemoWeaponBase* Definition=Equipment?Equipment->GetCatalogWeapon(Index):nullptr; // 蓝图基础参数，不混入本局GAS增益。
        const FName Id=DemoWeaponCatalog::IdAt(Index); // 原四枪目录稳定键，不能按显示名猜身份。
        const bool Unlocked=View.Progress.UnlockedWeaponIds.Contains(Id); // 当前本人权限，不借房主GI解锁。
        const bool Equipped=Index==0||View.PrimaryWeaponId==Id; // 副武器固定保留，主槽由快照确定。
        UHorizontalBox* Row=AddRow(Catalog); // 目录保持固定宽度和四项高度，点击仍打开原详情/装配模态。
        if(WeaponIcons.IsValidIndex(Index)&&WeaponIcons[Index])
        {
            UTexture2D* Texture=WeaponIcons[Index]; // 页面强持有的现成纹理；横向武器图不能强拉成正方形。
            UImage* Icon=AddIcon(Row,Texture,64.f); // 目录小图只读取Config.Icon软引用，和纯展示产品图分开。
            const float Scale=FMath::Min(72.f/FMath::Max(1,Texture->GetSizeX()),56.f/FMath::Max(1,Texture->GetSizeY())); // 72×56等比边界。
            Icon->SetDesiredSizeOverride(FVector2D(Texture->GetSizeX()*Scale,Texture->GetSizeY()*Scale));
            if(!Unlocked) { Icon->SetColorAndOpacity(FLinearColor(.95f,.95f,.95f,1.f)); Icon->SetIsEnabled(false); } // Slate负责去色；不再叠低Alpha/暗Tint，锁定轮廓清楚且条件按钮仍可查看。
        }
        else AddText(Row,TEXT("◇"),25,DemoUI::Muted); // 缺资源仍可读名称、状态及详情按钮。
        UVerticalBox* Description=AddColumn(Row); // 当前树拥有的武器正文列，目录每项约74逻辑高，四项初屏可见。
        if(auto* DescriptionSlot=Cast<UHorizontalBoxSlot>(Description->Slot)) DescriptionSlot->SetPadding(FMargin(8,0)); // 目录不再叠加通用正文列的16px上下留白。
        const FString Name=Definition?Definition->Config.DisplayName.ToString():TEXT("资源未就绪"); // 已加载CDO展示名，缺失不允许隐式生成Actor。
        UDemoMenuButton* Entry=AddAction(Description,FName(*FString::Printf(TEXT("Weapon%d"),Index)),Name,true,Index==SelectedWeaponIndex); // 锁定项可看条件，不能据此变为可装备。
        Entry->SetVisualRole(EDemoButtonVisualRole::Catalog); Entry->SetMinDimensions(140.f,42.f); Entry->UseSingleLineLabel();
        if(auto* EntrySlot=Cast<UVerticalBoxSlot>(Entry->Slot)) EntrySlot->SetPadding(FMargin(0));
        UTextBlock* Status=AddText(Description,!Unlocked?TEXT("锁定 · 查看条件"):Equipped?(Index==0?TEXT("固定副武器"):TEXT("主武器已装备")):TEXT("已解锁 · 可装备"),12,Unlocked?DemoUI::Muted:DemoUI::Gold); // 目录次级状态与完整条件分工，完整条件继续在右侧/模态显示。
        Status->SetAutoWrapText(false); Status->SetWrapTextAt(0.f);
        if(auto* StatusSlot=Cast<UVerticalBoxSlot>(Status->Slot)) StatusSlot->SetPadding(FMargin(0));
        AddGap(Catalog,4.f);
    }
    AddGap(Catalog,8.f);
    AddText(Catalog,TEXT("默认携带手枪\n在终端选择主武器。"),13,DemoUI::Muted);
    const ADemoWeaponBase* Selected=Equipment?Equipment->GetCatalogWeapon(SelectedWeaponIndex):nullptr; // 当前选中只读CDO，不生成或替换Actor。
    const bool Owned=View.Progress.UnlockedWeaponIds.Contains(DemoWeaponCatalog::IdAt(SelectedWeaponIndex)); // 服务器已确认解锁。
    UVerticalBox* Details=ShowAside(); // 参数、条件和查看CTA独立于主正文滚动。
    if(Selected)
    {
        FString Model=Selected->Config.StaticMesh?Selected->Config.StaticMesh->GetName():Selected->Config.Mesh?Selected->Config.Mesh->GetName():DemoWeaponCatalog::IdAt(SelectedWeaponIndex).ToString(); // 型号取实际CDO模型名，避免引用历史枪型。
        Model.RemoveFromStart(TEXT("SKM_")); Model.RemoveFromStart(TEXT("SK_")); Model.RemoveFromStart(TEXT("SM_"));
        AddText(Hero,FString::Printf(TEXT("FIELD ARMORY  /  %02d · 04"),SelectedWeaponIndex+1),15,DemoUI::Muted)->SetFont(DemoUI::DisplayFont(15,TEXT("Medium")));
        FSlateFontInfo ModelFont=DemoUI::DisplayFont(46,TEXT("Medium")); // 产品型号与下方基础对比共用中央区，字体仍由R04离线资产提供。
        ModelFont.LetterSpacing=40;
        AddText(Hero,Model.ToUpper(),46,DemoUI::Ink)->SetFont(ModelFont);
        UTexture2D* Product=WeaponIcons.IsValidIndex(SelectedWeaponIndex+4)?WeaponIcons[SelectedWeaponIndex+4].Get():nullptr; // 已异步加载产品图；缺失才回退Config.Icon。
        UTexture2D* Icon=WeaponIcons.IsValidIndex(SelectedWeaponIndex)?WeaponIcons[SelectedWeaponIndex].Get():nullptr; // 目录图标保活于同一页面缓存。
        AddEquipmentArtwork(Hero,Product?Product:Icon,SelectedWeaponIndex,Product!=nullptr,!Owned);
        const int32 PrimaryIndex=DemoWeaponCatalog::IndexOf(View.PrimaryWeaponId); // 只比较服务器已确认装配；空主槽不能伪造一把主枪。
        const bool EmptyPrimary=PrimaryIndex<1||PrimaryIndex>3; // 默认新档仅手枪，以有名副武器作为明确参照。
        const ADemoWeaponBase* Baseline=Equipment?Equipment->GetCatalogWeapon(EmptyPrimary?0:PrimaryIndex):nullptr; // 与Selected同为CDO基础值，公平口径。
        AddEquipmentComparison(Hero,Selected,Baseline,EmptyPrimary);
        UTextBlock* Section=AddText(Details,TEXT("基础武器参数"),14,DemoUI::Muted); // 固定单行标题，压缩垂直留白不改变信息。
        if(auto* SectionSlot=Cast<UVerticalBoxSlot>(Section->Slot)) SectionSlot->SetPadding(FMargin(0,0,0,6));
        AddEquipmentStat(Details,TEXT("伤害 / 每发"),Selected->GetPelletCount()>1?FString::Printf(TEXT("%.0f × %d"),Selected->Config.BaseDamage,Selected->GetPelletCount()):FString::Printf(TEXT("%.0f"),Selected->Config.BaseDamage));
        AddEquipmentStat(Details,TEXT("射速 / RPM"),FString::Printf(TEXT("%.0f"),Selected->Config.RoundsPerMinute));
        AddEquipmentStat(Details,TEXT("弹匣 / 发"),FString::FromInt(Selected->Config.MagazineCapacity));
        AddEquipmentStat(Details,TEXT("装填 / 秒"),FString::Printf(TEXT("%.1f"),Selected->Config.ReloadSeconds));
    }
    else { UE_LOG(LogFPSDemo,Log,TEXT("UIR_EQUIPMENT weapon CDO pending index=%d"),SelectedWeaponIndex); AddText(Hero,TEXT("等待武器定义"),22,DemoUI::Muted); }
    UTextBlock* Ownership=AddText(Details,Owned?TEXT("✓ 已解锁"):TEXT("尚未解锁"),14,Owned?DemoUI::Accent:DemoUI::Gold); // 解锁语义仍来自本人权威快照。
    if(auto* OwnershipSlot=Cast<UVerticalBoxSlot>(Ownership->Slot)) OwnershipSlot->SetPadding(FMargin(0,5,0,2));
    UTextBlock* Condition=AddText(Details,DemoWeaponCatalog::UnlockText(SelectedWeaponIndex),14,DemoUI::Muted); // 完整解锁条件不删减，长条件仍允许右栏独立滚动。
    if(auto* ConditionSlot=Cast<UVerticalBoxSlot>(Condition->Slot)) ConditionSlot->SetPadding(FMargin(0,2));
    if(!View.TerminalBlockReason.IsEmpty()) AddText(Hero,View.TerminalBlockReason,15,DemoUI::Gold); // 错误完整保留在中央正文，防止把主CTA推离初屏。
    AddGap(Details,8.f);
    const bool CanEquip=Owned&&SelectedWeaponIndex>0&&Selected&&View.TerminalBlockReason.IsEmpty(); // 只启用本人已解锁主武器；原服务仍重核终端/装配/保存许可。
    const FString EquipCaption=SelectedWeaponIndex==0?TEXT("固定副武器 [2]"):Owned?TEXT("装配为主武器"):TEXT("未购买 · 请到枪墙购买"); // 说明直接留在本页，不提供已删除的二级详情入口。
    UDemoMenuButton* Equip=AddAction(Details,TEXT("UI.WeaponEquip"),EquipCaption,CanEquip);
    Equip->SetVisualRole(EDemoButtonVisualRole::Primary); Equip->UseSingleLineLabel();
    if(auto* EquipSlot=Cast<UVerticalBoxSlot>(Equip->Slot)) EquipSlot->SetPadding(FMargin(0,3));
    AddText(Hero,TEXT("购买不会自动装配；点击装配直接提交，不消耗金币。"),14,DemoUI::Muted); // 明确操作仍由UIManager转原权威请求，不在控件中修改库存。
    if(GetGameInstance())
    {
        if(const UDemoPlayerProfile* Profile=GetGameInstance()->GetSubsystem<UDemoPlayerProfile>()) AddText(Hero,Profile->GetStatus(),14,DemoUI::Muted);
        if(const UDemoCloudSync* Cloud=GetGameInstance()->GetSubsystem<UDemoCloudSync>()) AddText(Hero,Cloud->GetStatus(),14,DemoUI::Muted);
    }
    AddCompactAction(PageFooter?static_cast<UPanelWidget*>(PageFooter):PageBody,TEXT("ProfileSave"),TEXT("保存重试")); // 仍走既有重试意图，作为次要操作而不抢装备CTA。
}

void UDemoTerminalScreen::BuildAmmo(const ADemoCharacter* Player)
{
    DEMO_LOG_CALL();
    if(UsesR02Presentation()) { BuildR02Ammo(Player); return; } // 实际Owner报价和购买/装配分离合同在R02实现中原样保留。
    const FDemoPlayerMenuSnapshot& View=Controller()->GetMenuSnapshot(); // 所有解锁、选中、价格和拒绝原因以此为准。
    const UDemoAmmoComponent* Ammo=Player?Player->FindComponentByClass<UDemoAmmoComponent>():nullptr; // 仅借用展示目录。
    const UDemoAmmoCatalog* Data=Ammo?Ammo->GetCatalog():nullptr; // 图标随目录资产已加载，无本页磁盘加载。
    SetHeading(TEXT("弹药配置"),TEXT("安全区配置  /  主副武器通用  /  同时装配一种"));
    if(!Data||Data->Entries.Num()!=4||View.AmmoOffers.Num()!=4)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("UMG_TERMINAL ammo catalog or public offers not ready"));
        AddText(PageBody,TEXT("等待弹药目录与服务器报价"),17,DemoUI::Muted); return;
    }
    UVerticalBox* Catalog=nullptr; // 和武器页使用完全相同的目录宽度/中央区/固定参数侧栏。
    UVerticalBox* Hero=BuildEquipmentColumns(Catalog); // 说明与图标归中央展示区，不推移共用页签。
    UVerticalBox* Details=ShowAside(); // 当前装配、解锁状态与主操作固定在右侧。
    const int32 Selected=Flow()->GetInspectedAmmo(); // 查看项和实际装配分开，非法索引不访问数组。
    for(int32 Index=0;Index<4;++Index)
    {
        const FDemoAmmoEntry& Entry=Data->Entries[Index]; // 既有弹药Icon与本地化名称。
        const FDemoShopOfferSnapshot& Offer=View.AmmoOffers[Index]; // 服务端逐项拥有者状态。
        UHorizontalBox* Row=AddRow(Catalog); // 图标、名字和解锁状态共用固定目录，不重排顶部按钮。
        AddIcon(Row,Entry.Icon,48.f);
        UVerticalBox* Description=AddColumn(Row); // 当前树拥有的目录名称/状态，不混入实际装配状态。
        if(auto* DescriptionSlot=Cast<UHorizontalBoxSlot>(Description->Slot)) DescriptionSlot->SetPadding(FMargin(8,0));
        UDemoMenuButton* EntryButton=AddAction(Description,FName(*FString::Printf(TEXT("Ammo%d"),Index)),Entry.Name.ToString(),true,Index==Selected);
        EntryButton->SetVisualRole(EDemoButtonVisualRole::Catalog); EntryButton->SetMinDimensions(140.f,42.f); EntryButton->UseSingleLineLabel();
        if(auto* EntrySlot=Cast<UVerticalBoxSlot>(EntryButton->Slot)) EntrySlot->SetPadding(FMargin(0));
        UTextBlock* Status=AddText(Description,Offer.bSelected?TEXT("已装配"):Offer.bUnlocked?TEXT("已解锁 · 可装配"):FString::Printf(TEXT("%d 金币解锁"),Offer.Cost),12,DemoUI::Muted); // 价格来自原报价，目录状态不与完整详情争夺垂直空间。
        Status->SetAutoWrapText(false); Status->SetWrapTextAt(0.f);
        if(auto* StatusSlot=Cast<UVerticalBoxSlot>(Status->Slot)) StatusSlot->SetPadding(FMargin(0));
        AddGap(Catalog,4.f);
    }
    AddGap(Catalog,8.f);
    AddText(Catalog,TEXT("主副武器共用 · 每人一种\n仅安全区可购买和更换。"),13,DemoUI::Muted);
    if(!Data->Entries.IsValidIndex(Selected))
    {
        UE_LOG(LogFPSDemo,Warning,TEXT("UMG_TERMINAL invalid inspected ammo index=%d"),Selected);
        AddText(Hero,TEXT("请选择有效弹药类型"),16,DemoUI::Gold); return;
    }
    const FDemoShopOfferSnapshot& Offer=View.AmmoOffers[Selected]; // 当前详情和操作使用同一权威项。
    AddText(Hero,FString::Printf(TEXT("AMMUNITION  /  %02d · 04"),Selected+1),15,DemoUI::Muted)->SetFont(DemoUI::DisplayFont(15,TEXT("Medium")));
    AddEquipmentArtwork(Hero,Data->Entries[Selected].Icon,INDEX_NONE,false);
    AddText(Hero,Data->Entries[Selected].Name.ToString(),34,DemoUI::Ink);
    switch(Selected)
    {
        case 0:
            AddText(Hero,TEXT("原始武器伤害，无附加状态。已默认解锁，原射速、弹匣和装填规则保持不变。"),18,DemoUI::Muted);
            break;
        case 1:
            AddText(Hero,FString::Printf(TEXT("命中叠加灼烧：每层每 %.1f 秒造成 %.1f HP，持续 %.1f 秒。"),Data->BurnPeriod,Data->BurnDamagePerStack,Data->BurnDuration),18,DemoUI::Muted);
            AddText(Hero,FString::Printf(TEXT("达到 %d 层触发单体爆炸，额外造成 %.1f 伤害并清空层数。"),Data->BurnThreshold,Data->ExplosionDamage),17,DemoUI::Muted);
            AddText(Hero,TEXT("同次开火对同一敌人只叠一层，散弹多颗命中也不重复叠层。"),16,DemoUI::Muted);
            break;
        case 2:
            AddText(Hero,FString::Printf(TEXT("每层减速 %.0f%%，上限 %.0f%%，持续 %.1f 秒。"),100*Data->SlowPerStack,100*Data->MaxSlow,Data->ChillDuration),18,DemoUI::Muted);
            AddText(Hero,FString::Printf(TEXT("达到 %d 层冻结 %.1f 秒，解冻后免疫冰冻 %.1f 秒。"),Data->FreezeThreshold,Data->FreezeDuration,Data->PostThawImmunityDuration),17,DemoUI::Muted);
            AddText(Hero,TEXT("冻结限制移动，不阻止攻击；免疫期间仍承受子弹直接伤害。"),16,DemoUI::Muted);
            break;
        case 3:
            AddText(Hero,FString::Printf(TEXT("首个敌人承受 %.0f%% 伤害；沿原轨迹额外击中一个后方敌人，承受首目标伤害的 %.0f%%。"),100*Data->PiercingDamageMultiplier,100*Data->SecondaryDamageRatio),18,DemoUI::Muted);
            AddText(Hero,TEXT("无法穿墙；散弹每颗弹丸独立检测穿透。"),16,DemoUI::Muted);
            break;
        default: break;
    }
    AddText(Details,TEXT("当前装配"),14,DemoUI::Muted);
    const FDemoShopOfferSnapshot* EquippedOffer=View.AmmoOffers.FindByPredicate([](const FDemoShopOfferSnapshot& Item){UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UIR_Ammo.FindEquippedOffer"));return Item.bSelected;}); // 同步无捕获扫描当前拥有者快照，静态日志不引用this，不保留引用到本次构建之外。
    const int32 EquippedIndex=EquippedOffer?int32(EquippedOffer-View.AmmoOffers.GetData()):INDEX_NONE; // 没有已确认选择时不把查看项误报成装配项。
    AddText(Details,Data->Entries.IsValidIndex(EquippedIndex)?Data->Entries[EquippedIndex].Name.ToString():TEXT("等待装配状态"),22,DemoUI::Ink);
    AddGap(Details,8.f);
    AddText(Details,TEXT("解锁状态"),14,DemoUI::Muted);
    AddText(Details,Offer.bUnlocked?TEXT("✓ 已解锁"):TEXT("尚未解锁"),18,Offer.bUnlocked?DemoUI::Accent:DemoUI::Gold);
    AddText(Hero,TEXT("永久解锁此存档中的弹药。解锁成功后，还需明确装配。"),16,DemoUI::Muted); // 解释移正文，右侧保留状态/价格与唯一主操作初屏可达。
    if(!Offer.BlockReason.IsEmpty()) AddText(Hero,Offer.BlockReason,15,DemoUI::Gold);
    AddGap(Details,12.f);
    if(!Offer.bUnlocked) AddEquipmentStat(Details,TEXT("解锁价格"),FString::Printf(TEXT("%d"),Offer.Cost));
    UDemoMenuButton* Primary=AddAction(Details,TEXT("AmmoAction"),Offer.bSelected?TEXT("已装配"):Offer.bUnlocked?TEXT("装配此弹药"):FString::Printf(TEXT("%d 金币解锁"),Offer.Cost),Offer.BlockReason.IsEmpty()&&!Offer.bSelected); // 查看永不消费，原AmmoAction仍负责确认/报价验证。
    Primary->SetVisualRole(EDemoButtonVisualRole::Primary); Primary->UseSingleLineLabel();
    if(auto* PrimarySlot=Cast<UVerticalBoxSlot>(Primary->Slot)) PrimarySlot->SetPadding(FMargin(0,3));
    AddText(Hero,TEXT("仅安全区可更换；购买解锁后可免费装配，购买本身不自动替换当前弹药。"),14,DemoUI::Muted);
}
