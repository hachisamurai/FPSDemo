// 职责：R04“钛灰·骨白”共用视觉Token与字体接口；CommonUI和Canvas只读引用，颜色不表示业务权限。
// 使用：sRGB色值经FLinearColor(FColor)转线性；Alpha只控制底板，不对整页前景统一降透明度。
#pragma once
#include "CoreMinimal.h"
#include "UI/DemoUIFont.h"

// 颜色对应批准的R04浏览器母版；资源、输入和数据权限均由既有模块管理。
namespace DemoUI
{
    inline const FLinearColor Surface(FColor(17,19,20)); // 缺图/面板基础钛灰；实际面板Opacity由原配置控制。
    inline const FLinearColor Raised=FLinearColor(FColor(36,40,42)).CopyWithNewOpacity(.42f); // 常态次级面板与按钮。
    inline const FLinearColor Hovered=FLinearColor(FColor(65,70,72)).CopyWithNewOpacity(.60f); // 指针反馈，保持中性灰。
    inline const FLinearColor Selected=FLinearColor(FColor(76,78,77)).CopyWithNewOpacity(.48f); // 显式选项背景，不能据此判断装备权限。
    inline const FLinearColor SelectedHovered=FLinearColor(FColor(99,101,99)).CopyWithNewOpacity(.64f); // 当前项叠加焦点/悬停。
    inline const FLinearColor Disabled=FLinearColor(FColor(40,43,45)).CopyWithNewOpacity(.28f); // 锁定/禁用底板。
    inline const FLinearColor Edge=FLinearColor(FColor(190,194,192)).CopyWithNewOpacity(.12f); // 必要分隔线，减少全矩形边框。
    inline const FLinearColor Overlay(0.002f,0.004f,0.005f,.80f); // 顶层模态遮罩，与普通页面半透明底板分离。
    inline const FLinearColor Ink(FColor(240,237,230)); // 骨白正文，不透明。
    inline const FLinearColor Muted(FColor(155,164,167)); // 灰色辅助说明。
    inline const FLinearColor Accent(FColor(143,185,177)); // 准备/已解锁等辅助青灰语义。
    inline const FLinearColor Gold(FColor(200,173,134)); // 香槟金，仅价格/小面积当前标记。
    inline const FLinearColor Danger(FColor(229,120,112)); // 危险/失败语义。
    inline const FLinearColor Primary(FColor(233,229,219)); // 主要行动骨白底，需配PrimaryInk。
    inline const FLinearColor PrimaryHovered(FColor(247,243,234)); // 主要行动悬停/焦点背景。
    inline const FLinearColor PrimaryInk(FColor(28,34,36)); // 骨白CTA上的深色文字。
}
