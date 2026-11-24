#pragma once

#include "CoreMinimal.h"
#include "GinkgoToast.generated.h"

/** toast 的图标：页面 Content/UI/Toast/page.js 的 ICONS，名字同枚举 */
UENUM(BlueprintType)
enum class EGinkgoToastIcon : uint8
{
	Gameplay	UMETA(DisplayName = "玩法"),
	Tutorial	UMETA(DisplayName = "教程"),
	Saved		UMETA(DisplayName = "已存档"),
};

/** @struct FGinkgoToast : 一条 toast（Docs/Design/Control.md「Toast」）：一个图标、一个标题、一段内容 */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoToast
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	EGinkgoToastIcon Icon = EGinkgoToastIcon::Gameplay;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (MultiLine = true))
	FText Body;

	/** 完全出现以后停留多久；不大于 0 时用项目设置的 Hold。字多的教程可以给长一点 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI", meta = (ClampMin = 0, Units = "s"))
	float HoldSeconds = 0.f;
};

/** @struct FGinkgoToastTimings : 一条 toast 的几段时间（项目设置 → CaseGinkgo → UI → Toast） */
USTRUCT(BlueprintType)
struct CASEGINKGO_API FGinkgoToastTimings
{
	GENERATED_BODY()

	/** 出现的动画 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = 0, Units = "s"))
	float In = 0.28f;

	/** 完全出现以后停留多久 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = 0, Units = "s"))
	float Hold = 4.5f;

	/** 消失的动画 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = 0, Units = "s"))
	float Out = 0.22f;

	/** 这一条消失以后隔多久出下一条 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI", meta = (ClampMin = 0, Units = "s"))
	float Gap = 0.25f;
};

/**
 * @class FGinkgoToastQueue : toast 的队列，先进先出，一次显示一条
 * 纯逻辑，时间由外面给（FGinkgoToastHud 给的是暂停与开着设置界面时不走的时钟），测试 CaseGinkgo.UI.Toast。
 */
class CASEGINKGO_API FGinkgoToastQueue
{
public:
	enum class EStep : uint8
	{
		None,
		Show,	// 这一帧出现一条：GetCurrent
		Hide,	// 这一帧这一条消失
	};

	/** @brief 排到队尾；正在显示的那条不受影响 */
	void Push(const FGinkgoToast& Toast);

	/**
	 * @brief 时间走到 Now
	 * @param Now 秒
	 * @return 这一帧要做的事，一帧最多一件
	 */
	EStep Advance(double Now, const FGinkgoToastTimings& Timings);

	/** @return 正在显示的那条；没有时为空 */
	const FGinkgoToast* GetCurrent() const { return bShowing ? &Current : nullptr; }

	/** @return 还在排队的条数（不算正在显示的） */
	int32 GetNumPending() const { return Pending.Num(); }

private:
	TArray<FGinkgoToast> Pending;

	FGinkgoToast Current;

	bool bShowing = false;

	/** 显示着的时候是该消失的时刻，否则是最早能出下一条的时刻 */
	double NextStepTime = 0.0;
};
