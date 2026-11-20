// 职责：Editor显式启动的R02 S0真实UMG样式板与有界截图夹具；不集成B01～B07正式页面或业务。
// 使用：独立Editor -game -DemoR02StyleBoard -DemoR02Output=绝对目录；需要先运行DemoCreateR02S0Assets。
// 依赖：R02 Theme/EditorialButton、Slate真实字体绘制、正常FScreenshotRequest；非Editor不创建或识别启动入口。
#pragma once
#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Subsystems/WorldSubsystem.h"
#include "DemoR02StyleBoard.generated.h"

class UDemoUITheme;
class UDemoR02EditorialButton;
class UCanvasPanel;
class UBorder;

/** 真实UMG板，静态文字/细线由Slate绘制，交互样本是实际CommonUI按钮；所有数值明确为样式示例。 */
UCLASS(Transient, NotBlueprintable)
class FPSDEMO_API UDemoR02StyleBoardWidget : public UCommonActivatableWidget
{
    GENERATED_BODY()
public:
    /** 初始化只读Theme；加载缺项视为夹具失败，不将Sans回退截图冒充衬线通过。 */
    bool InitializeBoard(UDemoUITheme* InTheme);
    /** 返回当前实际CommonUI焦点样本；由夹具分帧请求焦点，不触发任何游戏动作。 */
    UDemoR02EditorialButton* GetFocusSample() const;
    /** 返回已绘制的几何与字体/按钮事实，用于S0有限证据，不代表完整输入/游戏验收。 */
    bool ValidateRendered(FString& Reason) const;
protected:
    /** 样式板作为独立激活根拥有本机菜单输入，按钮沿Outer WidgetTree能找到此父级；不使用生产UIManager栈。 */
    virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
    /** 初次构造唯一实例树，按用户要求仅纯色底板；无场景图/角色图/全屏颗粒，按钮沿CommonUI原生交互。 */
    virtual TSharedRef<SWidget> RebuildWidget() override;
    /** 根据真实布局重新放置样本；不保存设备设置、不在逐帧同步加载资源。 */
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    /** Slate真实字形/固定数字格/物理像素线样板，不绘制任何来自概念图的文字贴图。 */
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
private:
    UPROPERTY(Transient) TObjectPtr<UDemoUITheme> Theme; // 板实例强持有候选主题；不复制/存档，不写CDO。
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> BoardCanvas; // 当前UMG实例树的根；绝对槽仅用于可缩放样式板。
    UPROPERTY(Transient) TObjectPtr<UBorder> BackgroundPlate; // 用户明确不要背景图：仅Theme.Base纯色，无纹理/材质，不可命中。
    UPROPERTY(Transient) TArray<TObjectPtr<UDemoR02EditorialButton>> Buttons; // 4个真实按钮，含已选+焦点和禁用样本。
    FVector2D LastLocalSize=FVector2D::ZeroVector; // 只在实际分配尺寸变化时重新设置槽，单位UMG局部坐标。
};

/** 仅显式Editor独立-game创建；普通PIE/包体没有入口，截图完成自动退出自己的进程。 */
UCLASS()
class FPSDEMO_API UDemoR02StyleBoardCapture : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    /** 仅Standalone Game且Editor显式参数，不在正式游玩/命令let/服务器启动。 */
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    /** 排除Editor/PIE/Preview World，避免影响用户正在编辑的界面。 */
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
    /** 安装本World弱Tick，等待真实LocalPlayer/Viewport和渲染稳定，不改生产UI配置。 */
    virtual void OnWorldBeginPlay(UWorld& World) override;
    /** 精确移除本夹具委托与自己的Widget，不取消其他进程截图或退出用户Editor。 */
    virtual void Deinitialize() override;
private:
#if WITH_EDITOR
    /** 有界状态机：创建激活板→实际焦点→等待资源/真实绘制稳定→检查几何→正常PNG→落盘确认与JSON。 */
    void OnWorldTick(UWorld* World,ELevelTick TickType,float DeltaSeconds);
    /** 写本次有限结果并退出显式隔离进程；Reason说明失败/未覆盖范围。 */
    void Finish(bool Success,const FString& Reason);
    FDelegateHandle TickHandle; // 当前World游戏线程委托；结束/卸载移除，不持悬空Lambda。
    FString OutputDirectory,PNGPath; // 命令行明确给出的证据目录，不枚举或写玩家数据。
    double StartedAt=0.,Deadline=0.; // 单调真实秒，最多90秒；暂停或低帧也不会无界等待。
    int32 Step=0; // 0创建、1焦点、2检查并截图、3检查落盘；只影响本次夹具。
    bool bFinished=false; // 完成后禁止二次写结果/退出。
#endif
    UPROPERTY(Transient) TObjectPtr<UDemoR02StyleBoardWidget> Board; // 本夹具创建的唯一板；结束移出Viewport并释放。
};
