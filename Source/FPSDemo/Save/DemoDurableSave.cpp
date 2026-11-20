// 职责：为整槽经济提交提供本地原子发布点；复用UE SaveGame内存格式与默认Windows SaveGames路径，不绕过Editor临时命名。
#include "Save/DemoDurableSave.h"
#include "Debug/DemoStaticLog.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
#if WITH_EDITOR
    int32 FailureForTest=0; // 只在游戏线程纯合同测试同步使用，不持久化、不进入包体。
#endif
    /** Slot由GI生成，只允许文件名字符；返回UE GenericSaveGameSystem同一绝对路径。 */
    FString SavePath(const FString& Slot)
    {
        DEMO_LOG_STATIC_CALL(nullptr);
        if (Slot.IsEmpty() || Slot.Contains(TEXT("..")) || Slot.Contains(TEXT("/")) || Slot.Contains(TEXT("\\")) || Slot.Contains(TEXT(":"))) return FString();
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("SaveGames")/(Slot+TEXT(".sav")));
    }
}
#if WITH_EDITOR
void DemoDurableSave::SetFailureForTest(int32 Point)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    UE_LOG(LogFPSDemo, Log, TEXT("DURABLE_SAVE test point=%d"),Point);
    check(IsInGameThread()); FailureForTest=(Point>=0&&Point<=2)?Point:0;
}
#endif
bool DemoDurableSave::Archive(const FString& SourceSlot,const FString& ArchiveSlot)
{
    DEMO_LOG_STATIC_CALL(nullptr);
#if PLATFORM_WINDOWS
    const FString Source=SavePath(SourceSlot), Target=SavePath(ArchiveSlot); // 两端均同一SaveGames安全文件名。
    if (Source.IsEmpty()||Target.IsEmpty()) return false;
    IPlatformFile& Files=FPlatformFileManager::Get().GetPlatformFile();
    if (!Files.FileExists(*Source)) return true;
    if (Files.FileExists(*Target)) { TArray<uint8> Existing; return FFileHelper::LoadFileToArray(Existing,*Target)&&!Existing.IsEmpty(); } // 首份归档永不轮换。
    TArray<uint8> Bytes,Readback; // 原始磁盘字节，不重新序列化/Normalize旧存档。
    const FString Staging=Target+TEXT(".pending");
    if (!FFileHelper::LoadFileToArray(Bytes,*Source)||Bytes.IsEmpty()) return false;
    TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*Staging,false,false));
    if (!Handle||!Handle->Write(Bytes.GetData(),Bytes.Num())||!Handle->Flush(true)) return false;
    Handle.Reset();
    if (!FFileHelper::LoadFileToArray(Readback,*Staging)||Readback!=Bytes||!::MoveFileExW(*Staging,*Target,MOVEFILE_WRITE_THROUGH)) return false;
    return true;
#else
    return false;
#endif
}
bool DemoDurableSave::Write(USaveGame* Snapshot, const FString& Slot)
{
    DEMO_LOG_STATIC_CALL(nullptr);
#if PLATFORM_WINDOWS
    const FString Target=SavePath(Slot); // 只由项目槽名构建，不能指向玩家指定任意路径。
    TArray<uint8> Bytes; // 本次同步完整快照，不在原正式文件上增量写字段。
    if (Target.IsEmpty() || !Snapshot || !UGameplayStatics::SaveGameToMemory(Snapshot,Bytes)) return false;
    IPlatformFile& Files=FPlatformFileManager::Get().GetPlatformFile(); // 当前平台本地文件接口；不创建云子系统。
    if (!Files.CreateDirectoryTree(*FPaths::GetPath(Target))) return false;
    const FString Staging=Target+TEXT(".pending"); // 同目录同卷暂存；崩溃后不自动采纳未提交文件。
    TUniquePtr<IFileHandle> Handle(Files.OpenWrite(*Staging,false,false)); // RAII关闭；先Flush(true)，失败时旧主档仍原样。
    if (!Handle || !Handle->Write(Bytes.GetData(),Bytes.Num()))
    { UE_LOG(LogFPSDemo, Warning, TEXT("DURABLE_SAVE staging write failed")); return false; }
#if WITH_EDITOR
    if (FailureForTest==1) { UE_LOG(LogFPSDemo, Log, TEXT("DURABLE_SAVE injected before flush")); return false; }
#endif
    if (!Handle->Flush(true))
    { UE_LOG(LogFPSDemo, Warning, TEXT("DURABLE_SAVE staging write/flush failed")); return false; }
    Handle.Reset();
    TArray<uint8> Readback; // 原子提交前验证暂存确为本次完整字节，不在rename之后再返回可回滚失败。
    if (!FFileHelper::LoadFileToArray(Readback,*Staging) || Readback!=Bytes)
    { UE_LOG(LogFPSDemo, Warning, TEXT("DURABLE_SAVE staging readback failed")); return false; }
#if WITH_EDITOR
    if (FailureForTest==2) { UE_LOG(LogFPSDemo, Log, TEXT("DURABLE_SAVE injected before replace")); return false; }
#endif
    if (!::MoveFileExW(*Staging,*Target,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    { UE_LOG(LogFPSDemo, Warning, TEXT("DURABLE_SAVE replace failed code=%u"), static_cast<uint32>(::GetLastError())); return false; }
    return true; // 唯一经济发布点；云Outbox/HTTP完全独立，离线不阻塞本地成功。
#else
    UE_LOG(LogFPSDemo, Warning, TEXT("DURABLE_SAVE unsupported local atomic backend")); return false;
#endif
}
void DemoDurableSave::RemoveStaging(const FString& Slot)
{
    DEMO_LOG_STATIC_CALL(nullptr);
    const FString Target=SavePath(Slot); // 与Write完全相同命名，不递归、不通配符删除。
    if (!Target.IsEmpty()) FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*(Target+TEXT(".pending")));
}
