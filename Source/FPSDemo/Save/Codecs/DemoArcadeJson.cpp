// 职责：持续娱乐局面的严格JSON编解码。使用：由检查点Codec同步调用，数值校验先于UE反射转换。
#include "Save/Codecs/DemoArcadeJson.h"
#include "JsonObjectConverter.h"
#include "Debug/DemoLog.h"
TSharedRef<FJsonObject> DemoArcadeJson::Write(const FDemoArcadeRound& Round)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    auto Json=MakeShared<FJsonObject>(); // 单次输出所有权，不持有保存对象。
    FJsonObjectConverter::UStructToJsonObject(FDemoArcadeRound::StaticStruct(),&Round,Json,0,0);
    return Json;
}
bool DemoArcadeJson::Read(const FJsonObject& Json,FDemoArcadeRound& Out)
{
    UE_LOG(LogFPSDemo,Log,TEXT("[CALL] %hs"),__FUNCTION__);
    const TSet<FString> Scalars={TEXT("mode"),TEXT("stake"),TEXT("debited"),TEXT("step"),TEXT("deadlineMs"),TEXT("seed"),TEXT("shots"),TEXT("heads")}; // 固定协议键，避免反射吞掉未知字段。
    if(Json.Values.Num()!=11) return false;
    for(const auto& Pair:Json.Values)
    {
        if(Scalars.Contains(Pair.Key))
        {
            double Value=0; // JSON全部整数先在double精确范围内检查，再交给反射。
            const bool Deadline=Pair.Key==TEXT("deadlineMs"); // 时间戳上界小于2^53，精确转换。
            if(!Pair.Value.IsValid()||Pair.Value->Type!=EJson::Number||!Pair.Value->TryGetNumber(Value)||!FMath::IsFinite(Value)||Value!=FMath::FloorToDouble(Value)||Value<(Deadline?0.:double(MIN_int32))||Value>(Deadline?253402300799999.:double(MAX_int32))) return false;
        }
        else
        {
            const bool Weights=Pair.Key==TEXT("chances"); // 仅概率允许浮点，值域由领域校验。
            if(!Weights&&Pair.Key!=TEXT("values")&&Pair.Key!=TEXT("payouts")) return false;
            const TArray<TSharedPtr<FJsonValue>>* Values=nullptr; // 同栈借用。
            if(!Pair.Value->TryGetArray(Values)||Values->Num()>10) return false;
            for(const auto& Item:*Values)
            {
                double Value=0; // 禁止字符串数字、NaN和整数截断。
                if(!Item||Item->Type!=EJson::Number||!Item->TryGetNumber(Value)||!FMath::IsFinite(Value)||Value<0||(!Weights&&(Value>MAX_int32||Value!=FMath::FloorToDouble(Value)))) return false;
            }
        }
    }
    FDemoArcadeRound Candidate; // 整体成功才公开。
    if(!FJsonObjectConverter::JsonObjectToUStruct(MakeShared<FJsonObject>(Json),FDemoArcadeRound::StaticStruct(),&Candidate,0,0)||!Candidate.Validate()) return false;
    Out=MoveTemp(Candidate); return true;
}
