// 职责：首次运行操作指南的Canvas展示。使用：DemoHUD顶层覆盖页调用；关闭走MenuFlow，持久记录归设备设置，不写战局档。
#include "UI/DemoHUD.h"
#include "UI/DemoUIStyle.h"
#include "Debug/DemoLog.h"

void ADemoHUD::DrawFirstRunGuide()
{
    DEMO_LOG_TICK();
    const float X=ViewWidth*.5f-360.f,Y=ViewHeight*.5f-260.f; // 与现有菜单同一720×520逻辑像素，缩放和点击热区统一。
    Panel(0,0,ViewWidth,ViewHeight,DemoUI::Overlay,0);
    Panel(X,Y,720,520,GetPanelSurface(),12);
    Label(TEXT("B R E A C H  /  新手操作"),X+32,Y+24,14,DemoUI::Accent);
    Label(TEXT("准备好开始行动"),X+32,Y+58,30,DemoUI::Ink);
    Label(TEXT("关闭后，这台电脑以后启动也不会再显示此提示。"),X+32,Y+103,16,DemoUI::Muted);
    Label(TEXT("WASD  移动     鼠标  视角     SPACE  跳跃"),X+32,Y+151,18,DemoUI::Ink);
    Label(TEXT("左键  攻击     右键  瞄准 / 狙击镜     R  装填"),X+32,Y+187,18,DemoUI::Ink);
    Label(TEXT("1  主武器     2  手枪     3  近战武器"),X+32,Y+223,18,DemoUI::Ink);
    Label(TEXT("SHIFT  冲刺     Q  治疗     E  终端交互"),X+32,Y+259,18,DemoUI::Ink);
    Label(TEXT("TAB  玩家属性     ESC  菜单     G  标记"),X+32,Y+295,18,DemoUI::Ink);
    Label(TEXT("按住 T  语音通话，松开停止"),X+32,Y+331,18,DemoUI::Ink);
    Label(TEXT("出发前在升级终端选择流派核心；多人需全队准备。"),X+32,Y+381,16,DemoUI::Accent);
    Button(TEXT("OverlayBack"),TEXT("知道了，不再提示"),X+32,Y+438,656,48,true);
}
