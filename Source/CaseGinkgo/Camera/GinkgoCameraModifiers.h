#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"  // FRuntimeFloatCurve：连续量经曲线映射
#include "GameplayTagContainer.h"
#include "GinkgoCameraContextComponent.h"  // FGinkgoCameraEvent：叠加效果按事件的时刻推进
#include "GinkgoCameraParams.h"
#include "GinkgoSpring.h"
#include "UObject/Object.h"
#include "GinkgoCameraModifiers.generated.h"

// 相机修改器的配置（Docs/Design/CameraPipeline.md「修改器结构：四块拆分」）：都挂在相机配置资产里，只放参数；
// 运行时的状态（权重、弹簧）由玩家相机组件持有（FGinkgoCameraParamLayer、FGinkgoCameraOverlayLayer）。

/** @enum EGinkgoCameraCompare : 连续量的条件怎么比 */
UENUM(BlueprintType)
enum class EGinkgoCameraCompare : uint8
{
	AtLeast	UMETA(DisplayName = "不小于"),
	Below	UMETA(DisplayName = "小于"),
};

/**
 * @struct FGinkgoCameraScalarCondition : 激活条件里的一个连续量条件
 * 上下文的连续量与一个常量或另一个连续量比。拿两个连续量比（水平速度不小于跑速）时阈值跟着移动配置走，改了跑速相机不用改
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraScalarCondition
{
	GENERATED_BODY()

	/** 上下文里的连续量；上下文里没有时当作 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition", meta = (DisplayName = "连续量"))
	FGameplayTag Scalar;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition", meta = (DisplayName = "比较"))
	EGinkgoCameraCompare Compare = EGinkgoCameraCompare::AtLeast;

	/** 拿来比的另一个连续量（例如移动配置的跑速）；空着时比 Threshold */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition", meta = (DisplayName = "比较的连续量"))
	FGameplayTag ThresholdScalar;

	/** 常量阈值，ThresholdScalar 空着时用 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Condition", meta = (DisplayName = "阈值"))
	float Threshold = 0.f;

	/** @brief 成立没有 */
	bool Holds(const UGinkgoCameraContextComponent* Context) const;

	/**
	 * @brief 余量：成立的一边为正（不小于：连续量减阈值；小于：阈值减连续量）
	 * @note 相机拿前后两帧的余量插出它在帧里哪一刻跨过 0，权重从那一刻起淡入淡出，结果与帧率无关（连续量在帧里线性变化时是精确的）
	 */
	double Margin(const UGinkgoCameraContextComponent* Context) const;
};

/**
 * @struct FGinkgoCameraActivation : 激活条件
 * 只决定权重的目标是 0 还是 1（四块拆分里的第一块，不多态）：标签查询与连续量的条件都成立才激活。
 * 只读相机上下文的标签与连续量，不依赖任何组件类型
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraActivation
{
	GENERATED_BODY()

	/** 读上下文的标签；空的查询总是成立 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (DisplayName = "标签查询"))
	FGameplayTagQuery Query;

	/** 连续量的条件，全部成立才激活 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (DisplayName = "连续量条件"))
	TArray<FGinkgoCameraScalarCondition> Conditions;

	/** @brief 标签查询成立没有（空的查询总是成立；没有上下文时只有空的查询成立） */
	bool MatchesQuery(const UGinkgoCameraContextComponent* Context) const;
};

/**
 * @class UGinkgoCameraValue : 修改器的数值来源
 * 四块拆分里唯一多态的一块：常量、上下文的连续量经曲线映射，以后加自定义计算（墙跑按墙面法线）只加一个小类
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class CASEGINKGO_API UGinkgoCameraValue : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 求值
	 * @param Context - 视角目标的相机上下文；没有时为空
	 * @param BaseValue - 修改器作用的参数的基础值 P₀
	 */
	virtual float Evaluate(const UGinkgoCameraContextComponent* Context, float BaseValue) const PURE_VIRTUAL(UGinkgoCameraValue::Evaluate, return 0.f;);
};

/** @class UGinkgoCameraValueConstant : 常量 */
UCLASS(meta = (DisplayName = "常量"))
class CASEGINKGO_API UGinkgoCameraValueConstant : public UGinkgoCameraValue
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value", meta = (DisplayName = "数值"))
	float Value = 0.f;

	virtual float Evaluate(const UGinkgoCameraContextComponent* Context, float BaseValue) const override { return Value; }
};

/** @class UGinkgoCameraValueScalarCurve : 上下文的一个连续量经曲线映射（例如钩锁速度 → 臂长） */
UCLASS(meta = (DisplayName = "连续量经曲线"))
class CASEGINKGO_API UGinkgoCameraValueScalarCurve : public UGinkgoCameraValue
{
	GENERATED_BODY()

public:
	/** 横轴：上下文里的这个连续量（没有时当作 0） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value", meta = (DisplayName = "连续量"))
	FGameplayTag Scalar;

	/** 横轴是连续量，纵轴是修改器的值 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value", meta = (DisplayName = "映射曲线"))
	FRuntimeFloatCurve Curve;

	virtual float Evaluate(const UGinkgoCameraContextComponent* Context, float BaseValue) const override;
};

/**
 * @class UGinkgoCameraValueZoomedFov : 基础视场按上下文里的一个倍率放大（Ginkgo::Camera::ZoomedFieldOfView）
 * 开镜的倍率是枪的属性，武器写进上下文，相机不认识武器
 */
UCLASS(meta = (DisplayName = "按倍率放大的视场"))
class CASEGINKGO_API UGinkgoCameraValueZoomedFov : public UGinkgoCameraValue
{
	GENERATED_BODY()

public:
	/** 倍率：上下文里的这个连续量；没有或不大于 1 时不放大 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Value", meta = (DisplayName = "倍率的连续量"))
	FGameplayTag ZoomScalar;

	virtual float Evaluate(const UGinkgoCameraContextComponent* Context, float BaseValue) const override;
};

/** @enum EGinkgoCameraBlend : 修改器怎么作用到参数上 */
UENUM(BlueprintType)
enum class EGinkgoCameraBlend : uint8
{
	/** 增量：P += (1 − 屏蔽) × 权重 × 值 */
	Delta		UMETA(DisplayName = "增量"),
	/** 覆盖：P = P + 权重 × (值 − P)，按优先级从低到高逐层插值 */
	Override	UMETA(DisplayName = "覆盖"),
};

/** @enum EGinkgoCameraBlendCurve : 盖在弹簧之后的覆盖，权重沿什么曲线走（Ginkgo::Camera::BlendCurveValue，k 是强度） */
UENUM(BlueprintType)
enum class EGinkgoCameraBlendCurve : uint8
{
	Linear		UMETA(DisplayName = "线性"),
	/** 两头缓：p^k / (p^k + (1 − p)^k) */
	Smooth		UMETA(DisplayName = "两头缓"),
	/** 先快后慢：1 − (1 − p)^k。按下马上有反应，到位时收住 */
	EaseOut		UMETA(DisplayName = "先快后慢"),
	/** 先慢后快：p^k */
	EaseIn		UMETA(DisplayName = "先慢后快"),
};

/**
 * @struct FGinkgoCameraParamModifier : 参数层的一个修改器
 * 四块：激活条件（标签与连续量）→ 作用目标（参数、增量或覆盖、类别）→ 数值来源（多态）→ 混合（优先级、淡入淡出、屏蔽）。
 * 修改器只给出贡献，不直接改参数；混合规则只有一份，在 FGinkgoCameraParamLayer 里
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraParamModifier
{
	GENERATED_BODY()

	/** 名字：调试面板、回放轨迹与 ginkgo.camera.set 用（Modifiers[名字].字段） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier", meta = (DisplayName = "名字"))
	FName Name;

	/** 开关：关掉时权重的目标是 0，照常淡出（对照用） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifier", meta = (DisplayName = "启用"))
	bool bEnabled = true;

	// ===== 激活条件 =====

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (DisplayName = "激活条件"))
	FGinkgoCameraActivation Activation;

	/**
	 * 条件不成立一次就锁住：标签查询还成立（同一次滑铲）时，连续量的条件再成立也不再激活，直到标签查询不成立。
	 * 滑铲 FOV 用它："加速后生效，直到低于跑速后失效"，下坡又快起来也不再加
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Activation", meta = (DisplayName = "失效后锁住"))
	bool bLatchOff = false;

	// ===== 作用目标 =====

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target", meta = (DisplayName = "参数"))
	FGinkgoCameraParamField Param;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target", meta = (DisplayName = "作用方式"))
	EGinkgoCameraBlend Blend = EGinkgoCameraBlend::Delta;

	/** 增量的类别：覆盖可以按类别屏蔽（开镜屏蔽速度类） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target", meta = (DisplayName = "增量类别", Categories = "Camera.Category", EditCondition = "Blend == EGinkgoCameraBlend::Delta"))
	FGameplayTag Category;

	/** 覆盖生效时屏蔽哪些类别的增量，屏蔽的程度就是这个覆盖的权重 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Target", meta = (DisplayName = "屏蔽的增量类别", Categories = "Camera.Category", EditCondition = "Blend == EGinkgoCameraBlend::Override"))
	FGameplayTagContainer BlockedCategories;

	// ===== 数值来源 =====

	/** 增量的值或覆盖成的值；空着时为 0 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Value", meta = (DisplayName = "数值来源"))
	TObjectPtr<UGinkgoCameraValue> Value;

	// ===== 混合 =====

	/** 覆盖的优先级：从低到高逐层插值，高的盖住低的 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend", meta = (DisplayName = "覆盖优先级", EditCondition = "Blend == EGinkgoCameraBlend::Override"))
	int32 Priority = 0;

	/** 淡入：权重从 0 到 1 的时间（线性，之后还有参数弹簧；盖在弹簧之后的沿淡入曲线走）；0 为一下子到 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend", meta = (DisplayName = "淡入时间", ClampMin = 0, Units = "s"))
	float BlendInTime = 0.15f;

	/** 淡出：权重从 1 到 0 的时间 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend", meta = (DisplayName = "淡出时间", ClampMin = 0, Units = "s"))
	float BlendOutTime = 0.4f;

	/**
	 * 合成在参数弹簧之后（仅对覆盖模式生效）：P = P + 权重 × (值 − P)，权重沿下方指定的缓动曲线演进。
	 * 淡入淡出时长即精确到位时长，不受该参数弹簧二次平滑拖尾影响；用于越肩开镜视场覆盖。按权重屏蔽增量的规则保持不变
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend", meta = (DisplayName = "盖在弹簧之后", EditCondition = "Blend == EGinkgoCameraBlend::Override"))
	bool bAfterSprings = false;

	/** 淡入的曲线（只有盖在弹簧之后的有：别的权重要分段线性，参数弹簧才能精确地拆开推进） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend", meta = (DisplayName = "淡入曲线", EditCondition = "bAfterSprings"))
	EGinkgoCameraBlendCurve BlendInCurve = EGinkgoCameraBlendCurve::Linear;

	/** 淡出的曲线 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend", meta = (DisplayName = "淡出曲线", EditCondition = "bAfterSprings"))
	EGinkgoCameraBlendCurve BlendOutCurve = EGinkgoCameraBlendCurve::Linear;

	/** 曲线的强度：1 就是线性，越大弯得越厉害 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Blend", meta = (DisplayName = "曲线强度", ClampMin = 1, EditCondition = "bAfterSprings"))
	float BlendCurveStrength = 2.f;

	/** @brief 合成在参数弹簧之后（只有覆盖可以） */
	bool IsAfterSprings() const { return Blend == EGinkgoCameraBlend::Override && bAfterSprings; }
};

/**
 * @struct FGinkgoCameraParamSpring : 一个参数的弹簧
 * 覆盖与增量合成以后的参数再过临界阻尼弹簧：权重的淡入淡出是线性的，弹簧把两头的折角磨圆。没列出的参数不平滑
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraParamSpring
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spring", meta = (DisplayName = "参数"))
	FGinkgoCameraParamField Param;

	/** 角频率：越大跟得越紧；临界阻尼的阶跃 0.32 s 到 90% 时约 12。≤ 0 不平滑 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spring", meta = (DisplayName = "角频率", ClampMin = 0, Units = "rad/s"))
	float Omega = 10.f;
};

/**
 * @enum EGinkgoCameraOverlayPivot : 叠加层的旋转绕哪里转（相机文档待定问题 1）
 */
UENUM(BlueprintType)
enum class EGinkgoCameraOverlayPivot : uint8
{
	/** 绕相机自身：相机的位置不动，只转画面；整个画面（连同角色）一起晃，像手持的相机被撞了一下。碰撞之后不挪位置，不会穿墙 */
	Camera	UMETA(DisplayName = "绕相机自身"),
	/** 绕跟随点：相机沿以跟随点为心的球面挪，角色在画面里几乎不动，只有背景晃；挪的距离是臂长 × 角度，碰撞之后挪的，贴墙时可能进墙 */
	Pivot	UMETA(DisplayName = "绕跟随点"),
};

/**
 * @struct FGinkgoCameraOverlayOutput : 一个叠加效果这一帧的输出：视角空间里的旋转（度）与 FOV（度）
 */
struct FGinkgoCameraOverlayOutput
{
	FRotator Rotation = FRotator::ZeroRotator;
	float FieldOfView = 0.f;

	FGinkgoCameraOverlayOutput& operator+=(const FGinkgoCameraOverlayOutput& Other)
	{
		Rotation += Other.Rotation;
		FieldOfView += Other.FieldOfView;
		return *this;
	}
};

/**
 * @struct FGinkgoCameraCarriedSpring : 事件带来的一次冲量的弹簧：ω、ζ 也是事件带来的（UGinkgoCameraCarriedShake）
 * 到峰值之前是踢出去的弹簧，之后换成回弹的。峰值上速度是 0，换弹簧不跳；在峰值的时刻拆开推进，所以与帧率无关
 */
struct FGinkgoCameraCarriedSpring
{
	TGinkgoImpulseSpring<FVector> Rotation;
	TGinkgoImpulseSpring<double> FieldOfView;
	double Omega = 0.0;
	double Zeta = 1.0;
	double ReturnOmega = 0.0;
	double ReturnZeta = 1.0;
	/** 还有多久到峰值（s） */
	double TimeToPeak = 0.0;

	/** @brief 推进一段时间 */
	void Step(const double DeltaTime)
	{
		const double Kicking = FMath::Min(DeltaTime, TimeToPeak);
		if (Kicking > 0.0)
		{
			Rotation.Step(Kicking, Omega, Zeta);
			FieldOfView.Step(Kicking, Omega, Zeta);
			TimeToPeak -= Kicking;
		}
		if (DeltaTime > Kicking)
		{
			Rotation.Step(DeltaTime - Kicking, ReturnOmega, ReturnZeta);
			FieldOfView.Step(DeltaTime - Kicking, ReturnOmega, ReturnZeta);
		}
	}
};

/**
 * @struct FGinkgoCameraOverlayState : 一个叠加效果的运行时状态（玩家相机组件持有，配置资产里没有）
 * 几种效果共用一份：冲量弹簧用旋转与 FOV 的两个弹簧，噪声用包络的弹簧，事件带来的冲量用弹簧的列表
 */
struct FGinkgoCameraOverlayState
{
	/** 旋转的偏差与角速度：X 是 Pitch、Y 是 Yaw、Z 是 Roll（度、度/秒） */
	TGinkgoImpulseSpring<FVector> Rotation;
	TGinkgoImpulseSpring<double> FieldOfView;
	TGinkgoCriticalSpring<double> Envelope;
	/** 事件带来的冲量还在晃的弹簧：晃停了就删 */
	TArray<FGinkgoCameraCarriedSpring> Carried;
	/** 上一帧映射出的连续量（不乘激活）：这一帧里连续量从这里匀速变过去 */
	double LastEnvelopeTarget = 0.0;
	/** 切换之后还没推进过：第一次推进直接放到位 */
	bool bActive = false;
	/** 累计吃了几个事件（调试与回放：每次滑铲一次） */
	int32 ImpulseCount = 0;
	/** 上一次给冲量的事件的世界时间：冲量弹簧的 MinInterval 拿它比 */
	double LastImpulseTime = TNumericLimits<double>::Lowest();
	/** 这一帧的输出（调试与回放） */
	FGinkgoCameraOverlayOutput Output;
};

/**
 * @struct FGinkgoCameraOverlayStep : 叠加效果推进一帧要的输入
 */
struct FGinkgoCameraOverlayStep
{
	const UGinkgoCameraContextComponent* Context = nullptr;
	/** 这一帧从上下文取出的事件 */
	TConstArrayView<FGinkgoCameraEvent> Events;
	/** 帧长（s） */
	double DeltaTime = 0.0;
	/** 这一帧开始的世界时间：事件的时刻减它就是在帧里的位置 */
	double FrameStartTime = 0.0;
	/** 相机的时钟（这一帧末）：切换以来的时间，噪声是它的函数，帧率不同时同一个时刻取到同一个值 */
	double Clock = 0.0;
	/** 第一人称的进度（0 越肩，1 眼睛在瞄具的出瞳上）：各效果的输出按它从 1 混到自己的 FirstPersonScale */
	float FirstPerson = 0.f;
};

/**
 * @class UGinkgoCameraOverlayEffect : 叠加层的一个效果
 * 碰撞之后只改旋转与 FOV（Docs/Design/CameraPipeline.md「叠加层与后坐力分工」）。有状态（弹簧的值与速度），状态由玩家相机组件持有，这里只有参数。
 * 子类：冲量弹簧（事件给冲量）、事件带来的冲量（参数也在事件上）、噪声 × 包络（持续的晃动）。新增能力只加一个小类，管线不动
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class CASEGINKGO_API UGinkgoCameraOverlayEffect : public UObject
{
	GENERATED_BODY()

public:
	/** 名字：调试面板、回放轨迹与 ginkgo.camera.set 用（OverlayEffects[名字].字段） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlay", meta = (DisplayName = "名字"))
	FName Name;

	/** 开关：关掉时输出 0、状态清空（对照用） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlay", meta = (DisplayName = "启用"))
	bool bEnabled = true;

	/** 激活条件（这一帧相机读到的上下文）：噪声的包络只在成立时追目标，冲量只在成立时收事件。空的总是成立 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlay", meta = (DisplayName = "激活条件"))
	FGinkgoCameraActivation Activation;

	/**
	 * 第一人称开镜（Docs/Design/Weapons.md「第一人称开镜」）时在相机画面上保留的输出比例：随第一人称过渡进度从 1 线性混合至该值。
	 * 例如开镜呼吸晃动设为 0（改由枪身与瞄具实体摆动），开火相机后坐保持为 1（维持与越肩一致的视野上跳冲击感）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlay", meta = (DisplayName = "第一人称时留多少", ClampMin = 0, ClampMax = 1))
	float FirstPersonScale = 1.f;

	/**
	 * 第一人称开镜时令武器与瞄具光轴同步跟随本层旋转偏移（瞄具镜筒、十字分划与画面一同跳动，弹道对齐分划中心）。
	 * 武器组件在同帧相机更新前按相同输入预演（AGinkgoPlayerCameraManager::PredictSightFollow），保证与相机输出严格一致
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Overlay", meta = (DisplayName = "第一人称时枪跟着转"))
	bool bSightFollows = false;

	/** @brief 激活条件成立没有 */
	bool IsActive(const UGinkgoCameraContextComponent* Context) const;

	/**
	 * @brief 推进一帧，给出这一帧末的输出
	 * @param State - 这个效果的状态；bActive 为假时（切换之后）从静止开始
	 */
	virtual FGinkgoCameraOverlayOutput Step(FGinkgoCameraOverlayState& State, const FGinkgoCameraOverlayStep& Input) const
		PURE_VIRTUAL(UGinkgoCameraOverlayEffect::Step, return FGinkgoCameraOverlayOutput(););
};

/**
 * @class UGinkgoCameraImpulseShake : 冲量弹簧
 * 事件给角速度一个冲量，欠阻尼弹簧把它晃回 0（静止点）。冲量每次一样（固定的，不随机）：同一个事件看上去每次相同。
 * 一帧里的事件按时刻拆开推进：先推进到事件的时刻、加冲量、再推进剩下的时间，结果只取决于事件的时刻，与帧率无关。
 * 振荡频率 ω√(1 − ζ²)/2π 宜在 20 Hz 以下：30 fps 下超过 15 Hz 会混叠
 */
UCLASS(meta = (DisplayName = "冲量弹簧"))
class CASEGINKGO_API UGinkgoCameraImpulseShake : public UGinkgoCameraOverlayEffect
{
	GENERATED_BODY()

public:
	/** 哪个事件给冲量（相机上下文的事件，如 Movement.Event.SlideStarted） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "触发事件"))
	FGameplayTag Event;

	/** 事件的强度（绝对值）低于它时不给冲量；0 为都给 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "最小强度", ClampMin = 0))
	float MinMagnitude = 0.f;

	/** 冲量：一下子加到角速度上（度/秒）。Pitch 为正是抬头，Yaw 为正是向右，Roll 为正是向右倾。峰值约为 冲量 / ω_d × e^(−ζωt) 的那一下 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "冲量"))
	FRotator Impulse = FRotator::ZeroRotator;

	/** 事件的强度为负时偏航、侧倾的冲量反过来：左右脚的落脚（左脚 +1、右脚 −1）各往一边晃 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "负强度时左右镜像"))
	bool bMirrorOnNegative = false;

	/** 两次冲量至少隔这么久（按事件的时刻，与帧率无关），近于它的事件不收。0 为都收。冲刺落脚：滑铲起身时两只脚 20 ms 内先后着地，只算一下 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "最小间隔", ClampMin = 0, Units = "s"))
	float MinInterval = 0.f;

	/** FOV 的冲量（度/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "视场冲量"))
	float FieldOfViewImpulse = 0.f;

	/** 无阻尼时的角频率（rad/s）：越大晃得越快、越硬 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "角频率", ClampMin = 0, Units = "rad/s"))
	float Omega = 40.f;

	/** 阻尼比：0 一直晃，越接近 1 越快停下 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "阻尼比", ClampMin = 0, ClampMax = 1))
	float Zeta = 0.3f;

	virtual FGinkgoCameraOverlayOutput Step(FGinkgoCameraOverlayState& State, const FGinkgoCameraOverlayStep& Input) const override;
};

/**
 * @class UGinkgoCameraCarriedShake : 事件带来的冲量
 * 冲量与弹簧的参数不在这里，在事件上（FGinkgoCameraEvent::Impulses）：开枪的晃动每把枪不一样、每枪有随机，参数在武器配置里（FGinkgoCameraRecoil），
 * 枪算好了带过来，相机不认识武器。这里只定收哪个事件、整体乘多少。
 * 每个冲量一个弹簧，各晃各的（线性，加起来就是叠加）：连开时自然累积；换枪时上一把的晃动照它自己的弹簧晃完；晃停了就删。
 * 踢出去与回弹可以是两组 ω、ζ，到峰值换（FGinkgoCameraCarriedSpring）。
 * 冲量加在事件的时刻，结果与帧率无关（同冲量弹簧）
 */
UCLASS(meta = (DisplayName = "事件带来的冲量"))
class CASEGINKGO_API UGinkgoCameraCarriedShake : public UGinkgoCameraOverlayEffect
{
	GENERATED_BODY()

public:
	/** 收哪个事件的冲量（相机上下文的事件，如 Weapon.Event.Fire） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "触发事件"))
	FGameplayTag Event;

	/** 整体乘多少：1 就是事件带来的（枪上写的）。只影响之后的冲量，正在晃的不变 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impulse", meta = (DisplayName = "整体倍数", ClampMin = 0))
	float Scale = 1.f;

	virtual FGinkgoCameraOverlayOutput Step(FGinkgoCameraOverlayState& State, const FGinkgoCameraOverlayStep& Input) const override;
};

/**
 * @struct FGinkgoCameraNoiseAxis : 噪声的一个轴
 * 三个正弦相加：主频率与它的 1.618、2.618 倍（无理数比，叠起来不重复），权重 0.6、0.3、0.1，所以最多偏幅度那么多
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoCameraNoiseAxis
{
	GENERATED_BODY()

	/** 幅度（度）：包络为 1 时最多偏这么多；0 为这个轴不晃 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (DisplayName = "幅度", ClampMin = 0, Units = "Degrees"))
	float Amplitude = 0.f;

	/** 主频率（Hz）：越低越像慢慢地晃，越高越像抖 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (DisplayName = "主频率", ClampMin = 0, Units = "Hz"))
	float Frequency = 0.5f;
};

/**
 * @class UGinkgoCameraNoiseSway : 噪声 × 包络
 * 持续的晃动（开镜）：噪声是相机时钟的函数（确定的，同一个时刻同一个值，帧率无关），包络由上下文定：
 * 激活条件不成立时目标是 0；成立时是一个连续量线性映射到 0–1（超出的外推，最后夹到 0–MaxEnvelope），没指定连续量时是 1。
 * 包络的目标再过临界阻尼弹簧（目标在帧里匀速变化），晃动渐起渐收
 */
UCLASS(meta = (DisplayName = "噪声 × 包络"))
class CASEGINKGO_API UGinkgoCameraNoiseSway : public UGinkgoCameraOverlayEffect
{
	GENERATED_BODY()

public:
	// ===== 包络 =====

	/** 包络跟着的连续量（水平速度、开镜进度）；空着时激活就是 1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Envelope", meta = (DisplayName = "包络的连续量"))
	FGameplayTag EnvelopeScalar;

	/** 连续量为它时包络是 0 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Envelope", meta = (DisplayName = "包络为 0 的值"))
	float ScalarForZero = 0.f;

	/** 连续量为它时包络是 1 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Envelope", meta = (DisplayName = "包络为 1 的值"))
	float ScalarForOne = 1.f;

	/** 包络的上限：比 1 大时更快的速度晃得更多（冲刺） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Envelope", meta = (DisplayName = "包络上限", ClampMin = 0))
	float MaxEnvelope = 1.f;

	/** 包络的平滑（临界阻尼的角频率）：越小晃动起得越慢；≤ 0 不平滑 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Envelope", meta = (DisplayName = "包络的角频率", ClampMin = 0, Units = "rad/s"))
	float EnvelopeOmega = 5.f;

	// ===== 噪声 =====

	/** 俯仰：为正是抬头 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (DisplayName = "俯仰"))
	FGinkgoCameraNoiseAxis Pitch;

	/** 偏航：为正是向右 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (DisplayName = "偏航"))
	FGinkgoCameraNoiseAxis Yaw;

	/** 侧倾：为正是向右倾 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (DisplayName = "侧倾"))
	FGinkgoCameraNoiseAxis Roll;

	/** 相位的种子：几个噪声效果、几个轴各自错开 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (DisplayName = "相位种子"))
	int32 Seed = 0;

	/**
	 * 第一人称时晃的是枪（开镜的晃动）：同一个噪声加到瞄准方向上（AGinkgoPlayerCameraManager::SampleSightSway），瞄具连同分划一起晃，
	 * 子弹跟着分划走；画面的那一份按 FirstPersonScale 收掉。越肩时照旧只晃画面（开镜时子弹打画面的中心，也跟着晃）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Noise", meta = (DisplayName = "第一人称时晃枪"))
	bool bMovesSight = false;

	/** @brief 连续量映射成包络、夹到 0–MaxEnvelope；没指定连续量时是 1 */
	double MapEnvelopeScalar(const UGinkgoCameraContextComponent* Context) const;

	/** @brief 包络的目标：激活条件不成立时 0，成立时按连续量映射 */
	double EvaluateEnvelopeTarget(const UGinkgoCameraContextComponent* Context) const;

	/**
	 * @brief 一个轴在某个时刻的噪声（度），包络为 1 时
	 * @param Axis - 0 俯仰、1 偏航、2 侧倾：各轴的相位不同
	 */
	double EvaluateNoise(int32 Axis, double Time) const;

	virtual FGinkgoCameraOverlayOutput Step(FGinkgoCameraOverlayState& State, const FGinkgoCameraOverlayStep& Input) const override;
};
