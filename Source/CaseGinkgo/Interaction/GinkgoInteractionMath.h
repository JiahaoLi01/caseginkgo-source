#pragma once

#include "CoreMinimal.h"

/**
 * 交互里与世界无关的计算：距离、角度、滞回、选焦点。写成纯函数，测试直接调（CaseGinkgo.Interaction.*）。
 * 规则见 Docs/Design/Interaction.md「被交互组件」「交互组件（角色）」。
 */
namespace Ginkgo::Interaction
{
	/**
	 * @brief 胶囊的中轴线段：两个半球球心之间的那一段
	 * @param Center - 胶囊中心
	 * @param Up - 胶囊的上方（单位向量）
	 * @param HalfHeight - 半高（含半球）
	 * @param Radius - 半径
	 * @param OutBottom - 下面那个半球的球心
	 * @param OutTop - 上面那个半球的球心
	 */
	CASEGINKGO_API void GetCapsuleSegment(const FVector& Center, const FVector& Up, double HalfHeight, double Radius, FVector& OutBottom, FVector& OutTop);

	/**
	 * @brief 点到线段的距离
	 * @note 距离从目标量到角色胶囊的中轴线段：目标在胶囊的高度范围内时就是水平距离，地上的枪与桌上的枪不用分别调
	 */
	CASEGINKGO_API double DistanceToSegment(const FVector& Point, const FVector& SegmentStart, const FVector& SegmentEnd);

	/**
	 * @brief 交互距离：默认量到胶囊的中轴线段；bFromCenter 时量到胶囊的中心（线段的中点），以角色为中心的一个球
	 * @note 球心在中轴线段上，所以量到中心的总不比量到线段的近：同一个半径，球比胶囊形的范围小，上下都收
	 */
	CASEGINKGO_API double InteractDistance(const FVector& Point, const FVector& SegmentBottom, const FVector& SegmentTop, bool bFromCenter);

	/**
	 * @brief 离准星的角度（度）：视线的前方与视点到目标的夹角，0 到 180
	 * @note 目标就在视点上时方向不定，算 0
	 */
	CASEGINKGO_API double ViewAngleDegrees(const FVector& ViewLocation, const FVector& ViewForward, const FVector& Target);

	/**
	 * @brief 带滞回的距离判断：进来要不超过 Radius，已经在里面时到 Radius + Hysteresis 才算出去
	 * @note 服务器判断请求时也用它：当作已经在里面，多放的是容差
	 */
	CASEGINKGO_API bool IsWithinRange(double Distance, double Radius, bool bWasWithin, double Hysteresis);

	/** 选焦点时的一个候选 */
	struct FFocusCandidate
	{
		/** 离准星的角度（度） */
		double ViewAngle = 0.0;

		/** 能当焦点：在交互距离以内、角色的眼睛看得见 */
		bool bEligible = false;

		/** 上一帧是焦点 */
		bool bWasFocus = false;
	};

	/**
	 * @brief 选焦点：几个都在交互距离以内时交互哪个
	 * @param Candidates - 候选
	 * @param MaxAngle - 离准星超过这个角度（度）的不算
	 * @param SwitchMargin - 上一帧的焦点还合格时，新的角度要小这么多（度）以上才换，免得两件挨着的东西来回跳
	 * @return 焦点的下标；没有时为 INDEX_NONE
	 * @note 只看角度，不看能不能交互：玩家对着哪个就是哪个
	 */
	CASEGINKGO_API int32 SelectFocus(TConstArrayView<FFocusCandidate> Candidates, double MaxAngle, double SwitchMargin);
}
