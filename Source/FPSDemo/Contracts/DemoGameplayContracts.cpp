#include "Contracts/DemoGameplayContracts.h"
#include "Debug/DemoLog.h"

bool FDemoCommandResult::IsApplied() const
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] %hs"), __FUNCTION__);
    return Status != EDemoCommandStatus::Rejected;
}
FDemoCommandResult FDemoCommandResult::Reject(FName Code, const FString& Message)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs reason=%s"), __FUNCTION__, *Code.ToString());
    FDemoCommandResult Result; // 返回值独立于调用方临时文本生命周期。
    Result.Code = Code; Result.Message = Message;
    return Result;
}
FDemoCommandResult FDemoCommandResult::Applied(bool bSaved, const FString& Message)
{
    UE_LOG(LogFPSDemo, Log, TEXT("[CALL] %hs saved=%d"), __FUNCTION__, bSaved);
    FDemoCommandResult Result; // 两阶段结果使已应用但保存失败的交易可见。
    Result.Status = bSaved ? EDemoCommandStatus::Committed : EDemoCommandStatus::AppliedPendingSave;
    Result.Code = bSaved ? TEXT("Committed") : TEXT("PendingSave"); Result.Message = Message;
    return Result;
}
