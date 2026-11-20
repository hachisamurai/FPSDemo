// 职责：近战武器、招式、连招及动画通知窗口配置。使用：复制DA并替换资源/数值；窗口从标准TP动作读，脊柱参考从实际Idle读，不保存玩家运行状态。
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DemoMeleeDefinition.generated.h"
class UAnimSequence;
class UAimOffsetBlendSpace1D; // 可选FP持握俯仰加成，依赖同Skeleton/已还原来源基准；不改变TP扫掠。
class UAnimInstance;
class UStaticMesh;
class USkeletalMesh; // 可选近战专用手臂网格，与其动画Skeleton组成独立资源闭包。
class UMaterialInterface; // FP局部可见性材质只影响拥有者，不参与骨骼、握点或权威轨迹。
class USoundBase;

/** 一根作者中立脊柱的父骨局部旋转；导入器从实际Idle指定时刻回读，不含平移或缩放。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoMeleeSpineReferenceBone
{
    GENERATED_BODY()
    // 实际Skeleton骨名；完整覆盖pelvis的子链到spine_05，顺序不作为运行时索引。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spine Reference") FName BoneName;
    // UE局部空间单位四元数；按真实父骨取relative，不是组件/世界空间旋转。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spine Reference") FQuat LocalRotation=FQuat::Identity;
};

/** 从TP动作烘焙的均匀轨迹样本；原点为角色胶囊中心，+X前/+Y右/+Z上，单位cm。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoMeleeTracePose
{
    GENERATED_BODY()
    // 当前样本的棍身有效段起点，不包括握把。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trace") FVector Base=FVector::ZeroVector;
    // 同时刻棍头中心，与Base共同决定整根棍的扫掠。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Trace") FVector Tip=FVector::ZeroVector;
};

/** 同一招式的FP/TP资源、时间窗口和数值；新增招式不需要新增GA类型。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoMeleeAttack
{
    GENERATED_BODY()
    // 稳定招式键用于日志和动画调试，不以名称硬编码玩法分支。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity") FName Id;
    // 编辑器/HUD显示名，允许本地化。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity") FText DisplayName;
    // 总动作秒数，含前摇、有效打击与收招；所有观察者按服务器同一时钟取样。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0.2",ClampMax="5")) float Duration=.8f;
    // (运行时间比例,作者动作比例)，单调折线；空数组保持旧线性播放。用于仅压缩前摇/回位，命中和声音仍用作者phase。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing") TArray<FVector2D> PlaybackKeys;
    /** Elapsed为起招后秒数，返回0..1作者相位；表现/扫掠/声音统一调用，不允许只提前伤害而动画仍未出棒。 */
    float SampleProgress(float Elapsed) const;
    /** 作者phase反解到原运行时间（秒）；严格单调PlaybackKeys保证唯一，用于连招入口/分支/结束共用一个时钟。 */
    float RuntimeAtPhase(float Phase) const;
    // 启用后从标准TP唯一DemoMeleeTraceWindow通知状态读区间；每刀冻结，不依赖客户端/Teleport通知回调授权。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing") bool bUseAnimationNotifyHitWindow=false;
    // 旧资产有效打击窗口0..1；仅未启用通知时使用，保留便于回滚，不作为第二份同时生效的窗口。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(EditCondition="!bUseAnimationNotifyHitWindow")) FVector2D HitWindow=FVector2D(.28,.55);
    /** 起刀/校验同步读取有效窗口；OutWindow为0..1，Error为失败原因。缺失/重复通知失败，不静默回退旧配置。 */
    bool ResolveHitWindow(FVector2D& OutWindow,FString& Error) const;
    // 挥动声音归一化时刻；到达过晚的网络状态不补播过期声音。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0",ClampMax="1")) float SwingSoundPhase=.2f;
    // 基础伤害HP，先加WeaponDamageBonus，再乘实际命中部位倍率；不使用枪械元素弹/返弹天赋。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage", meta=(ClampMin="0",ClampMax="10000")) float Damage=35.f;
    // 同一招式最多击中不同敌人数；1用于纵劈，横挥可配置多个，每敌只结算一次。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage", meta=(ClampMin="1",ClampMax="16")) int32 MaxTargets=3;
    // 实际扣血且目标存活时的平面击退距离cm，0关闭；服务器扫掠防穿墙，旧资产缺字段时采用150cm默认。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Knockback", meta=(ClampMin="0",ClampMax="600")) float KnockbackDistance=150.f;
    // 击退总时长秒，0.05..2；位移先快后慢，与攻击动画/后摇分别配置，默认0.25秒提供短促打击感。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Knockback", meta=(ClampMin="0.05",ClampMax="2")) float KnockbackDuration=.25f;
    // 拥有者专用动作，与TP共享Duration/HitWindow及相位；只改变取景和姿态，绝不参与服务器伤害扫掠。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person") TObjectPtr<UAnimSequence> FirstPerson;
    // 真实全身动作；移动时使用上身层，允许静止完整动作时骨盆/双腿同源参与，配置在武器定义。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation") TObjectPtr<UAnimSequence> ThirdPerson;
    // None禁用连招、保持原完整Standalone；非None为本资产主循环中真实下一刀的稳定ID，不接受客户端指定任意目标招式。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combo") FName ComboTargetId;
    // 已审前缀严格兼容Standalone、Dec末转Antic的配套源；可含完整后续招式，但本刀只采至ComboEndPhase，不替换原轨迹表。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combo") TObjectPtr<UAnimSequence> ComboFirstPerson;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combo") TObjectPtr<UAnimSequence> ComboThirdPerson; // 远端同一前缀/Antic源，必须与FP同总长。
    // 当前招原作者0..1相位：新点击必须在此之前抵达服务器；分支前原伤害窗口必须已经结束。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combo", meta=(ClampMin="0",ClampMax="1")) float ComboBranchPhase=0.f;
    // 当前招原作者0..1相位：连招源末端；实际秒数由RuntimeAtPhase反解，不能按变体长度重新归一化。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combo", meta=(ClampMin="0",ClampMax="1")) float ComboEndPhase=0.f;
    // 目标Standalone的归一化入口phase，允许跳过其重复Windup；下一刀从此初始化伤害扫描，绝不补前摇伤害。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combo", meta=(ClampMin="0",ClampMax="1")) float ComboEntryPhase=0.f;
    // 原创挥动声，可替换SoundCue/SoundWave；只承担表现。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio") TObjectPtr<USoundBase> SwingSound;
    // 从真实TP棍姿按等间隔0..1导出；服务器无需渲染骨架仍能覆盖低帧率跨过的完整攻击窗口。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") TArray<FDemoMeleeTracePose> TracePoses;
    // 与TracePoses一一对应的武器完整胶囊局部变换；Editor从真实握点烘焙。
    // 只供显式分层握点锚定策略，把本帧密采曲线映射到服务器实际已求值手骨，不复制逐帧骨骼。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") TArray<FTransform> WeaponFrames;
};

/** 本人近战装饰反馈；仅最终Roll，不改变共享眼位、瞄准前向或ControlRotation，无网络/存档状态。 */
USTRUCT(BlueprintType)
struct FPSDEMO_API FDemoMeleeCameraFeedbackSettings
{
    GENERATED_BODY()
    // 关闭后本武器不创建Modifier；保留动画、音效、伤害和本人受击反馈。
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Camera") bool bEnabled=true;
    // 单次挥刀最大Roll度数；沿招式左右交替，默认很轻，运行保护0..0.3。
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Camera",meta=(ClampMin="0",ClampMax="0.3")) float SwingRollDegrees=.12f;
    // 实际HP伤害回执的单刀确认Roll度数；同ShotId多个目标只一次，运行保护0..0.4。
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Camera",meta=(ClampMin="0",ClampMax="0.4")) float HitRollDegrees=.20f;
    // 挥击/命中包络游戏秒数，0.05..0.3；不推进动画或延长攻击窗口。
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Camera",meta=(ClampMin="0.05",ClampMax="0.3")) float SwingSeconds=.14f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Camera",meta=(ClampMin="0.05",ClampMax="0.3")) float HitSeconds=.10f;
    // 本模块挥击+命中的合成上限度数；既有受击先执行后，再限制总Roll到0.65度，不挤占大幅特殊相机模式。
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Camera",meta=(ClampMin="0",ClampMax="0.4")) float MaximumRollDegrees=.28f;
};

/** 只读武器定义；角色组件的DefinitionAsset是接入入口，运行时绝不改共享资产。 */
UCLASS(BlueprintType)
class FPSDEMO_API UDemoMeleeDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    // 本机小幅装饰反馈，与真实动作/HP回执绑定；不复制配置，不按逐帧相机变换发RPC。
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Feedback|Camera") FDemoMeleeCameraFeedbackSettings CameraFeedback;
    // 身份用于日志/HUD；当前默认开局拥有，不写入旧枪械解锁账本。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity") FName WeaponId=TEXT("baseball_bat");
    // 玩家看到的名称，不作为玩法条件。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Identity") FText DisplayName;
    // 低多边形静态网格；使用实际武器局部坐标及GripOffset，刀刃不要求沿固定+Z，轨迹端点单独配置。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") TObjectPtr<UStaticMesh> Mesh;
    // 两视角均挂在真正右手骨，不采用枪械专属ik_hand_gun偏移。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") FName HandSocket=TEXT("hand_r");
    // 武器相对HandSocket的厘米/度变换；作者导入器标定，不修改共享Skeleton。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Visual") FTransform GripOffset=FTransform::Identity;
    // 相机空间cm/度；整体移动完整肩肘腕链，不独立挪手破坏握点。TP与权威轨迹不使用此取景配置。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person") FTransform FirstPersonOffset=FTransform(FRotator(0,-90,0),FVector(8,0,-166));
    // 可编辑主图/Linked Layer；独立于枪械ASC主实例，切近战不重绑GAS动画所有权。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person") TSubclassOf<UAnimInstance> FirstPersonAnimClass;
    // 可选近战手臂；为空沿用Appearance.ArmsMesh。枪械换骨架时明确指定旧网格，避免改变已验收棒球棍动作。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person") TObjectPtr<USkeletalMesh> FirstPersonArmsMesh;
    // 全身FP模式下近战可单独显示同源动作，仅应用FirstPersonOffset整体取景；枪械策略不变，伤害仍读TP源。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person") bool bIndependentFirstPersonDisplay=false;
    // 独立近战双臂启用时，本机Body改用下身分区；去掉头颈/近眼躯干及双臂，保腿脚并LeaderPose跟随真实Body，避免双份手臂与自体遮挡。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person") TObjectPtr<USkeletalMesh> OwnerBodyWithoutArmsMesh;
    // 按材质槽索引的可选FP覆盖；空项保留原材质。用于裁掉近镜头肩口，不移动/缩放骨骼，不影响TP和枪械。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person") TArray<TObjectPtr<UMaterialInterface>> FirstPersonMaterials;
    // 拥有者低位待机。每个FP攻击首尾须与它一致；取消使用下方过渡秒数回位。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person") TObjectPtr<UAnimSequence> FirstPersonIdle;
    // 可选本人持握look层：Pitch度为X轴，样本为MeshSpace additive；相机已完成整角俯仰，此层只配作者的相对持握变化。
    // 默认空保持旧图；仅Idle渐入、攻击淡出，不作为权威瞄准/碰撞源。运行配置随Pawn重建，不支持共享资产热改。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person|Look") TObjectPtr<UAimOffsetBlendSpace1D> FirstPersonLookOffset;
    // look层进出秒[0.03,0.3]，保护原攻击/连招姿态；与相机位置/控制旋转及GAS截止时钟独立。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person|Look", meta=(ClampMin="0.03",ClampMax="0.3")) float FirstPersonLookBlendSeconds=.10f;
    // 秒，0.03..0.3；同一近战装备内的动作/取消姿态混合，不改变攻击时长或有效窗口。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|First Person", meta=(ClampMin="0.03",ClampMax="0.3")) float FirstPersonBlendSeconds=.10f;
    // 正常收招表现秒[0.08,0.4]：FP标准惯性过渡与TP Slot混出共用；不延长GA/输入冷却/伤害窗口，源Idle从匹配帧0接入。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Recovery", meta=(ClampMin="0.08",ClampMax="0.4")) float MeleeRecoveryBlendSeconds=.18f;
    // 显式近战分布式腰背Aim，只对使用实际已求值握点扫掠的DA开放；原总俯仰限制不变，枪械/旧DA保持旧策略。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Aim") bool bUseDistributedThirdPersonAim=false;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation") TObjectPtr<UAnimSequence> ThirdPersonIdle; // 与当前攻击首尾一致的TP待机，不能混用旧双手肩位。
    // 候选可选：装备期间将TP待机/攻击/取消的脊柱旋转相对实际参考姿态的delta叠到同帧Lyra脊柱，保留横移腰背反向补偿。
    // 默认false保留旧武器/枪械行为；只叠spine旋转，平移/缩放及Root/骨盆/腿仍来自移动，不恢复捕获的骨盆位移或转体。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Upper Body") bool bUseAuthoredSpine=false;
    // 显式候选策略：将作者脊柱相对实际武器Idle的动态旋转叠到Lyra；不再把Idle静态弯身对A-Pose的差量重复叠加。
    // 默认false保持全部旧DA/枪械公式；启用时必须使用作者脊柱与上身Montage，并由下面完整中立数据校验通过。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Upper Body|Idle Reference") bool bUseWeaponIdleSpineReference=false;
    // 中立数据来源，必须与本DA的ThirdPersonIdle同一对象；只作身份/骨架校验和导入溯源，工作线程不采样UObject。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Upper Body|Idle Reference") TObjectPtr<UAnimSequence> SpineReferenceIdle;
    // 秒；导入器实际采样中立源的位置，0..源长度；不改变当前Idle播放时钟或攻击相位。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Upper Body|Idle Reference", meta=(ClampMin="0")) float SpineReferenceTime=0.f;
    // 源文件/导入版本及SHA标识，用于识别数据是否已随Idle重烘；不能把旧参考隐式套到新Idle。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Upper Body|Idle Reference") FString SpineReferenceRevision;
    // 完整脊柱局部旋转列表；仅导入/作者编辑，运行时PreUpdate复制为纯值，不把源骨盆、平移或缩放混入移动基底。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Upper Body|Idle Reference") TArray<FDemoMeleeSpineReferenceBone> SpineReferenceBones;
    /** 装备校验及动画PreUpdate游戏线程调用；仅复制并校验中立五骨纯值，失败清空输出并返回具体原因。不在动画工作线程读资产。 */
    bool CopySpineReference(TMap<FName,FQuat>& OutRotations,FString& Error) const;
    // 装备此武器时通过DemoMeleeUpperBody Montage独占上身攻击，Root/骨盆/腿始终保留Lyra；
    // 必须配合已安装同名Slot的DemoLyra主图。默认false保持旧武器；所有客户端读取相同只读DA，无独立复制时钟。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Upper Body") bool bUseUpperBodyMontage=false;
    // 上身分层武器可选：将密采烘焙弧线按本帧服务器实际握点重锚，保留Lyra骨盆/步态变化；
    // 要求同数量WeaponFrames，Body必须先求值。默认false保留旧武器；低帧跨窗使用本帧基底，不做客户端回溯。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") bool bAlignTraceToEvaluatedGrip=false;
    // 静止挥击使用源动画完整骨盆/腿姿；默认关闭以兼容旧武器。开启前需制作原地足锁动作，不依赖RootMotion移动胶囊。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Full Body") bool bStationaryFullBody=false;
    // cm/s，超过此速度完整动作权重为0，恢复移动步态；30让起步及时退出足锁，不在跑动时硬套站姿。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Full Body", meta=(ClampMin="10",ClampMax="200")) float FullBodyFadeSpeed=30.f;
    // 秒；原地全身层进入/退出过渡，0.12与既有上身动作混合接近，避免突然扭骨盆。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation|Full Body", meta=(ClampMin="0.03",ClampMax="0.4")) float FullBodyBlendSeconds=.12f;
    // 至少1项：第0项左键主攻击，第1项可选右键副攻击；其余仅由服务器主攻击循环选取，客户端不接受任意下标。
    // 第1项默认不参与左键循环；只有下面显式许可开启时，同一副攻击才可同时供右键直选和左键稳定ID循环使用。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attacks") TArray<FDemoMeleeAttack> Attacks;
    // 左键循环稳定ID；空数组使用第0项，最多8项；默认排除第1项以保留旧副攻槽合同，显式许可见下方字段。
    // 右键仅在存在第1项时直选副攻且不推进左键游标；左键权威成功开始后推进；可选收招重开策略见下方，不保存游标。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attacks") TArray<FName> HorizontalAttackSequence;
    // 只读武器配置许可，默认false保留旧资产对第1项副攻的循环禁令；true允许其稳定ID加入上方左键循环，右键仍直选同一招。
    // 不增删招式、不改变输入解析/权威游标/伤害或网络字段；旧DA未序列化此字段时保持false，所有端应使用同版DA。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attacks") bool bAllowSecondaryInHorizontalSequence=false;
    // 完整收招后的新左键组从HorizontalAttackSequence第0项起；默认false保留旧武器跨次独立挥击的循环行为。
    // 仅决定Standalone首刀选择；GAS真实Begin成功才从所选0位推进，GA失败不改游标；已承诺连段仍按原目标/游标接刀，右键不受影响。
    // 纯只读DA配置，不添加复制/存档字段；所有观察者与拥有者预测必须使用同版配置。是否两刀结束由各招ComboTargetId决定。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Attacks") bool bRestartPrimarySequenceAfterRecovery=false;
    // 可选纯表现策略：真实连段子刀继续读取父Combo资源中已作者验证的完整目标/Recovery上下文，不在刀间更换源。
    // 默认false兼容旧武器；当前仅支持bUseUpperBodyMontage=true的TP秒数通路，旧Evaluator的phase重归一化路线不启用。
    // 每条Combo的FP/TP父完整源均须覆盖父出口+目标剩余全段；目标可继续接刀，只有停止点击才走其完整Recovery。
    // 子刀尚未承诺后继时读父完整上下文；承诺后切到子刀自己的兼容前缀/Antic，下一刀再沿该完整源继续。
    // Action/Progress/Serial、GA/伤害/窗口/Duration仍属当前子刀；只读DA配置，不新增State/RPC或SaveGame字段。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combo") bool bUseFullContextForComboContinuation=false;
    // 收招结束前可缓存一次本机点击的秒数，0关闭；范围0..0.3。取消/阶段切换清空，不会自动连打或提前刷新服务器冷却。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Timing", meta=(ClampMin="0",ClampMax="0.3")) float InputBufferSeconds=.15f;
    // 棍网格局部厘米：只检测金属击打有效段，握把不作为额外伤害体；换材质不改变轨迹。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") FVector TraceBase=FVector(0,0,25);
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") FVector TraceTip=FVector(0,0,80); // 末端必须落在模型棍头。
    // 球扫掠半径cm，默认贴近粗棍头；每帧沿整根棍与上帧路径采样，不只检测单点。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="1",ClampMax="10")) float TraceRadius=3.5f;
    // 额外距离上限cm，防配置异常/传送产生远程伤害；依据玩家胶囊中心检查真实命中点。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision", meta=(ClampMin="80",ClampMax="250")) float MaxReach=180.f;
    // 肩部瞄准枢轴在角色胶囊空间cm，与TP上胸旋转枢轴标定；轨迹和动画共同使用有限俯仰。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Collision") FVector AimPivot=FVector(0,0,35);
    // TP/权威轨迹上身俯仰限制（度）；FP随相机取景，不能用其棍尖越过此权威攻击范围。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Animation", meta=(ClampMin="0",ClampMax="30")) float AimPitchLimit=20.f;
    // 真实怪物HP损失后送入共享金属池（固定声部/空间衰减）；可按近战定义覆盖Cue，无效部位/零伤害不播。
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio") TObjectPtr<USoundBase> EnemyImpactSound;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio") TObjectPtr<USoundBase> WorldImpactSound; // 坚硬表面回响。
    /** 载入或编辑器验收调用；Error为失败原因；验证资源/窗口/数值，完整上下文策略须TP Montage及每条边的目标完整Recovery覆盖，失败禁止装备。 */
    UFUNCTION(BlueprintCallable, Category="Melee") bool Validate(FString& Error) const;
};
