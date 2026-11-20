#include "UI/Flow/DemoExitSaveFlow.h"
#include "Debug/DemoLog.h"

void UDemoExitSaveFlow::Begin(bool bRetry)
{
    DEMO_LOG_CALL(); State=EDemoQuitState::SavingLocal; bLocalSaved=false; NextActionTime=FPlatformTime::Seconds()+.15;
    Message=bRetry?TEXT("正在重新保存本地进度……"):TEXT("正在保存本地检查点与永久进度……");
}
void UDemoExitSaveFlow::CompleteLocal(bool bSuccess,const FString& Detail)
{
    DEMO_LOG_CALL();
    if(State==EDemoQuitState::Idle){UE_LOG(LogFPSDemo,Log,TEXT("EXIT_LOCAL late result ignored"));return;}
    bLocalSaved=bSuccess;
    if(!bSuccess){State=EDemoQuitState::Failed;Message=Detail;}
}
void UDemoExitSaveFlow::WaitForCloud()
{
    DEMO_LOG_CALL();
    if(!bLocalSaved||State==EDemoQuitState::Idle){UE_LOG(LogFPSDemo,Log,TEXT("EXIT_CLOUD rejected no local save/intent"));return;}
    State=EDemoQuitState::WaitingCloud;
}
void UDemoExitSaveFlow::ObserveCloud(EDemoCloudExitState Result,const FString& Detail)
{
    DEMO_LOG_TICK();
    if(State!=EDemoQuitState::WaitingCloud)return;
    Message=Detail;
    if(Result==EDemoCloudExitState::Complete||Result==EDemoCloudExitState::LocalOnly)
        MarkSaved(Result==EDemoCloudExitState::Complete?TEXT("本地与云端均已保存，即将退出"):TEXT("本地保存完成（云同步已关闭），即将退出"));
    else if(Result==EDemoCloudExitState::Failed||Result==EDemoCloudExitState::Idle){State=EDemoQuitState::Failed;UE_LOG(LogFPSDemo,Log,TEXT("EXIT_CLOUD failed"));}
}
void UDemoExitSaveFlow::MarkSaved(const FString& Detail)
{
    DEMO_LOG_CALL();
    if(!bLocalSaved||State==EDemoQuitState::Idle){UE_LOG(LogFPSDemo,Log,TEXT("EXIT_SAVED rejected no local save/intent"));return;}
    State=EDemoQuitState::Saved; Message=Detail; NextActionTime=FPlatformTime::Seconds()+1.0;
}
void UDemoExitSaveFlow::Cancel(){DEMO_LOG_CALL();State=EDemoQuitState::Idle;bLocalSaved=false;NextActionTime=0;Message.Empty();}
EDemoQuitState UDemoExitSaveFlow::GetState() const {DEMO_LOG_TICK();return State;}
bool UDemoExitSaveFlow::IsLocalSaved() const {DEMO_LOG_TICK();return bLocalSaved;}
bool UDemoExitSaveFlow::IsLocalDue() const {DEMO_LOG_TICK();return State==EDemoQuitState::SavingLocal&&FPlatformTime::Seconds()>=NextActionTime;}
bool UDemoExitSaveFlow::CanFinish() const {DEMO_LOG_TICK();return State==EDemoQuitState::Saved&&FPlatformTime::Seconds()>=NextActionTime;}
const FString& UDemoExitSaveFlow::GetMessage() const {DEMO_LOG_TICK();return Message;}
