// 职责：终端、奖励、冒险抽取与出发准备的UMG页面。使用：CommonUI协调器激活本页，按钮只提交原MenuFlow白名单意图。
// 依赖：拥有者PC公开快照、GAS只读属性及武器/弹药目录；本页没有扣币、随机、存档或服务器修改权限。
#pragma once

#include "CoreMinimal.h"
#include "UI/Common/DemoMenuScreen.h"
#include "DemoTerminalScreen.generated.h"

class ADemoGameState;
class ADemoCharacter;
class ADemoWeaponBase;
class UDemoWeaponComponent;
class UCanvasPanel;
class UBorder;
class UTextBlock;
struct FStreamableHandle;

/** 原生默认终端页面；可派生Widget蓝图替换PageBody样式，业务仍经基类受控SubmitAction。 */
UCLASS(Blueprintable)
class FPSDEMO_API UDemoTerminalScreen : public UDemoMenuScreen
{
    GENERATED_BODY()
public:
    /** 只配置R04产品展示软路径与UV留白；不加载资源、不改变武器Config或装备。 */
    UDemoTerminalScreen(const FObjectInitializer& ObjectInitializer);
    /** UIManager在已验证Weapon0..3意图转发前记录目录高亮；Index只能0..3，不提交交易。 */
    void NoteWeaponInspection(int32 Index);
    /** 返回本页纯展示选择（未选时INDEX_NONE）；只供刷新键与右侧查看CTA使用。 */
    int32 GetPresentationWeaponIndex() const;
    /** UIManager在核过当前核心页/按钮后传入目录Index；只记录本地预览，绝不调用SelectCore或保存。 */
    void NoteCoreInspection(int32 Index);
    /** 返回当前本地核心预览索引，未初始化为INDEX_NONE；只读审查/刷新使用，不代表服务器已选。 */
    int32 GetPresentationCoreIndex() const;
protected:
    /** 按已授权页型启用R02；武器/弹药、奖励与核心分别沿原Flow，不改变业务真值。 */
    virtual bool UsesR02Presentation() const override;
    /** 每帧只比较展示模型；排除滚动时间/位移，数据未变时不重建控件树。 */
    virtual FString RefreshKey() const override;
    /** 模型改变时构建当前互斥子页；确认、武器详情与下注模态由协调器单独管理。 */
    virtual void BuildBody() override;
    /** DeltaTime为本地帧秒；仅移动已创建的七张卡片，中奖及终点始终读取已保存回执。 */
    virtual void AnimateScreen(float DeltaTime) override;
    /** 旅行/关页释放异步图标请求并清本地核心预览；UObject弱回调不得更新已拆卸页面。 */
    virtual void NativeDestruct() override;
private:
    /** State为当前公开阶段；互斥页签只在普通终端中展示，奖励/滚动页不提供越权入口。 */
    void BuildTabs(const ADemoGameState* State);
    /** State决定币种文案；Player只读取GAS总结。三列卡展示权威值/报价及个人阵亡与整轮结束的寿命区别，不计算交易权限。 */
    void BuildStats(const ADemoGameState* State,const ADemoCharacter* Player);
    /** 免费奖励与核心分支共用权威RewardOffers；说明个人阵亡复活保留、整轮结束清除，无数据时不创建虚假可领取项。 */
    void BuildRewards();
    /** 展示七核心战斗触发链、完整权威说明与当前选择；只改表现，不直接修改Build组件或数值。 */
    void BuildCores();
    /** R02七核心只读目录预览与当前一项完整OwnerOffer；右侧原CoreN按钮才提交明确选择。 */
    void BuildR02Cores();
    /** Parent为本次WidgetTree容器；Code为展示编号；Selected只决定细线/底色，权限仍来自Offer。 */
    UVerticalBox* AddProgressionCard(UPanelWidget* Parent,const FString& Code,bool Selected=false);
    /** Parent由当前树拥有；用1px分隔建立信息层级，不产生可点击区域或虚构属性强度条。 */
    void AddProgressionRule(UPanelWidget* Parent);
    /** Player只借用武器目录；锁定项可预览，已拥有主枪在当前武器库明确装配，不再打开详情模态。 */
    void BuildWeapons(const ADemoCharacter* Player);
    /** Player只借用弹药目录及图标；装配/解锁按快照报价，购买由统一确认模态完成。 */
    void BuildAmmo(const ADemoCharacter* Player);
    /** R02武器无框目录/真产品图/基础比较；Player只借目录，明确装配在本页提交原权威请求，无二级详情。 */
    void BuildR02Weapons(const ADemoCharacter* Player);
    /** S1四弹目录/真实报价/当前装配；Player只借用目录，购买不自动装配，回执继续由原Flow展示。 */
    void BuildR02Ammo(const ADemoCharacter* Player);
    /** 创建S1左目录与中间产品列；OutCatalog仅借用本次树，右栏沿原独立滚动区避免主操作随正文滚走。 */
    UVerticalBox* BuildR02EquipmentColumns(UVerticalBox*& OutCatalog);
    /** Parent拥有本次文本；Size为DPI前字号，Serif/Muted/Latin仅选字体和非关键信息颜色，不代表权限。 */
    UTextBlock* AddR02EquipmentLabel(UPanelWidget* Parent,const FString& Text,int32 Size=18,bool Serif=false,bool Muted=false,bool Latin=false);
    /** Parent拥有原生Slate托管节点；按实际累计布局缩放/绝对起点绘1物理像素线，无贴图背景/输入。 */
    void AddR02EquipmentRule(UPanelWidget* Parent);
    /** Parent拥有原生Slate竖向1物理像素分隔线，用于C04三栏视觉分界。 */
    void AddR02EquipmentVerticalRule(UPanelWidget* Parent);
    /** Parent拥有C04风格发丝数值条：左标签、右衬线数字，下附按真实最大标尺计算的1px骨白占比线。 */
    void AddR02EquipmentMeter(UPanelWidget* Parent,const FString& Title,const FString& Value,float Fraction);
    /** Parent拥有参数行；Title保留单位，Value只读实际CDO或Owner报价，衬线数值不表达评分。 */
    void AddR02EquipmentStat(UPanelWidget* Parent,const FString& Title,const FString& Value);
    /** Selected/Baseline均为只读CDO；EmptyPrimary说明使用有名固定手枪基准，不推算DPS或GAS后值。 */
    void AddR02EquipmentComparison(UPanelWidget* Parent,const ADemoWeaponBase* Selected,const ADemoWeaponBase* Baseline,bool EmptyPrimary);
    /** State决定金币七玩法/遗物或银币四抽取方向；详细七玩法由独立页面负责。 */
    void BuildGamble(const ADemoGameState* State);
    /** 创建固定七卡的裁剪奖励带；使用真实UMG控件，不为循环奖池逐圈创建Widget。 */
    void BuildGambleReel();
    /** R02金币娱乐/遗物（Hub）与银币冒险强化（关间）；State只决定币种与布局，按钮沿原Casino.Open/GambleN/GambleAction白名单。 */
    void BuildR02Gamble(const ADemoGameState* State);
    /** R02奖励带：同一七槽位移/回执，只换主题色与衬线字体，不重抽或改概率。 */
    void BuildR02GambleReel();
    /** Parent拥有本次文本；Serif选衬线，Muted仅辅助信息色，WrapWidth>0时按已知列宽首帧换行。 */
    UTextBlock* AddR02GambleLabel(UPanelWidget* Parent,const FString& Text,int32 Size=18,bool Serif=false,bool Muted=false,float WrapWidth=0.f);
    /** Parent为出发页左栏；Hub战役出发前提供七核心预览与原CoreN明确选择，只读Owner快照，不改出发/难度门禁。 */
    void BuildDepartureCorePicker(UPanelWidget* Parent,float Width);
    /** CoreId为可信稳定ID，返回一行操作链教学文案（如"枪击短冻 → 近战冰裂"）；未知ID返回空，不推测机制。 */
    static FString CoreTriggerLine(FName CoreId);
    /** State为公开关卡/队伍/资源状态；区分资源等待与就绪未入场，并显示原重试/取消及房主本机检查点缓存。单人Hub仍点原难度出发，无保存或状态修改权限。 */
    void BuildDeparture(const ADemoGameState* State);
    /** Equipment仅本次借用；软图标首次访问异步加载，不在逐帧刷新同步读磁盘。 */
    void PrepareWeaponIcons(const UDemoWeaponComponent* Equipment);
    /** 游戏线程UObject弱绑定回调；请求取消或页面销毁后引擎不会访问悬空页面。 */
    void OnWeaponIconsLoaded();
    /** 创建左260目录与中间产品列，右侧参数仍由固定ShowAside承载；OutCatalog仅当前树借用。 */
    UVerticalBox* BuildEquipmentColumns(UVerticalBox*& OutCatalog);
    /** Parent由当前WidgetTree拥有；Value为已格式化的只读参数，不按显示条推断伤害。 */
    void AddEquipmentStat(UPanelWidget* Parent,const FString& Title,const FString& Value);
    /** Parent为本树的中央列；Selected/Baseline均只读目录CDO，EmptyPrimary说明基准是固定手枪；不混入实例/GAS或推算DPS。 */
    void AddEquipmentComparison(UPanelWidget* Parent,const ADemoWeaponBase* Selected,const ADemoWeaponBase* Baseline,bool EmptyPrimary);
    /** 用已加载Texture创建等比大图；ProductIndex为纯展示UV配置索引，bProduct=false时不裁图。 */
    void AddEquipmentArtwork(UPanelWidget* Parent,UTexture2D* Texture,int32 ProductIndex,bool bProduct,bool bDimmed=false);

    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Equipment") TArray<TSoftObjectPtr<UTexture2D>> WeaponPresentationImages; // 独立产品渲染软引用，只用于大图；目录图标始终读取武器Config.Icon，随页面异步请求。
    UPROPERTY(EditDefaultsOnly,Category="Demo|UI|Equipment") TArray<FVector4> WeaponPresentationUVs; // 对应产品图的0..1 UV(minX,minY,maxX,maxY)，默认只去除已核对长枪的透明留白，不改源纹理。
    int32 SelectedWeaponIndex=INDEX_NONE; // 页面纯展示选择0..3，不持久化/不复制；初始显示已装备主枪或固定手枪。
    int32 SelectedCoreIndex=INDEX_NONE; // 当前树核心预览索引0..CoreCount-1，纯本机、不复制/存档；关页清理，初始显示Owner已选项。

    TArray<FSoftObjectPath> WeaponIconPaths; // 前4项为Config.Icon，后4项为纯展示产品图；只在路径变化时重新请求。
    TSharedPtr<FStreamableHandle> WeaponIconLoadHandle; // 页面独占请求，销毁时显式取消。
    UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> WeaponIcons; // 对应8个路径的已加载纹理强引用；不生成资产，NativeDestruct整体清理。
    uint32 WeaponIconRevision=0; // 异步完成时增长，使静态模型键下一帧刷新一次。
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> ReelCanvas; // 奖励带的局部裁剪容器，仅负责卡片位置。
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> ReelCards; // 当前七张可见卡片，PageBody重建时重新绑定。
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ReelTitles; // 对应卡片标题，跨越整卡时才替换文本。
    TArray<int32> ReelCardIndices; // 当前槽映射的池索引，避免每帧重新设置相同文本。
    // 奖励带当前页型颜色（线性RGBA），构建时按R02/R04写入，AnimateScreen只读；纯本机展示，不复制/保存。
    FLinearColor ReelIdleColor=FLinearColor::Transparent;
    FLinearColor ReelSelectedColor=FLinearColor::White;
    FLinearColor ReelTitleColor=FLinearColor::White;
    FLinearColor ReelMutedColor=FLinearColor::Gray;
};
