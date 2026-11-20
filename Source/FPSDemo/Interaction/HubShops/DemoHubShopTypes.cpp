// 职责：P0统一交互输入可信默认引用。使用：PC本地安装，原Character交互只保留缺资源回退；无经济或联网权限。
#include "Interaction/HubShops/DemoHubShopTypes.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Debug/DemoLog.h"
UDemoHubShopInputSettings::UDemoHubShopInputSettings()
{
    DEMO_LOG_CALL();
    InteractAction = TSoftObjectPtr<UInputAction>(FSoftObjectPath(TEXT("/Game/Data/HubShops/Input/IA_HubInteract.IA_HubInteract")));
    Mapping = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Data/HubShops/Input/IMC_HubInteract.IMC_HubInteract")));
    FallbackKey = EKeys::E;
}
