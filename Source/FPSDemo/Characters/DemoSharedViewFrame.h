// 职责：共享第一人称基础眼位的纯值策略；游戏线程冻结稳定眼位/胶囊界限，动画工作线程仅在显式旧策略下使用当前head。依赖Core数学，不访问Pawn/World或复制状态。
#pragma once
#include "CoreMinimal.h"

struct FPSDEMO_API FDemoSharedViewFrame
{
    // true仅保留旧全身头部眼位兼容/隔离AB；默认生产调用显式从Appearance冻结false，不由是否渲染过相机决定。
    bool bUseAnimatedHead=true;
    // 稳定策略的世界眼位cm；已包含本Pawn胶囊/姿态与水平视向偏移，不含装饰CameraModifier。
    FVector StableEye=FVector::ZeroVector;
    // 本帧胶囊中心世界cm；仅值快照，不持有角色。
    FVector Center=FVector::ZeroVector;
    // 已按本Pawn控制旋转变换的眼位偏移世界cm，包含原低头前移规则。
    FVector EyeOffset=FVector::ZeroVector;
    // 原胶囊内水平安全半径cm，至少1；不改变碰撞尺寸。
    float Radius=1.f;
    // 稳定策略安全胶囊的世界半高cm，至少Radius；顶部半球需按Z缩小可用横向半径。
    float CapsuleHalfHeight=88.f;
    // 眼位相对胶囊中心的世界Z最小/最大cm，沿用原身体相机限制。
    FVector2D HeightLimits=FVector2D(-58.f,83.f);
    /** Head为已求值头骨世界cm，仅旧策略使用；稳定策略忽略Head并按真实半球夹限。返回基础眼位，线程安全纯数学。 */
    FVector Resolve(const FVector& Head) const;
};
