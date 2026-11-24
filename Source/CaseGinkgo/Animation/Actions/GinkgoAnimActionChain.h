#pragma once

#include "CoreMinimal.h"
#include "Animation/Actions/GinkgoAnimAction.h"
#include "GinkgoAnimActionChain.generated.h"

class UGinkgoAnimActionChain;

/** 链的一步怎么创建动作：进入这一步时才调，所以后一步能用前一步的决定 */
using FGinkgoAnimActionFactory = TFunction<UGinkgoAnimAction*()>;

/** @struct FGinkgoAnimActionChainTransition : 按结局与原因的转换 */
struct FGinkgoAnimActionChainTransition
{
	EGinkgoAnimActionOutcome Outcome = EGinkgoAnimActionOutcome::Completed;

	/** 空时这个结局都算；否则原因要是它或它的子标签 */
	FGameplayTag Reason;

	/** 去哪一步；NAME_None 时结束 */
	FName Target;
};

/** @struct FGinkgoAnimActionChainStep : 链的一步 */
struct FGinkgoAnimActionChainStep
{
	FName Name;

	/** 放蒙太奇的步：进入时创建动作 */
	FGinkgoAnimActionFactory Factory;

	/** 瞬时的步（Do）：不放蒙太奇，做完就算完成 */
	TFunction<void()> Instant;

	/** 完成时去哪一步；NAME_None 时结束 */
	FName Next;

	/** 中止时去哪一步；NAME_None 时结束 */
	FName OnAborted;

	/** 按结局与原因的转换，比 Next、OnAborted 优先 */
	TArray<FGinkgoAnimActionChainTransition> Rules;
};

/**
 * @struct FGinkgoAnimActionChainBuilder : 定义链的步骤与转换
 * 在 OnBuild 里用：Step 是放蒙太奇的步，Do 是瞬时的步；返回的 FStepBuilder 接着写这一步的转换
 */
struct CASEGINKGO_API FGinkgoAnimActionChainBuilder
{
	/** 一步的转换 */
	struct CASEGINKGO_API FStepBuilder
	{
		/** @brief 完成时去哪一步；不写就结束 */
		FStepBuilder& Next(FName Target);

		/** @brief 中止时去哪一步；不写就结束 */
		FStepBuilder& OnAborted(FName Target);

		/**
		 * @brief 按结局与原因去哪一步，比 Next、OnAborted 优先
		 * @param Outcome - 只能是完成或中止：取消总是结束
		 * @param Reason - 原因是它或它的子标签时才算；空时这个结局都算
		 * @param Target - NAME_None 时结束
		 */
		FStepBuilder& On(EGinkgoAnimActionOutcome Outcome, const FGameplayTag& Reason, FName Target);

	private:
		friend struct FGinkgoAnimActionChainBuilder;

		FStepBuilder(FGinkgoAnimActionChainBuilder& InBuilder, const int32 InIndex)
			: Builder(InBuilder), Index(InIndex)
		{
		}

		FGinkgoAnimActionChainBuilder& Builder;
		int32 Index;
	};

	explicit FGinkgoAnimActionChainBuilder(UGinkgoAnimActionChain& InChain)
		: Chain(InChain)
	{
	}

	/** @brief 放蒙太奇的一步 */
	FStepBuilder Step(FName Name, FGinkgoAnimActionFactory Factory);

	/** @brief 瞬时的一步：不放蒙太奇，做完就算完成（切枪的换层） */
	FStepBuilder Do(FName Name, TFunction<void()> Function);

private:
	UGinkgoAnimActionChain& Chain;

	FGinkgoAnimActionChainStep& GetStep(int32 Index) const;
};

/**
 * @class UGinkgoAnimActionChain : 动作链
 * 命名的步骤加转换表。动作只报结局，去哪由链定，所以同一个动作能用在几条链里；链也是动作，可以嵌套。
 * 见 Docs/Design/AnimActions.md「动作链」。
 *
 * 转换：完成去 Next，中止去 OnAborted，按结局与原因写的 On 优先，都没写就结束；取消总是结束。
 * 链的结局：路径上第一个没完成的步的结局与原因；都完成了就是完成。
 */
UCLASS()
class CASEGINKGO_API UGinkgoAnimActionChain : public UGinkgoAnimAction
{
	GENERATED_BODY()

public:
	using FBuildFunction = TFunction<void(FGinkgoAnimActionChainBuilder&)>;

	/**
	 * @brief 用 lambda 定义一条链
	 * @param Build - 定义步骤与转换，同覆写 OnBuild
	 */
	static UGinkgoAnimActionChain* MakeChain(UObject* Outer, FName Name, FBuildFunction Build);

	/** 当前这一步的动作；瞬时的步、链还没开始或结束后为空 */
	UGinkgoAnimAction* GetCurrentStep() const { return CurrentStep; }

	/** 当前这一步的名字 */
	FName GetCurrentStepName() const;

protected:
	/** @brief 定义步骤与转换；执行器开始这条链时调一次。默认调 MakeChain 给的 lambda */
	virtual void OnBuild(FGinkgoAnimActionChainBuilder& Builder);

	/** 链不自己放蒙太奇：执行器开始链时调 OnBuild，不调 OnStart */
	virtual void OnStart(FGinkgoAnimActionSetup& Setup) override final;

private:
	friend struct FGinkgoAnimActionRunner;
	friend struct FGinkgoAnimActionChainBuilder;

	FBuildFunction BuildFunction;
	TArray<FGinkgoAnimActionChainStep> Steps;
	int32 CurrentIndex = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<UGinkgoAnimAction> CurrentStep;

	/** 路径上第一个没完成的步的结局；都完成了就一直是未结束 */
	FGinkgoAnimActionResult FirstUnfinished;

	int32 FindStep(FName Name) const;

	/** @return 按这一步的结局去的下一步；INDEX_NONE 时结束 */
	int32 FindNext(int32 From, const FGinkgoAnimActionResult& StepResult) const;

	/** 步骤名不重复，转换里写的步骤都在；写错了打错误日志 */
	bool Validate() const;
};
