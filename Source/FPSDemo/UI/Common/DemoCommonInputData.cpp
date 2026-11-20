// 职责：注册CommonUI确认/返回动作数据。使用：引擎读取InputData CDO；不创建玩法InputAction或覆盖战斗映射。
#include "UI/Common/DemoCommonInputData.h"
#include "CommonUITypes.h"
#include "Debug/DemoLog.h"
namespace
{
    /** 仅初始化基类受保护键字段，添加到表时按基类UStruct复制，不保存派生结构。 */
    struct FDemoUIKeyRow : FCommonInputActionDataBase
    {
        /** Name是显示名；Keyboard/Gamepad为同一UI动作的设备键，无跨线程对象。 */
        FDemoUIKeyRow(const TCHAR* Name,FKey Keyboard,FKey Gamepad)
        { UE_LOG(LogFPSDemo,Log,TEXT("[CALL] UIKeyRow")); DisplayName=FText::FromString(Name); KeyboardInputTypeInfo.SetKey(Keyboard); DefaultGamepadInputTypeInfo.SetKey(Gamepad); }
    };
}
UDemoCommonInputData::UDemoCommonInputData()
{
    DEMO_LOG_CALL(); Actions=CreateDefaultSubobject<UDataTable>(TEXT("UIActions")); Actions->RowStruct=FCommonInputActionDataBase::StaticStruct();
    Actions->AddRow(TEXT("Back"),FDemoUIKeyRow(TEXT("返回"),EKeys::Escape,EKeys::Gamepad_FaceButton_Right));
    Actions->AddRow(TEXT("Accept"),FDemoUIKeyRow(TEXT("确认"),EKeys::Enter,EKeys::Gamepad_FaceButton_Bottom));
    DefaultBackAction.DataTable=Actions; DefaultBackAction.RowName=TEXT("Back"); DefaultClickAction.DataTable=Actions; DefaultClickAction.RowName=TEXT("Accept");
}
