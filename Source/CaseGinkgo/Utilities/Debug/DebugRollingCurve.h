#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class UCanvas;

/**
 * @class FDebugRollingCurve : 调试用的滚动曲线
 * 发行版不编译。按时间追加采样，只保留最近一段时间；画在 UCanvas 上，横轴是时间（最右是现在），纵轴从 0 起自动缩放，
 * 或者正负对称地缩放（有正有负的量）。移动的水平速度曲线（ginkgo.showMovementCurve）、相机叠加弹簧的 x 与 v 曲线（ginkgo.camera.curves）在用。
 */
class CASEGINKGO_API FDebugRollingCurve
{
public:
	/** 一条水平参考线，例如跑速；同时参与纵轴缩放 */
	struct FReference
	{
		float Value = 0.f;
		FColor Color = FColor::White;
		FString Label;
	};

	explicit FDebugRollingCurve(const double InTimeWindow = 6.0) : TimeWindow(InTimeWindow) {}

	/** @brief 追加一个采样，并丢掉超出时间窗口的旧采样 */
	void AddSample(double Time, float Value, FColor Color);

	void Reset() { Samples.Reset(); }

	/**
	 * @brief 画在 Canvas 的一个矩形里
	 * @param Canvas - 目标画布
	 * @param Origin - 矩形左上角（像素）
	 * @param Size - 矩形大小（像素）
	 * @param Title - 左上角的标题
	 * @param References - 参考线
	 * @param bSigned - 纵轴正负对称（0 在中间，刻度按 1、2、5 取整）；默认从 0 起（刻度取 50 的整数倍）
	 */
	void Draw(UCanvas& Canvas, const FVector2D& Origin, const FVector2D& Size, const FString& Title, const TArray<FReference>& References,
		bool bSigned = false) const;

private:
	struct FSample
	{
		double Time;
		float Value;
		FColor Color;
	};

	TArray<FSample> Samples;
	double TimeWindow;
};

#endif // !UE_BUILD_SHIPPING
