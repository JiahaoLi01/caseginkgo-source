#pragma once

#include "CoreMinimal.h"

/**
 * @struct FGinkgoMovementWindowState : 一个活动窗口的状态
 * 只有定义的下标、阶段、阶段已过时间与一个参数；定义在移动配置的窗口列表里，规则在 FGinkgoMovementWindowManager
 */
struct CASEGINKGO_API FGinkgoMovementWindowState
{
	/** 定义在窗口列表里的下标 */
	uint8 Definition = 0;

	/** 当前阶段 */
	uint8 Phase = 0;

	/** 当前阶段已过的时间（秒） */
	float PhaseTime = 0.f;

	/** 推入时带的参数，例如死滑要扣回的起步加速 */
	float Param = 0.f;
};

/**
 * @struct FGinkgoMovementSimState : 移动组件里由模拟产生的状态
 * 不由客户端上报：两端按同样的输入与 Δt 各自算出来，平时不用传。三处共用这一份结构：
 * 1. 存档：每次移动开始时记一份（FGinkgoSavedMove::SetInitialPosition）。客户端把上一次还没发出的移动并进这一次时，
 *    引擎只退回位置、速度、地面与跳跃计数，这里的状态要在 CombineWith 里放回，否则被合并那次移动的 Δt 会被多算一遍
 * 2. 校正：服务器在回包里整体下发（FGinkgoMoveResponseDataContainer），客户端放回后重放之后的移动
 * 3. 比对（调试）：ginkgo.net.verifySimState 打开时，服务器在确认好移动的回包里也带上它，客户端与存档里那次移动的结果比对
 * 窗口的状态也在这里，所以合并退回、校正下发、比对对窗口同样成立。
 */
struct CASEGINKGO_API FGinkgoMovementSimState
{
	/** 落地计时的上限：只需超过落地免摩擦窗口的上限（0.5 s），避免一直累加 */
	static constexpr float LandingTimeLimit = 1.f;

	/** 同时活动的窗口最多几个：容量固定，状态是值类型，网络读写用 4 位存个数 */
	static constexpr int32 MaxWindows = 8;

	using FWindows = TArray<FGinkgoMovementWindowState, TFixedAllocator<MaxWindows>>;

	/** 落地后经过的时间（秒），超过 LandingTimeLimit 后不再增长；决定落地免摩擦窗口 */
	float TimeSinceLanded = LandingTimeLimit;

	/** 正在滑铲（Walking 里的子状态） */
	bool bSliding = false;

	/** 这次滑铲已滑的时间（秒） */
	float SlideTime = 0.f;

	/** 这次滑铲的起步加速（cm/s），没拿到加速时为 0 */
	float SlideBoost = 0.f;

	/** 活动的窗口，按推入的先后排列 */
	FWindows Windows;

	/**
	 * 处理过的方向键按下计数：移动组件的计数（输入）与它不同时，在移动开始时发出"方向键按下"并记下。
	 * 放在模拟状态里，校正时随服务器的状态放回：服务器晚一次移动才收到按下时，客户端重放不会把那次按下再处理一遍
	 */
	uint8 HandledDirectionPressCount = 0;

	/**
	 * @brief 网络读写
	 * @note 浮点按原样写、不量化：量化后客户端放回的值与服务器差一点，时长边界可能落到不同的移动上，又引出一次校正。
	 *       不在滑铲时不写滑铲的两项，读出时清零；窗口先写 4 位个数，再逐个写下标、阶段与两个浮点；最后是处理过的按下计数（8 位）
	 */
	void NetSerialize(FArchive& Ar);

	/**
	 * @brief 两份状态里计时之差的最大值（秒），调试比对用
	 * @note 滑铲已滑时间只在两份都在滑铲时才比；窗口的阶段已过时间只在两份窗口的个数、定义与阶段都相同时才比
	 */
	float MaxTimeDifference(const FGinkgoMovementSimState& Other) const;

	/**
	 * @brief 两份状态是否一致，调试比对用
	 * @param TimeTolerance - 计时允许的差（秒）。两端的位置本来就有低于校正阈值的误差，落地发生在一次移动里的哪一刻，
	 *        两端会差出零点几毫秒；合并移动时多算的一次 Δt 至少是一帧（144 fps 约 7 ms），1 ms 能把两者分开
	 * @param SpeedTolerance - 起步加速与窗口参数允许的差
	 * @return 滑铲与否相同、窗口的个数、定义与阶段相同、处理过的按下计数相同，计时之差、起步加速与窗口参数之差都在容差内
	 */
	bool NearlyEquals(const FGinkgoMovementSimState& Other, float TimeTolerance, float SpeedTolerance) const;

	FString ToString() const;
};
