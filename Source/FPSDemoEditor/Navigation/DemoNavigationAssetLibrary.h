#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DemoNavigationAssetLibrary.generated.h"

/** Editor专用导航制作桥接；原生BrushBuilder与NavigationSystem创建资产，不伪造.umap二进制。 */
UCLASS()
class FPSDEMOEDITOR_API UDemoNavigationAssetLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** World为已载入白模EditorWorld；按三个DemoArea标签创建/更新体积并完成构建，失败不保存地图。 */
    UFUNCTION(BlueprintCallable,Category="Demo|Editor|Navigation")
    static bool ConfigureNavigation(UWorld* World);
};
