#pragma once

#include "CoreMinimal.h"
#include "GinkgoMovementWindow.h"
#include "GinkgoLurchWindow.generated.h"

/**
 * @class UGinkgoLurchWindow : lurch 窗口
 * 不疲劳的起跳后一小段时间里，第一次按下键盘方向键时，把水平速度一下子转向新的输入方向（泰坦陨落 2 与 Apex 的 lurch）。
 * 强度 k = Strength × StrengthScale(窗口已过的时间)，按 Ginkgo::Movement::Lurch 转向，转完用掉窗口：每次起跳一次。
 *
 * 处理"方向键按下"的规则：
 * 1. 起跳那次移动里的按下不算（窗口已过的时间还是 0）：起跳在移动之前处理，同一次移动里的按下是起跳前就有的方向。
 *    联网时也要这样：起跳前那次移动丢了，服务器在起跳那次移动里才数到那次按下，这时窗口刚推入，两端都不转
 * 2. 没有方向（没按着方向键，或同一帧按下了反方向的两个键）、水平速度超过上限、强度为 0 时不转，窗口留着
 * 3. 方向取按着的方向键合起来的方向，同一条轴上两个反方向的键都按着时后按的那个赢（只给 lurch 用的 SOCD，移动输入仍是相加）：
 *    换方向时先按下新的键、后松开原来的键，按下那一刻新的键就算数。两端用同一份打包的按键与压过精度的视角算，逐位相同
 *
 * 与 Apex 的不同：Apex 一次起跳里可以按多次（Lurch Stack，滚轮连按就是 tap strafe），速度越快一次转得越少；
 * 这里每次起跳一次，转过的角度只看强度，与速度大小无关。
 */
UCLASS(meta = (DisplayName = "lurch 窗口"))
class CASEGINKGO_API UGinkgoLurchWindow : public UGinkgoMovementWindow
{
	GENERATED_BODY()

public:
	/** 满强度 α（0–1）：1 时速度大小不变、方向完全换过去；越小转得越少，夹角大时掉速越多。0.8 时向侧面按（90°）转过约 76°，剩约 82% 的速度 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Lurch", meta = (ClampMin = 0, ClampMax = 1))
	float Strength = 0.8f;

	/** 强度随窗口已过时间的倍率（0–1），乘在满强度上；空着时为 1。类默认值：0.2 s 内为 1，之后线性降到 0.4 s 时为 0（Apex 的社区实测） */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Window|Lurch")
	TObjectPtr<UGinkgoWindowValue> StrengthScale;

	/** 水平速度高于它时不起作用（Apex 的社区实测在 1150 到 1300 u/s 之间，取下限，乘 2.54） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Window|Lurch", meta = (ClampMin = 0, Units = "cm/s"))
	float MaxSpeed = 2921.f;

	/**
	 * @brief 窗口已过 PhaseTime 时按下的强度 k
	 * @return 满强度乘倍率，夹在 0 到 1
	 */
	float GetStrengthAt(float PhaseTime, float Param) const;

	//~ Begin UGinkgoMovementWindow
	virtual FGinkgoWindowBehaviorResult ApplyBehavior(const FGinkgoWindowEvent& Event, float PhaseTime, float Param, FGinkgoWindowEventContext& Context) const override;
	//~ End UGinkgoMovementWindow
};
