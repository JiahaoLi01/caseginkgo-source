#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"  // FRuntimeFloatCurve：曲线倍率
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "GinkgoMovementWindow.generated.h"

/**
 * @enum EGinkgoWindowEvent : 推入、切换与结束窗口的事件
 * 模拟产生的事件两端在同一次移动里算出来；输入事件（方向键按下）由扩展的移动数据带到服务器，在移动开始时发出
 */
UENUM(BlueprintType)
enum class EGinkgoWindowEvent : uint8
{
	/** 起跳（DoJump）。数值：这次的跳跃高度倍率；标志：这次起跳疲劳（倍率小于 1） */
	Jumped			UMETA(DisplayName = "起跳"),
	/** 落地 */
	Landed			UMETA(DisplayName = "落地"),
	/** 没有起跳就离开了地面：走下平台边缘 */
	WalkedOffLedge	UMETA(DisplayName = "未起跳离地"),
	/** 开始滑铲。数值：这次的起步加速（cm/s）；标志：拿到了起步加速 */
	SlideStarted	UMETA(DisplayName = "滑铲开始"),
	/** 滑铲结束：松开蹲、速度降到结束速度以下、离开地面 */
	SlideEnded		UMETA(DisplayName = "滑铲结束"),
	/**
	 * 输入：键盘方向键按下（松开不算），在这次移动开始时发出，这次移动的加速度已经定好。数值：上次之后按了几次。
	 * 起跳之后发出：同一次移动里的起跳先处理（CheckJumpInput 在移动之前）
	 */
	DirectionPressed	UMETA(DisplayName = "方向键按下"),

	/** 翻越、低攀或高攀通过检测并进入 CMC 自定义移动；请求被拒绝时不发出。数值和标志保留默认值。 */
	TraversalStarted	UMETA(DisplayName = "攀爬开始"),
};

/** @enum EGinkgoWindowRepush : 已有同类窗口时再次推入 */
UENUM(BlueprintType)
enum class EGinkgoWindowRepush : uint8
{
	/** 从第一个阶段重新开始，参数换成新的 */
	Refresh		UMETA(DisplayName = "刷新"),
	/** 什么也不做 */
	Ignore		UMETA(DisplayName = "忽略"),
	/** 再推入一个独立计时的窗口，受容量限制 */
	Stack		UMETA(DisplayName = "叠加"),
};

/** @enum EGinkgoWindowEventFlagFilter : 推入时对事件标志的要求 */
UENUM(BlueprintType)
enum class EGinkgoWindowEventFlagFilter : uint8
{
	Any			UMETA(DisplayName = "不限"),
	Set			UMETA(DisplayName = "要求为真"),
	Clear		UMETA(DisplayName = "要求为假"),
};

/**
 * @struct FGinkgoWindowEvent : 一次窗口事件
 * 只在模拟里传递，不进资产，所以不是 USTRUCT
 */
struct FGinkgoWindowEvent
{
	EGinkgoWindowEvent Type = EGinkgoWindowEvent::Jumped;

	/** 数值参数，例如滑铲开始时的起步加速；推入的窗口把它记作自己的参数 */
	float Value = 0.f;

	/** 标志，例如滑铲开始时是否拿到了加速 */
	bool bFlag = false;
};

/**
 * @struct FGinkgoWindowEventContext : 行为处理事件时读写的移动快照
 * 窗口不直接改移动组件：行为改这里的速度，管理器处理完事件后由移动组件写回
 */
struct FGinkgoWindowEventContext
{
	/** 当前速度，行为可以改 */
	FVector Velocity = FVector::ZeroVector;

	/** 这次移动的输入加速度（世界方向），只读 */
	FVector Acceleration = FVector::ZeroVector;

	/**
	 * 按着的键盘方向键合起来的方向（世界、水平的单位向量），只读；同一条轴上两个反方向的键都按着时后按的那个赢，没按方向键时为零。
	 * 与 Acceleration 不同：移动输入把反方向的两个键相加抵消，换方向时先按新键、后松开原来的键，按下那一刻的加速度里没有新的方向
	 */
	FVector KeyDirection = FVector::ZeroVector;

	/** 处理这次事件之前的汇总标签，只读；管理器处理事件时填上 */
	const FGameplayTagContainer* Tags = nullptr;
};

/** @struct FGinkgoWindowBehaviorResult : 一个窗口处理事件的结果 */
struct FGinkgoWindowBehaviorResult
{
	/** 消费事件：优先级更低的窗口不再收到它。窗口的阶段切换与推入不受影响 */
	bool bConsumeEvent = false;

	/** 用掉这个窗口：处理完就结束，例如死滑扣回一次加速 */
	bool bEndWindow = false;
};

/** @struct FGinkgoWindowTransition : 阶段里由事件触发的切换 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoWindowTransition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window")
	EGinkgoWindowEvent Event = EGinkgoWindowEvent::Landed;

	/** 切到第几个阶段；-1 结束窗口 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (ClampMin = -1))
	int32 TargetPhase = INDEX_NONE;
};

/**
 * @struct FGinkgoWindowPhase : 窗口的一个阶段
 * 阶段之间由时长或事件切换；跳跃疲劳的开关就是一个不定长、等落地事件的阶段
 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoWindowPhase
{
	GENERATED_BODY()

	/** 阶段标签，一般是窗口标签的子标签（如 Movement.Window.JumpFatigue.Armed）；可以空着 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (Categories = "Movement.Window"))
	FGameplayTag Tag;

	/** 时长（秒）；0 为不定长，只由事件结束 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (ClampMin = 0, Units = "s"))
	float Duration = 0.f;

	/** 到了时长切到第几个阶段；-1 结束窗口。多出来的时间带进下一个阶段，整个窗口的时长不随帧率变 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (ClampMin = -1))
	int32 NextPhase = INDEX_NONE;

	/** 这个阶段里由事件触发的切换，从这个阶段的开头重新计时 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window")
	TArray<FGinkgoWindowTransition> Transitions;
};

/**
 * @class UGinkgoWindowValue : 窗口倍率的数值来源
 * 窗口里唯一多态的数值：常量、按阶段已过时间查曲线，以后可以加自定义计算。求值只依赖窗口自己的状态，两端算出来一样
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class CASEGINKGO_API UGinkgoWindowValue : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 求值
	 * @param PhaseTime - 当前阶段已过的时间（秒）
	 * @param Param - 窗口推入时带的参数
	 */
	virtual float Evaluate(float PhaseTime, float Param) const PURE_VIRTUAL(UGinkgoWindowValue::Evaluate, return 1.f;);
};

/** @class UGinkgoWindowValueConstant : 常量倍率 */
UCLASS(meta = (DisplayName = "常量"))
class CASEGINKGO_API UGinkgoWindowValueConstant : public UGinkgoWindowValue
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window")
	float Value = 1.f;

	virtual float Evaluate(const float PhaseTime, const float Param) const override { return Value; }
};

/** @class UGinkgoWindowValueCurve : 按当前阶段已过的时间查曲线；跳跃疲劳的回升就是一条曲线 */
UCLASS(meta = (DisplayName = "曲线"))
class CASEGINKGO_API UGinkgoWindowValueCurve : public UGinkgoWindowValue
{
	GENERATED_BODY()

public:
	/** 横轴是阶段已过的时间（秒），纵轴是倍率 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window")
	FRuntimeFloatCurve Curve;

	virtual float Evaluate(float PhaseTime, float Param) const override;
};

/** @struct FGinkgoWindowMultiplier : 一项倍率贡献 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoWindowMultiplier
{
	GENERATED_BODY()

	/** 类别（Movement.Window.Effect.*）：查询这个类别或它的父类别时计入；被任何活动窗口屏蔽时整项不计 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (Categories = "Movement.Window.Effect"))
	FGameplayTag Category;

	/** 只在第几个阶段起作用；-1 为所有阶段 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (ClampMin = -1))
	int32 Phase = INDEX_NONE;

	/** 数值来源；空着时为 1 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Window")
	TObjectPtr<UGinkgoWindowValue> Value;
};

/**
 * @class UGinkgoMovementWindow : 移动窗口的定义
 * 定义只是数据：推入它的事件、推入时清除的窗口、阶段与切换、标签、能力、倍率、屏蔽的类别。不带状态，状态在 FGinkgoMovementSimState::Windows，
 * 按下标对应移动配置里的定义列表。组合规则只在 FGinkgoMovementWindowManager 里写一份。
 * 只用数据就能写出的窗口（滑铲疲劳、土狼时间、跳跃疲劳）直接用这个类；要改速度的行为（死滑、lurch）派生子类重写 ApplyBehavior。
 *
 * 为什么不开放蓝图重写：窗口在客户端重放与服务器回放里每次移动都要求值，留在 C++ 里，两端执行同一份代码。
 */
UCLASS(EditInlineNew, DefaultToInstanced, CollapseCategories, meta = (DisplayName = "窗口"))
class CASEGINKGO_API UGinkgoMovementWindow : public UObject
{
	GENERATED_BODY()

public:
	/** 窗口标签（Movement.Window.*）：活动时进入汇总标签 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window", meta = (Categories = "Movement.Window"))
	FGameplayTag Tag;

	/** 显示名：窗口时间轴（ginkgo.showWindowTimeline）上这条轨道的名字；空着时用标签的最后一节。打包的游戏里没有标签的注释，所以单写一份 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window")
	FText DisplayName;

	/** 推入它的事件 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Push")
	TArray<EGinkgoWindowEvent> PushEvents;

	/** 推入时对事件标志的要求：死滑只在拿到起步加速的滑铲开始时推入 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Push")
	EGinkgoWindowEventFlagFilter PushFlag = EGinkgoWindowEventFlagFilter::Any;

	/** 推入条件：汇总标签要满足它；空着不限 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Push")
	FGameplayTagQuery PushQuery;

	/** 已有同类窗口时再次推入的处理 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Push")
	EGinkgoWindowRepush Repush = EGinkgoWindowRepush::Refresh;

	/**
	 * 清除：推入（含刷新）时结束这些标签的活动窗口，按窗口标签或当前阶段的标签做层级匹配。这次事件里推入的窗口不受影响，与定义的先后无关。
	 * 土狼时间清除跳跃疲劳：走下平台边缘就关掉疲劳开关，之后落地也不再有疲劳。与屏蔽类别不同，屏蔽只在这个窗口活动时不计
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Push", meta = (Categories = "Movement.Window"))
	FGameplayTagContainer ClearedWindows;

	/** 阶段，至少一个；推入时从第一个开始 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Phases")
	TArray<FGinkgoWindowPhase> Phases;

	/** 在任何阶段都会结束窗口的事件 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Phases")
	TArray<EGinkgoWindowEvent> EndEvents;

	/** 能力（Movement.Window.Ability.*）：活动时给出，多个窗口取或 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Contributions", meta = (Categories = "Movement.Window.Ability"))
	FGameplayTagContainer GrantedAbilities;

	/** 倍率：同一类别的贡献相乘 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Contributions")
	TArray<FGinkgoWindowMultiplier> Multipliers;

	/** 屏蔽的倍率类别：活动时这些类别（及子类别）的贡献都不计，不论来自哪个窗口。土狼时间屏蔽跳跃惩罚，它不必知道疲劳窗口存在 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Contributions", meta = (Categories = "Movement.Window.Effect"))
	FGameplayTagContainer BlockedCategories;

	/** 处理事件时的优先级：大的先处理，先处理的可以消费事件；相同时先推入的先处理 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Behavior")
	int32 Priority = 0;

	/**
	 * @brief 行为：处理一次事件，可以改上下文里的速度
	 * @param Event - 事件
	 * @param PhaseTime - 当前阶段已过的时间（秒）
	 * @param Param - 推入时带的参数
	 * @param Context - 可读写的移动快照
	 * @return 是否消费事件、是否用掉这个窗口
	 * @note 基类不处理任何事件，只用数据的窗口不用重写
	 */
	virtual FGinkgoWindowBehaviorResult ApplyBehavior(const FGinkgoWindowEvent& Event, float PhaseTime, float Param, FGinkgoWindowEventContext& Context) const
	{
		return {};
	}
};
