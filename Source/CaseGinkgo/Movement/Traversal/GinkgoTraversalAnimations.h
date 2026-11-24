#pragma once

#include "Engine/DataAsset.h"
#include "GinkgoTraversalTypes.h"
#include "GinkgoTraversalMotion.h"
#include "GinkgoTraversalAnimations.generated.h"

class UAnimMontage;

/** 一只手的接触窗口；四个时刻均为蒙太奇时间（秒），两端平滑渐入/渐出。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoTraversalHandWindow
{
	GENERATED_BODY()

	/** 开始向墙沿贴合的时刻（秒）；此前保留原动画。 */
	UPROPERTY(EditAnywhere, Category = "Contact")
	float Begin = 0.f;

	/** 完全贴合的时刻（秒），不得早于 Begin。 */
	UPROPERTY(EditAnywhere, Category = "Contact")
	float Contact = 0.f;

	/** 开始脱手的时刻（秒），此前目标固定在同一墙沿。 */
	UPROPERTY(EditAnywhere, Category = "Contact")
	float Release = 0.f;

	/** 完全回到原动画的时刻（秒）。 */
	UPROPERTY(EditAnywhere, Category = "Contact")
	float End = 0.f;

	float Weight(float Time) const;
	bool IsValid(float Duration) const;
};

/** 一类攀爬的素材、适配范围及独立碰撞代理；保留原轨迹，不规定动画如何经过墙沿。 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoTraversalClip
{
	GENERATED_BODY()

	/** 全身 DefaultSlot 蒙太奇，带整段根运动适配窗口；由资源资产强引用以供打包。 */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TObjectPtr<UAnimMontage> Montage;

	/** 是否需要双手并暂时收枪；短翻越、低攀爬为 false，只占左手，右手保留当前武器。 */
	UPROPERTY(EditAnywhere, Category = "Hands")
	bool bTwoHanded = true;

	/** 足部支撑成立后，全身姿势让回 Locomotion 的时长（秒）；无论是否按移动都开始让出，不影响墙上的根运动。 */
	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ClampMin = .05, ClampMax = .3, Units = "s"))
	float MovementBlendOut = .18f;

	/** 支撑交接后保留的素材尾段（秒），供全身淡出采样；它不继续锁定输入或等待角色站直。 */
	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ClampMin = .2, Units = "s"))
	float RecoveryDuration = .6f;

	/** 自然结束前的兜底淡出时长（秒）；正常流程在支撑事件处使用 MovementBlendOut 提前让出。 */
	UPROPERTY(EditAnywhere, Category = "Animation", meta = (ClampMin = .1, Units = "s"))
	float BlendOutDuration = .35f;

	/** 根运动结束及足部支撑交接时刻（秒），允许早于完全站直；此前保持素材的前进、抬升、下落节奏。 */
	UPROPERTY(EditAnywhere, Category = "Animation")
	float MotionEndTime = .8f;

	/** 原素材对应的墙高（cm），用于整段竖直适配；角色骨骼和碰撞体不随它缩放。 */
	UPROPERTY(EditAnywhere, Category = "Adaptation", meta = (ClampMin = 1, Units = "cm"))
	float ReferenceHeight = 100.f;

	/** 起跳至支撑手触边的上升段结束时刻（蒙太奇秒数）；水平位置及起步朝向在此对齐。 */
	UPROPERTY(EditAnywhere, Category = "Adaptation")
	float RiseEndTime = .26f;

	/** 触边时角色根在边沿前方保留的水平距离（cm，沿检测方向）；独立于起步距离，IK 补偿手掌位置。 */
	UPROPERTY(EditAnywhere, Category = "Adaptation", meta = (ClampMin = 0, ClampMax = 100, Units = "cm"))
	float ContactRootDistance = 22.f;

	/** 整段竖直轨迹允许的缩放范围 [最小,最大]，只改变幅度，不改变峰值所在时刻。 */
	UPROPERTY(EditAnywhere, Category = "Adaptation")
	FVector2D VerticalScaleRange = FVector2D(.5, 1.5);

	/** 落点高度的额外平滑修正上限（cm）；超过时拒绝素材，不能靠大幅修正重写起落曲线。 */
	UPROPERTY(EditAnywhere, Category = "Adaptation", meta = (ClampMin = 0, Units = "cm"))
	float MaxHeightCorrection = 25.f;

	/** 查询胶囊的姿势关键帧，必须从正常胶囊开始并恢复正常胶囊；不写动画根轨迹。 */
	UPROPERTY(EditAnywhere, Category = "Collision")
	TArray<FGinkgoTraversalBodyKey> Body;

	/** 左手贴合窗口（蒙太奇秒数），与右手独立，允许先后抓住和松开。 */
	UPROPERTY(EditAnywhere, Category = "Hands")
	FGinkgoTraversalHandWindow LeftHand;

	/** 右手贴合窗口（蒙太奇秒数）。 */
	UPROPERTY(EditAnywhere, Category = "Hands")
	FGinkgoTraversalHandWindow RightHand;

	/** 手腕高出支撑点的距离（cm，沿支撑法线），为手掌厚度留空。 */
	UPROPERTY(EditAnywhere, Category = "Hands", meta = (ClampMin = 0, ClampMax = 15, Units = "cm"))
	float WristHeight = 5.f;

	/** 肩到手腕的最大长度（cm），IK 不通过拉长手臂弥补失配。 */
	UPROPERTY(EditAnywhere, Category = "Hands", meta = (ClampMin = 1, Units = "cm"))
	float ArmReach = 65.f;

	/** 抓点偏低或偏远时额外弯腰上限（度）；沿现有脊柱分配比例修正，不移动骨盆或拉长手臂。 */
	UPROPERTY(EditAnywhere, Category = "Hands", meta = (ClampMin = 0, ClampMax = 60, Units = "deg"))
	float MaxBodyLean = 60.f;

	bool IsValid() const;
};

/** 移动配置引用的攀爬素材集合；检测参数仍在 MovementConfig，素材及时序单独维护。 */
UCLASS(BlueprintType)
class CASEGINKGO_API UGinkgoTraversalAnimations : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 矮薄障碍的单手 run 翻越主体，接停步落地；源素材的长接近和下落尾段不参与。 */
	UPROPERTY(EditAnywhere, Category = "Traversal")
	FGinkgoTraversalClip Vault;

	/** 起步时有移动意图的同款翻越，接跑动落地，落稳后立即归还移动输入。 */
	UPROPERTY(EditAnywhere, Category = "Traversal")
	FGinkgoTraversalClip VaultMoving;

	/** 可站立矮台的攀上片段。 */
	UPROPERTY(EditAnywhere, Category = "Traversal")
	FGinkgoTraversalClip Mantle;

	/** 高台的抓边、拉起和踏上片段。 */
	UPROPERTY(EditAnywhere, Category = "Traversal")
	FGinkgoTraversalClip Climb;

	const FGinkgoTraversalClip* Find(EGinkgoTraversalKind Kind, bool bMovingExit = false) const;
};

/** 游戏线程生成、动画线程只读的双手快照；无 UObject 或组件引用。 */
struct FGinkgoTraversalHandPose
{
	/** 正在执行或淡出的攀爬；实际姿势覆盖程度由 Weight 决定。 */
	bool bActive = false;

	/** 仍由根运动控制身体；落稳后即为 false，不能用 bActive 代替它冻结 Locomotion。 */
	bool bDrivingRootMotion = false;

	/** 高攀使用双手；false 时右手持续持枪，墙沿 IK 只接管左手。 */
	bool bTwoHanded = true;

	/** 全身蒙太奇的当前混合权重 [0,1]；手部和持枪覆盖随它连续退出，不能等 bActive 清零才瞬切。 */
	float Weight = 0.f;

	/** 左手腕的世界位置（cm），接触窗口内固定在经验证的墙沿。 */
	FVector Left = FVector::ZeroVector;

	/** 右手腕的世界位置（cm）。 */
	FVector Right = FVector::ZeroVector;

	/** 左侧支撑面的世界单位法线，用于在原片段手腕朝向上叠加倾斜面的转角。 */
	FVector LeftNormal = FVector::UpVector;

	/** 右侧支撑面的世界单位法线。 */
	FVector RightNormal = FVector::UpVector;

	/** 左手接触窗口权重 [0,1]，使用时再乘全身 Weight。 */
	float LeftAlpha = 0.f;

	/** 右手接触窗口权重 [0,1]；单手动作恒为零，使用时再乘全身 Weight。 */
	float RightAlpha = 0.f;

	/** 肩至手腕的最大长度（cm），供动画线程限制目标。 */
	float ArmReach = 65.f;

	/** 本片段额外弯腰上限（度），仅随接触权重生效，脱手后归还原动画。 */
	float MaxBodyLean = 60.f;
};

namespace Ginkgo::Traversal
{
	/** 落稳完成通知；此时根运动已到最后目标，动画还有收势段，避免引擎最后一帧失去 Warp 上下文。 */
	extern CASEGINKGO_API const FName LandedEvent;

}
