#pragma once

#include "CoreMinimal.h"

/**
 * @struct FGinkgoDirectionKeys : 键盘方向键的按住状态与按下的先后
 * lurch 的方向用：同一条轴上两个反方向的键都按着时，后按的那个赢（SOCD，磁轴键盘上叫 Snap Tap 或 Rappy Snappy）。
 * 移动输入不用它，仍是各个键相加、反方向的两个键一起按着就抵消（同 Source 与 Apex）。
 *
 * 为什么只给 lurch：换方向时先按下新的键、后松开原来的键，按下那一帧的移动输入里两个键抵消了，没有新的方向；
 * lurch 只在按下的那一次移动里看方向，于是转一半（按着 W 时）或者整个没了。移动输入在重叠那几毫秒里是零，不碍事。
 *
 * 只是数据，不碰输入系统：控制器每帧把四个键这一帧按下了没有、现在按着没有交进来，单独测试（CaseGinkgo.Control.DirectionKeys）。
 */
struct CASEGINKGO_API FGinkgoDirectionKeys
{
	/** 四个键的顺序：右、左、前、后（D、A、W、S），两两是一条轴上的正与负 */
	static constexpr int32 Count = 4;

	/**
	 * @brief 交进一帧的按键
	 * @param bPressed - 这一帧按下了没有
	 * @param bHeld - 现在按着没有；这一帧按下又松开（一帧里点按完）的也算按着
	 * @note 同一帧按下的几个键记同一个先后，谁也不比谁后
	 */
	void Update(TConstArrayView<bool> bPressed, TConstArrayView<bool> bHeld);

	/**
	 * @brief 按着的键合起来的朝向
	 * @return X 向右、Y 向前，各为 −1、0、1。一条轴上只有一个键按着时就是它；两个都按着时后按的那个赢，同一帧按下的抵消
	 */
	FIntPoint Resolve() const;

private:
	/** 每个键最近一次按下时的序号，越大越晚；没按过为 0 */
	uint32 PressOrder[Count] = {};

	/** 有键按下的帧数，作为按下的序号 */
	uint32 Sequence = 0;

	bool bHeldKeys[Count] = {};
};
