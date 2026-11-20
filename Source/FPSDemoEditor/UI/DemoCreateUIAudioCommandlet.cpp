// 职责：用UE原生SoundFactory创建真实可Cook UI声音，固定5资源，已有资产不覆盖。
// 使用：先执行Tools/Audio/build_ui_feedback.py，再运行本命令；无网络/玩家档/World。
#include "UI/DemoCreateUIAudioCommandlet.h"
#include "Factories/SoundFactory.h"
#include "Sound/SoundWave.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "Debug/DemoLog.h"

UDemoCreateUIAudioCommandlet::UDemoCreateUIAudioCommandlet()
{ DEMO_LOG_CALL(); IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true; }
int32 UDemoCreateUIAudioCommandlet::Main(const FString& Params)
{
    DEMO_LOG_CALL();
    for(const TCHAR* Name:{TEXT("S_UIHover"),TEXT("S_UIPress"),TEXT("S_UIBack"),TEXT("S_UIReject"),TEXT("S_UISuccess")})
    {
        const FString PackageName=FString(TEXT("/Game/UI/RedesignR04/Audio/"))+Name; // 唯一可写输出目录，禁止覆盖旧UI资源。
        if(auto* Existing=LoadObject<USoundWave>(nullptr,*(PackageName+TEXT(".")+Name),nullptr,LOAD_NoWarn|LOAD_Quiet))
        {
            if(Existing->NumChannels!=1||Existing->Duration<=0.f||Existing->Duration>.2f||Existing->SoundGroup!=SOUNDGROUP_UI||Existing->bLooping)
            { UE_LOG(LogFPSDemo,Error,TEXT("UI_AUDIO existing asset invalid %s"),Name); return 1; }
            UE_LOG(LogFPSDemo,Log,TEXT("UI_AUDIO verified %s duration=%.3f"),Name,Existing->Duration); continue;
        }
        const FString Source=FPaths::ProjectDir()/TEXT("SourceAssets/UI/RedesignR04/Audio")/(FString(Name)+TEXT(".wav")); // 固定自产PCM源，不读用户下载目录。
        TArray<uint8> Bytes; // 同步工厂消费期间保活WAV原字节。
        if(!FFileHelper::LoadFileToArray(Bytes,*Source)||Bytes.Num()<44) { UE_LOG(LogFPSDemo,Error,TEXT("UI_AUDIO source missing %s"),Name); return 1; }
        UPackage* Package=CreatePackage(*PackageName); // 只创建新命名空间包。
        USoundFactory* Factory=NewObject<USoundFactory>(); Factory->bAutoCreateCue=false; Factory->SuppressImportDialogs();
        const uint8* Begin=Bytes.GetData(); // Factory可推进指针，不越过本数组。
        USoundWave* Wave=Cast<USoundWave>(Factory->FactoryCreateBinary(USoundWave::StaticClass(),Package,FName(Name),RF_Public|RF_Standalone,nullptr,TEXT("wav"),Begin,Begin+Bytes.Num(),GWarn));
        if(!Wave) { UE_LOG(LogFPSDemo,Error,TEXT("UI_AUDIO import failed %s"),Name); return 1; }
        Wave->SoundGroup=SOUNDGROUP_UI; Wave->bLooping=false; Wave->OverrideLoadingBehavior(ESoundWaveLoadingBehavior::ForceInline); // 短音内联，无后台流送/高频IO。
        Wave->PostEditChange(); FAssetRegistryModule::AssetCreated(Wave);
        const FString Destination=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination),true);
        FSavePackageArgs Save; Save.TopLevelFlags=RF_Public|RF_Standalone; Save.SaveFlags=SAVE_NoError; // 引擎序列化，不手写uasset。
        if(!UPackage::SavePackage(Package,Wave,*Destination,Save)) { UE_LOG(LogFPSDemo,Error,TEXT("UI_AUDIO save failed %s"),Name); return 1; }
        UE_LOG(LogFPSDemo,Log,TEXT("UI_AUDIO created %s duration=%.3f"),Name,Wave->Duration);
    }
    UE_LOG(LogFPSDemo,Display,TEXT("UI_AUDIO_ASSETS_PASS count=5; asset structure only, no listening acceptance")); return 0;
}
