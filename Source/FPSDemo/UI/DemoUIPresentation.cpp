// 职责：R04本机动效/音效偏好的统一适配与限频播放。使用：菜单控件主动调用，不每帧加载或创建音频对象。
// 依赖：原GameUserSettings与LocalPlayer，五个自产SoundWave；仅本机表现，不改变玩法/经济/保存事务。
#include "UI/DemoUIPresentation.h"
#include "Settings/DemoGameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "HAL/PlatformTime.h"
#include "Debug/DemoLog.h"

bool DemoUI::ReducedMotion()
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoUI::ReducedMotion"));
    const auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings()); // 引擎保活的只读设备对象。
    return Settings&&Settings->IsUIMotionReduced();
}
bool DemoUI::UISoundEnabled()
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoUI::UISoundEnabled"));
    const auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings());
    return !Settings||Settings->IsUISoundEnabled();
}
void DemoUI::SetReducedMotion(bool Value)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoUI::SetReducedMotion value=%d"),Value);
    if(auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())) Settings->SetUIMotionReduced(Value);
    else UE_LOG(LogFPSDemo,Warning,TEXT("UI motion preference rejected missing GameUserSettings"));
}
void DemoUI::SetUISoundEnabled(bool Value)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoUI::SetUISoundEnabled value=%d"),Value);
    if(auto* Settings=Cast<UDemoGameUserSettings>(UGameUserSettings::GetGameUserSettings())) Settings->SetUISoundEnabled(Value);
    else UE_LOG(LogFPSDemo,Warning,TEXT("UI sound preference rejected missing GameUserSettings"));
}
void DemoUI::PlayFeedback(APlayerController* Owner,EDemoUISound Sound)
{
    UE_LOG(LogFPSDemo,VeryVerbose,TEXT("[CALL] DemoUI::PlayFeedback"));
    if(!Owner||!Owner->IsLocalController()||!Owner->GetLocalPlayer()) return;
    if(auto* Presentation=Owner->GetLocalPlayer()->GetSubsystem<UDemoUIPresentationSubsystem>()) Presentation->Play(Owner,Sound);
}
UDemoUIPresentationAssets::UDemoUIPresentationAssets()
{
    DEMO_LOG_CALL();
    const TCHAR* Paths[]={TEXT("/Game/UI/RedesignR04/Audio/S_UIHover.S_UIHover"),TEXT("/Game/UI/RedesignR04/Audio/S_UIPress.S_UIPress"),TEXT("/Game/UI/RedesignR04/Audio/S_UIBack.S_UIBack"),TEXT("/Game/UI/RedesignR04/Audio/S_UIReject.S_UIReject"),TEXT("/Game/UI/RedesignR04/Audio/S_UISuccess.S_UISuccess")}; // 固定小资源，无外部路径或在线下载。
    for(const TCHAR* Path:Paths)
    {
        ConstructorHelpers::FObjectFinderOptional<USoundBase> Sound(Path); // CDO初始化一次，导入前可为空；按钮不再LoadSynchronous。
        Sounds.Add(Sound.Get());
    }
}
USoundBase* UDemoUIPresentationAssets::GetSound(EDemoUISound Index) const
{ DEMO_LOG_TICK(); return Sounds.IsValidIndex(static_cast<int32>(Index))?Sounds[static_cast<int32>(Index)].Get():nullptr; }
void UDemoUIPresentationSubsystem::Play(APlayerController* Owner,EDemoUISound Sound)
{
    DEMO_LOG_CALL();
    if(!Owner||!Owner->IsLocalController()||Owner->GetLocalPlayer()!=GetLocalPlayer()||!DemoUI::UISoundEnabled()) return;
    const double Now=FPlatformTime::Seconds(); // 独立真实时钟，单机暂停不冻结菜单反馈限频。
    if(Sound==EDemoUISound::Hover) { if(Now-LastHoverTime<.08) return; LastHoverTime=Now; }
    else if(Sound==EDemoUISound::Success||Sound==EDemoUISound::Reject) { if(Now-LastResultTime<.06) return; LastResultTime=Now; }
    else if(Sound==EDemoUISound::Back) { if(Now-LastBackTime<.04) return; LastBackTime=Now; }
    else { if(Now-LastActionTime<.04) return; LastActionTime=Now; }
    USoundBase* Audio=GetDefault<UDemoUIPresentationAssets>()->GetSound(Sound); // CDO强持有，不持有旧World。
    if(Audio) UGameplayStatics::PlaySound2D(Owner,Audio,Sound==EDemoUISound::Hover?.22f:.40f,1.f,0.f,nullptr,Owner,true); // bIsUISound=true，菜单暂停可播放；不向队友复制。
}
