// 职责：最终组件空间瞄准节点；只消费AnimInstance快照，保持作者双手/枪的共同关系，不访问World。
#pragma once
#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "AnimNode_DemoPlayerAim.generated.h"

class UDemoAuthoredAimProfile;

/** 最终瞄准层：优先使用离线多方向姿态、分布式胸颈及完整枪框架；无配置保留旧骨控制兼容路径，工作线程不访问Pawn/World。 */
USTRUCT(BlueprintInternalUseOnly)
struct FPSDEMO_API FAnimNode_DemoPlayerAim : public FAnimNode_SkeletalControlBase
{
    GENERATED_BODY()
    /** 默认骨名兼容当前小白人骨架；索引随RequiredBones重建，不向共享Skeleton加骨。 */
    FAnimNode_DemoPlayerAim();
    // 来自游戏线程的受限俯仰度数；正数抬头，拥有者直接值，模拟代理为本地平滑值。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) float Pitch=0.f;
    // 已限制的头身水平夹角（度）；旧路径只转head，新近战分布式路径由胸腰分担一半、head补余角，不改变胶囊朝向。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) float HeadYaw=0.f;
    // 世界上向在实际身体组件空间的单位向量；求值时继承躯干俯仰后作为头部水平转轴，不随模型导入方向改变。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) FVector UpAxis=FVector::UpVector;
    // 角色真实右向转换至身体组件空间，不能硬编码模型朝向或在工作线程查Actor。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) FVector RightAxis=FVector::RightVector;
    // 输入Pose的spine_05局部中性枪管方向；不从上一帧世界武器推导，避免延迟反馈。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) FVector BoreInChest=FVector::ZeroVector;
    // 当前Pawn的真实瞄准单位向量，转换到实际Mesh组件空间，补偿网络Mesh平滑角。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) FVector TargetDirection=FVector::ForwardVector;
    // 有完整标定且使用刚体持枪时启用；兼容旧资产与独立近战瞄准。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) bool bCalibratedBore=false;
    // 0作者原动作、1完整方向对齐；换弹过渡暂让出，仍保留机械后坐和枪局部动画。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) float AlignmentWeight=0.f;
    // [0,0.35]为原腰部小角度比例；1为散弹枪完整上胸模式，共同父胸骨以上与枪共享刚体瞄准，腰与原握姿保留。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) float TorsoFraction=.18f;
    // 手枪自然左手为0，装填接触段为1；长枪始终1，取消过渡同步原上身动作混合。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) float SupportWeight=1.f;
    // 为当前姿态提供的组件空间握枪整体平移cm；只移动枪与腕目标，双臂IK保持肩/锁骨连接和原骨长。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) FVector GripOffset=FVector::ZeroVector;
    // 无武器/阵亡时为false，关闭握枪俯仰；无武器仍可单独使用HeadYaw，阵亡由AnimInstance将其清零。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) bool bEnabled=false;
    // 上身作者参考骨盆；独立IK支系先带入本帧移动骨盆，再与两臂一同瞄准，避免展示枪重复骨盆变换。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) FTransform ReferencePelvis=FTransform::Identity;
    // 新移动图已经在分层之前映射独立枪根，true时禁止在最终瞄准中重复骨盆变换。
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aim", meta=(PinShownByDefault)) bool bAnchorAlreadyAligned=false;
    /** Skeleton/RequiredBones为当前只读骨容器；必须有同父胸部的双锁骨以及原IK枪支系。 */
    virtual bool IsValidToEvaluate(const USkeleton* Skeleton,const FBoneContainer& RequiredBones) override;
    /** Output为完整混合Pose；OutBoneTransforms返回组件空间最终骨，不改胶囊/骨盆/腿或武器Gameplay。 */
    virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms) override;
    /** DebugData属于本次动画调试遍历；只输出节点名，不跨线程读取Pawn。 */
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;
    /** 在游戏线程发布当前枪的不可变作者配置；工作线程不读取Pawn或World。 */
    virtual bool HasPreUpdate() const override;
    /** 引擎保证本次任务前实例有效；只复制强引用，序列和配置不在求值时加载/修改。 */
    virtual void PreUpdate(const UAnimInstance* InAnimInstance) override;
private:
    /** 显式近战策略的FK瞄准：沿本LOD真实脊柱链分摊原总角，保留作者臂/指locals及全部骨长；缺链不发布部分结果，返回false沿旧路。 */
    bool EvaluateDistributedMeleeAim(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms);
    // PreUpdate在游戏线程从当前已装备/存活Melee配置取样；不读上一帧NativeUpdate缓存、不新增图引脚，工作线程只消费此纯值。
    bool bUseDistributedMeleeAim=false;
    // 本LOD的pelvis子骨到双锁骨共同胸骨，父先子后；只在InitializeBoneReferences重建，不固定Mesh/Compact骨编号。
    TArray<FCompactPoseBoneIndex> MeleeSpineChain;
    // 每Compact骨在上方真实链的位置（非链为INDEX_NONE），供本帧一次线性FK重建，避免逐帧按名字/父链查找。
    TArray<int32> MeleeSpineSteps;
    /** 新配置的完整瞄准路径；成功返回true，数据缺失返回false并记录原因，交由旧资产路径保底。 */
    bool EvaluateAuthoredAim(FComponentSpacePoseContext& Output,TArray<FBoneTransform>& OutBoneTransforms);
    // 当前Avatar枪械的只读姿态库；PreUpdate写入，工作线程只读，UProperty确保任务期间保活。
    UPROPERTY(Transient) TObjectPtr<UDemoAuthoredAimProfile> AuthoredProfile;
    // 当前LOD骨对应作者数组索引；配置变化或骨数变化时重新构建。
    TArray<int32> AuthoredIndices;
    // 映射生成时的配置身份，非拥有引用；AuthoredProfile或Appearance保活实际资产。
    const UDemoAuthoredAimProfile* MappedProfile=nullptr;
    // 最后稳定持枪的最终组件空间Pose；本节点独占，混入原生局部旋转差量且不复制原生绝对枪轴/骨长。
    TArray<FTransform> IdlePoseReference;
    FName IdleReferenceWeapon=NAME_None; // 身份变化或LOD重建时失效，不跨枪复用。
    int32 IdleReferenceBones=0; // 参考对应的紧凑骨数，拒绝LOD索引错配。
    bool bIdleReferenceValid=false; // 有完整中立采样后才应用待机，初始化/缺数据回退正常瞄准。
    /** RequiredBones变化时缓存真实骨父链归属，支持LOD重建，不假定左右骨索引连续。 */
    virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;
    /** Transforms为本帧待提交CS数组；Side=0左/1右，Target为公共枪框架推导的腕目标；不伸长骨，指骨随最终腕完整转动。 */
    void SolveArm(TArray<FTransform>& Transforms,int32 Side,const FTransform& Target,const FBoneContainer& Bones);
    // 参考链只用于索引，归当前节点拥有；胸部枢轴由两锁骨真实共同父骨推导。
    UPROPERTY() FBoneReference Spine;
    // 历史字段名Neck现以head关节为转头起点；旧neck_01/02值初始化时迁移，保留两节颈骨/喉片/衣口的基础姿态。
    UPROPERTY() FBoneReference Neck;
    UPROPERTY() FBoneReference LeftClavicle;
    UPROPERTY() FBoneReference RightClavicle;
    UPROPERTY() FBoneReference HandRoot;
    UPROPERTY() FBoneReference Pelvis;
    // 原骨架左右肩/肘/腕；[0..2]左、[3..5]右，初始化索引，不硬编码Mesh骨序。
    UPROPERTY() TArray<FBoneReference> Arms;
    // 每Compact骨的归属：0不改，1左臂，2右臂，3头及子骨，4上胸含颈骨，5独立手IK，6下方脊柱；只在骨引用初始化重建。
    TArray<uint8> Groups;
    // 每骨最近臂段祖先：0非臂段，1/2/3左上臂/小臂/手，4/5/6右；让扭转骨和手指继承IK结果。
    TArray<uint8> Segments;
};
