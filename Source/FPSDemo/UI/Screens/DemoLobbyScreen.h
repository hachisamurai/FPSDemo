// 职责：大厅根页的UMG表现。使用：CommonUI菜单栈创建，按钮沿用MenuFlow；云服务仅打包游戏存在。
#pragma once
#include "UI/Common/DemoMenuScreen.h"
#include "DemoLobbyScreen.generated.h"
UCLASS(Blueprintable)
class FPSDEMO_API UDemoLobbyScreen : public UDemoMenuScreen
{
    GENERATED_BODY()
public:
    /** 初始化旧壳兼容字段；R02正文显式采用纯Base全屏，不恢复历史背景图。 */
    UDemoLobbyScreen(const FObjectInitializer& ObjectInitializer);
protected:
    /** 本大厅显式启用R02；原StartGame/合作/设置/退出及槽守卫仍归Flow/协调器。 */
    virtual bool UsesR02Presentation() const override;
    /** 比较GI缓存三槽摘要、诊断/已有云状态和视口；不在绘制/刷新键读取磁盘。 */
    virtual FString RefreshKey() const override;
    /** 创建R02大厅及三条真实档案；只借GI缓存，不直接读盘、创建或写存档。 */
    virtual void BuildBody() override;
private:
    /** 本次树构建左品牌/动作与底部横向三槽简行；UI.LobbySlot0..2由协调器桥接原StartGame→Saves→SaveN。 */
    void BuildR02Lobby();
    /** Parent为当前树，Size为DPI前point；字体/灰字仅表现，WrapWidth>0时按本次真实列宽预先换行。 */
    UTextBlock* AddLobbyLabel(UPanelWidget* Parent,const FString& Text,int32 Size=18,bool Serif=false,bool Muted=false,bool Latin=false,float WrapWidth=0.f);
    /** Parent拥有Host；Vertical仅选择线方向，完整累计缩放和绝对起点保证1物理像素，不接收输入。 */
    void AddLobbyRule(UPanelWidget* Parent,bool Vertical=false);
};
