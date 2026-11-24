#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GinkgoTraversalTypes.generated.h"

class UPrimitiveComponent;

/** 几何检测得到的动作类别；None 表示没有完整方案，不应抢占手部动作或消费普通跳跃。 */
UENUM(BlueprintType)
enum class EGinkgoTraversalKind : uint8
{
	None,
	Vault,
	Mantle,
	Climb,
};

/** 检测停止的位置；与动作中途取消原因分开，供灰盒调试和边界回归使用。 */
enum class EGinkgoTraversalFailure : uint8
{
	None,
	InvalidInput,
	InvalidState,
	NoWall,
	InitialOverlap,
	UnsupportedSurface,
	ObliqueWall,
	NoTop,
	Height,
	Hands,
	TopBlocked,
	NoSupport,
	NoLanding,
	PathBlocked,
};

/**
 * 翻越与攀爬的几何检测参数。距离均为 cm，角度均为度；由移动配置持有，PIE 中实时读取。
 * 当前仅检测静止支撑上的地面发起动作；几何范围与素材适配范围需要配套调整。
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoTraversalSettings
{
	GENERATED_BODY()

	/** 从胶囊前缘沿相机水平投影额外检查多远（cm）；角色查询用速度曲线求值覆盖本次副本，配置中的值只作空曲线回退。 */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = 1, ClampMax = 200, Units = "cm"))
	float Reach = 50.f;

	/** 最低边沿高度，相对本次检测的角色脚底（cm）；同时排除移动组件可直接 StepUp 的台阶。 */
	UPROPERTY(EditAnywhere, Category = "Height", meta = (ClampMin = 1, Units = "cm"))
	float MinHeight = 50.f;

	/** 最高可抓边沿，相对脚底（cm）；高于它的表面不参与本次攀爬。 */
	UPROPERTY(EditAnywhere, Category = "Height", meta = (ClampMin = 1, ClampMax = 350, Units = "cm"))
	float MaxHeight = 250.f;

	/** 矮墙与高墙攀爬的分界（cm）；本次结果生成后类型固定，动作不随高度重选。 */
	UPROPERTY(EditAnywhere, Category = "Height", meta = (ClampMin = 1, Units = "cm"))
	float LowHeight = 125.f;

	/** 翻越允许的最大障碍高度（cm）；高而薄的墙不能沿用矮墙翻越。 */
	UPROPERTY(EditAnywhere, Category = "Vault", meta = (ClampMin = 1, Units = "cm"))
	float MaxVaultHeight = 125.f;

	/** 翻越允许的最大墙体厚度，沿相机水平投影方向测量（cm）。 */
	UPROPERTY(EditAnywhere, Category = "Vault", meta = (ClampMin = 1, ClampMax = 200, Units = "cm"))
	float MaxVaultDepth = 70.f;

	/** 翻越落点允许低于起始脚底多少（cm）；超过它按没有安全落点处理。 */
	UPROPERTY(EditAnywhere, Category = "Vault", meta = (ClampMin = 0, ClampMax = 300, Units = "cm"))
	float MaxLandingDrop = 100.f;

	/** 从前沿向内查找落脚点的上限（cm），防止越过整个平台寻找远处可站位置。 */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = 10, ClampMax = 300, Units = "cm"))
	float MaxDepth = 160.f;

	/** 沿台面向前的采样间距（cm）；后缘再二分细化，不把采样步长当成墙厚精度。 */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = 2, ClampMax = 20, Units = "cm"))
	float ScanStep = 8.f;

	/** 正面探测球半径（cm），避免单射线轻易漏过边缘；手的支撑另外验证。 */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = 1, ClampMax = 15, Units = "cm"))
	float ProbeRadius = 6.f;

	/** 正面多高度探测的竖直间隔（cm），查询总量另有硬上限。 */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = 5, ClampMax = 40, Units = "cm"))
	float HeightStep = 20.f;

	/** 相机水平意图与墙面内法线的最大夹角（度），防止擦墙触发或绕角吸附。 */
	UPROPERTY(EditAnywhere, Category = "Detection", meta = (ClampMin = 0, ClampMax = 75, Units = "deg"))
	float MaxWallAngle = 55.f;

	/** 左右手距中线的横向距离（cm），沿相机水平投影的左右方向布置。 */
	UPROPERTY(EditAnywhere, Category = "Hands", meta = (ClampMin = 1, ClampMax = 45, Units = "cm"))
	float HandHalfSpan = 20.f;

	/** 手接触点从各自的前沿向里探入多少（cm），排除只有侧面而没有顶面支撑的栏杆。 */
	UPROPERTY(EditAnywhere, Category = "Hands", meta = (ClampMin = 1, ClampMax = 20, Units = "cm"))
	float HandInset = 6.f;

	/** 三条前沿采样及双手支撑的允许高度差（cm），超过时当前动画不能适配。 */
	UPROPERTY(EditAnywhere, Category = "Hands", meta = (ClampMin = 0, ClampMax = 20, Units = "cm"))
	float HeightTolerance = 8.f;

	/** 双手前沿相对中央前沿的允许前后偏差（cm），防止一手跨到第二堵墙或墙角背面。 */
	UPROPERTY(EditAnywhere, Category = "Hands", meta = (ClampMin = 0, ClampMax = 35, Units = "cm"))
	float HandDepthTolerance = 20.f;

	/** 站立支撑环在胶囊半径外增加的余量（cm），使落点不恰好卡在平台边上。 */
	UPROPERTY(EditAnywhere, Category = "Clearance", meta = (ClampMin = 0, ClampMax = 15, Units = "cm"))
	float SupportMargin = 4.f;

	/** 检查胶囊底部与地面的留空量（cm）；避免把正常接触误判为初始穿透。 */
	UPROPERTY(EditAnywhere, Category = "Clearance", meta = (ClampMin = 1, ClampMax = 10, Units = "cm"))
	float Clearance = 3.f;

	/** 保守路径抬升后，胶囊底部高出边沿的距离（cm）；执行阶段仍以实际根运动逐帧 Sweep 验证。 */
	UPROPERTY(EditAnywhere, Category = "Clearance", meta = (ClampMin = 1, ClampMax = 30, Units = "cm"))
	float PathClearance = 10.f;

	/** 候选墙面及支撑采样通道；身体空间另外使用实际角色碰撞响应，不能借此穿墙。 */
	UPROPERTY(EditAnywhere, Category = "Detection")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Pawn;

	/** 验证运行时参数，拒绝非法值并限制查询数量；不在检测函数里偷偷修改配置资产。 */
	bool IsValid() const;
};

/**
 * 一次纯几何查询的输入；控制源提供相机方向，检测器不读取角色朝向、不改变角色或动作状态。
 * 本阶段只允许世界 Z 为向上的地面发起；Feet、尺寸和碰撞响应必须来自同一次角色快照。
 */
struct CASEGINKGO_API FGinkgoTraversalQuery
{
	/** 被忽略的角色，不持有；只在同步调用期间读取。 */
	const AActor* Owner = nullptr;

	/** 当前胶囊底部的世界位置（cm），包括蹲伏后胶囊的实际高度。 */
	FVector Feet = FVector::ZeroVector;

	/** 相机前向在世界 XY 平面上的单位投影；零向量被拒绝，不回退到角色朝向。 */
	FVector Forward = FVector::ForwardVector;

	/** 实际胶囊半径（含组件缩放，cm）。 */
	float Radius = 34.f;

	/** 实际胶囊半高（含蹲伏与组件缩放，cm），用于检测起始穿透。 */
	float CurrentHalfHeight = 88.f;

	/** 站立半高（含组件缩放，cm），用于检测最终落点与保守通过路径。 */
	float StandingHalfHeight = 88.f;

	/** 移动组件当前 StepUp 上限（cm）；不把可直接跨上的台阶判成攀爬。 */
	float StepHeight = 45.f;

	/** 移动组件的可行走法线 Z 阈值，范围 [0,1]；支撑组件自己的坡度覆盖仍然生效。 */
	float WalkableFloorZ = .71f;

	/** 角色胶囊的实际对象通道，身体 Sweep 使用它而不是候选检测通道。 */
	ECollisionChannel BodyChannel = ECC_Pawn;

	/** 角色胶囊的实际碰撞响应，用于身体 Sweep 的双向过滤。 */
	FCollisionResponseContainer BodyResponses = FCollisionResponseContainer(ECR_Block);
};

/** 几何接触锚点；组件只借用，执行阶段锁定世界位置并监测组件变换，不延长支撑物生命周期。 */
struct CASEGINKGO_API FGinkgoTraversalContact
{
	/** 命中的支撑组件；世界退出或组件销毁后自动失效。 */
	TWeakObjectPtr<UPrimitiveComponent> Component;

	/** 检测时的世界位置（cm），用于路径、Warp Target 和手部 IK；支撑改变时取消而不移动锚点。 */
	FVector Position = FVector::ZeroVector;

	/** 相对支撑组件的局部位置（cm），有效期与 Component 相同。 */
	FVector LocalPosition = FVector::ZeroVector;

	/** 接触表面的世界单位法线；当前只接受可行走的顶部支撑。 */
	FVector Normal = FVector::UpVector;
};

/** 可选的调试探测记录；纯检测时不传数组，避免日常查询产生可视化分配。 */
struct FGinkgoTraversalProbe
{
	/** 探测起点的世界位置（cm），体积探测时为体积中心。 */
	FVector Start = FVector::ZeroVector;

	/** 探测终点的世界位置（cm），不是命中点。 */
	FVector End = FVector::ZeroVector;

	/** 球或胶囊半径（cm）；零表示射线。 */
	float Radius = 0.f;

	/** 胶囊半高（cm）；零表示射线或球。 */
	float HalfHeight = 0.f;

	/** 本次是否被阻挡；命中支撑不等同于最终失败。 */
	bool bHit = false;
};

/** 单次检测结果；失败时仍保留已找到的几何供调试，只有 IsValid 为真时才可用于后续启动。 */
struct CASEGINKGO_API FGinkgoTraversalResult
{
	/** 完整有效方案的动作类型；失败时为 None。 */
	EGinkgoTraversalKind Kind = EGinkgoTraversalKind::None;

	/** 最后失败原因；成功时为 None。 */
	EGinkgoTraversalFailure Failure = EGinkgoTraversalFailure::NoWall;

	/** 本次使用的世界水平单位方向，完全由相机意图决定。 */
	FVector Forward = FVector::ForwardVector;

	/** 检测实际采用的胶囊前缘外距离（cm）；动作排队复检沿用此快照，避免排队清零速度后缩回静止范围。未查询时为零。 */
	float Reach = 0.f;

	/** 中央前沿上可抓握的顶部接触点。 */
	FGinkgoTraversalContact Ledge;

	/** 左手的独立支撑，不要求与右手属于同一个组件。 */
	FGinkgoTraversalContact LeftHand;

	/** 右手的独立支撑，高攀必需；短翻越/低攀允许为空，不作为动作存续条件。 */
	FGinkgoTraversalContact RightHand;

	/** 最终脚底支撑位置；还没有加胶囊半高，不能直接作为 Actor 位置。 */
	FGinkgoTraversalContact Landing;

	/** 起点到正面墙的水平距离（cm，沿 Forward）；不包含手掌探入顶面的偏移，用于入场适配。 */
	float WallDistance = 0.f;

	/** 前沿相对起点脚底的高度（cm）。 */
	float Height = 0.f;

	/** 沿相机方向测得的顶面连续深度（cm）；未找到后缘时表示至少有这么深。 */
	float Depth = 0.f;

	/** 在 MaxDepth 内找到了明确后缘；false 不能据此选择翻越。 */
	bool bFoundBackEdge = false;

	/** 保守几何预检的胶囊中心控制点（世界 cm）；仅供粗筛和调试显示，不能作为根运动执行路径。 */
	TArray<FVector> Path;

	/** 本次实际物理查询次数，供预算检查；包括支撑和体积验证。 */
	int32 QueryCount = 0;

	bool IsValid() const { return Kind != EGinkgoTraversalKind::None && Failure == EGinkgoTraversalFailure::None; }
};
