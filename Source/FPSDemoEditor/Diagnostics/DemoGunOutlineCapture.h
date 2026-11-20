// 职责：纯Editor枪墙轮廓专项的真实ViewOwner捕获桥；仅FPSDemoEditor模块使用，不进入游戏包体。
// 使用：临时生成ADemoGunOutlineCapture并SetOwner到ViewerPawn，复用SceneCapture2D正常RHI/深度与OnlyOwnerSee；不修改共享枪、相机或资产。
// 依赖UE5.4.4 USceneCaptureComponent2D::GetViewOwner虚接口；普通SceneCapture2D默认返回nullptr，单设Actor Owner无效。
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "DemoGunOutlineCapture.generated.h"

class UMaterialInterface;

/** Editor临时捕获组件；渲染器实际调用的GetViewOwner与脚本读回走同一虚接口。 */
UCLASS(Transient)
class FPSDEMOEDITOR_API UDemoGunOutlineCaptureComponent2D : public USceneCaptureComponent2D
{
    GENERATED_BODY()
public:
    /** 默认子对象创建时初始化，ObjectInitializer为引擎构造上下文；不生成游戏World或启动捕获。 */
    UDemoGunOutlineCaptureComponent2D(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    /** 每次渲染场景视图取Capture Actor的Owner作为真实ViewActor；未配置返回空，不关闭OnlyOwnerSee兜底。 */
    virtual const AActor* GetViewOwner() const override;

    /** 每次真正提交RHI捕获前补回本专项唯一Blendable，再交原生捕获；不修改其它PP字段或游戏相机。 */
    virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;

    /** Actor脚本入口同步设置/撤销本专项PP；空值移除旧项，保留其他捕获设置与Blendables。 */
    void SetAuditBlendable(UMaterialInterface* Material);

    /** 只读当前专项PP引用；提交前仍由UpdateSceneCaptureContents实际注入，不能以此单独证明像素效果。 */
    UMaterialInterface* GetAuditBlendable() const;
private:
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> AuditBlendable; // 本瞬态捕获拥有的强引用，renderer整体赋PP后仍可重注入；不保存/复制。
};

/** 临时Editor SceneCapture2D；必须可被EditorActorSubsystem生成，脚本以transient=True创建并finally销毁。 */
UCLASS(Transient)
class FPSDEMOEDITOR_API ADemoGunOutlineCapture : public ASceneCapture2D
{
    GENERATED_BODY()
public:
    /** 按UE5.4.4真实默认子对象名替换Capture组件类；不修改引擎、全局渲染或游戏Capture。 */
    ADemoGunOutlineCapture(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    /** 脚本在捕获前读回实际GetViewOwner虚调用结果；返回非拥有引用，不能以Actor.GetOwner代替该证据。 */
    UFUNCTION(BlueprintPure, Category = "Demo|Editor|Gun Outline")
    AActor* GetOutlineViewOwner() const;
    /** 仅此Editor捕获设置独有PP材质；空值为前图，真正注入发生在RHI捕获入口，不关闭OnlyOwnerSee。 */
    UFUNCTION(BlueprintCallable, Category = "Demo|Editor|Gun Outline")
    void SetOutlinePostProcess(UMaterialInterface* Material);
    /** 读回此捕获专属PP材质引用；实际材质图/渲染结果须专项分别验收。 */
    UFUNCTION(BlueprintPure, Category = "Demo|Editor|Gun Outline")
    UMaterialInterface* GetOutlinePostProcess() const;
    /** 仅纯Editor World按Viewer所在World直接生成RF_Transient捕获，避开命令行无放置工厂的Editor UI流程。
     * Location/Rotation为本次世界cm捕获位姿，Owner直接为Viewer；失败返回空，不保存或启动Game。
     */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Gun Outline")
    static ADemoGunOutlineCapture* SpawnForViewer(APawn* Viewer,FVector Location,FRotator Rotation);
};
