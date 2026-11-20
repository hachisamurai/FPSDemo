#pragma once
#include "CoreMinimal.h"

// 包括声明在内仅Editor编译；不提供包体可调用的调试接口或运行时绕过开关。
#if WITH_EDITOR
class AActor; // 无敌接口只借用Avatar，不要求调用者包含角色定义。
namespace DemoEditorCheats
{
    /** Context仅定位当前GI，Flag=0金币/1银币/2无敌；只允许单人Editor游戏World。 */
    FPSDEMO_API bool Enabled(const UObject* Context,int32 Flag);
    /** Context为本次付款所属World，bGold决定币种，QuotedCost仍正常校验/显示；启用时实际扣款为0，不注入巨额余额。 */
    FPSDEMO_API int32 Debit(const UObject* Context,bool bGold,int32 QuotedCost);
    /** Avatar必须是本机DemoCharacter，无敌不保护敌人/队友，也不复活已死亡玩家。 */
    FPSDEMO_API bool Invulnerable(const AActor* Avatar);
    /** HUD显示当前三个测试开关，关闭时为空，不作为业务权限来源。 */
    FPSDEMO_API FString Status(const UObject* Context);
}
#endif
