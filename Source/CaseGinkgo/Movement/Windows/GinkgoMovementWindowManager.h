#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GinkgoMovementWindow.h"             // FGinkgoWindowEvent、FGinkgoWindowEventContext
#include "Movement/GinkgoMovementSimState.h"  // FGinkgoMovementSimState::FWindows

/**
 * @struct FGinkgoMovementWindowManager : 窗口管理器
 * 移动组件的一个结构体成员，不是 UObject。窗口的状态在模拟状态里：两端各自推进，存档快照、合并退回、校正下发都跟着模拟状态走；
 * 定义在移动配置里。管理器只管规则：按每次移动的 Δt 推进，按事件推入与切换，组合各窗口的贡献，维护由活动窗口重建的汇总标签。
 * 组合规则只在这里写一份，窗口之间互不访问。
 *
 * 处理一次事件的顺序：
 * 1. 行为：按优先级交给各窗口（大的先，相同时先推入的先），可以改速度、消费事件、用掉自己
 * 2. 切换：各窗口按定义里的结束事件与当前阶段的切换表换阶段或结束
 * 3. 推入：推入事件对得上、事件标志与推入条件都满足的定义，按"再次推入"的规则推入。推入全部做完后清除：结束这次推入或刷新的窗口
 *    声明要清除的窗口，这次推入或刷新的不清；所以结果与定义的先后无关，推入条件看到的是清除之前的汇总标签
 * 先切换再推入，所以同一个事件既结束又推入同一种窗口时，结果是从头开始。
 *
 * 窗口列表是固定容量的数组，定义是 TConstArrayView，UEQuery 只接默认分配器的 TArray，这里用普通循环。
 */
struct CASEGINKGO_API FGinkgoMovementWindowManager
{
	/** 窗口定义：状态里的下标指向它 */
	using FDefinitions = TConstArrayView<TObjectPtr<UGinkgoMovementWindow>>;

	using FWindows = FGinkgoMovementSimState::FWindows;

	/** 一次窗口变化（推入、换阶段、结束），给表现层与调试 */
	struct FChange
	{
		/** 定义的下标 */
		int32 Definition = INDEX_NONE;

		/** 原来的阶段；推入时为 -1 */
		int32 FromPhase = INDEX_NONE;

		/** 新的阶段；结束时为 -1。再次推入刷新时为 0，原来的阶段在 FromPhase */
		int32 ToPhase = INDEX_NONE;
	};

	/**
	 * @brief 按这次移动的 Δt 推进所有窗口，到了时长的换阶段或结束
	 * @note 每次移动结束时调用一次（UpdateCharacterStateAfterMovement），这次移动里推入的窗口也算上这一整段。
	 *       多出来的时间带进下一个阶段；定义已经不在列表里的窗口直接结束
	 */
	void Advance(FDefinitions Definitions, FWindows& Windows, float DeltaTime);

	/**
	 * @brief 处理一次事件：行为、切换、推入
	 * @param Context - 行为可以改其中的速度，调用方处理完再写回移动组件；汇总标签由这里填上
	 * @return 事件是否被某个窗口的行为消费
	 */
	bool HandleEvent(FDefinitions Definitions, FWindows& Windows, const FGinkgoWindowEvent& Event, FGinkgoWindowEventContext& Context);

	/** @brief 能力：任何活动窗口给出这个能力（或它的子能力）就为真 */
	bool HasAbility(FDefinitions Definitions, const FWindows& Windows, const FGameplayTag& Ability) const;

	/**
	 * @brief 倍率：所有活动窗口在这个类别（含子类别）上的贡献相乘
	 * @note 被任何活动窗口屏蔽的类别不计；没有贡献时为 1
	 */
	float GetMultiplier(FDefinitions Definitions, const FWindows& Windows, const FGameplayTag& Category) const;

	/**
	 * @brief 从活动窗口重建汇总标签：窗口标签加当前阶段的标签
	 * @note 推进与处理事件后自动重建；状态被整体换掉（合并退回、校正放回）后由调用方重建
	 */
	void RebuildTags(FDefinitions Definitions, const FWindows& Windows);

	/** 汇总标签：每次移动重建的快照，推入条件、相机上下文、调试显示都读它 */
	const FGameplayTagContainer& GetTags() const { return Tags; }

	/** 窗口变化的累计次数，只增不减；存档在移动开始与结束时各记一次，不相同就不合并（同引擎对重叠事件的处理） */
	uint32 GetChangeCount() const { return ChangeCount; }

	/** 取走累计的变化 */
	TArray<FChange, TInlineAllocator<8>> TakeChanges();

private:
	/** 结束一个窗口，后面的窗口顺次前移，推入的先后不变 */
	void EndWindow(FWindows& Windows, int32 Index);

	/**
	 * @brief 清除：结束标签与 Cleared 对得上的活动窗口（窗口标签或当前阶段的标签，层级匹配）
	 * @param Fresh - 这次事件里推入或刷新的窗口，按下标的位掩码；它们不清，结束别的窗口时位跟着前移
	 */
	void ClearWindows(FDefinitions Definitions, FWindows& Windows, const FGameplayTagContainer& Cleared, uint32& Fresh);

	void RecordChange(int32 Definition, int32 FromPhase, int32 ToPhase);

	FGameplayTagContainer Tags;

	uint32 ChangeCount = 0;

	TArray<FChange, TInlineAllocator<8>> Changes;
};
