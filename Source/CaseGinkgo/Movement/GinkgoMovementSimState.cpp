#include "GinkgoMovementSimState.h"

namespace
{
	/** 窗口个数占的位数：容量 8 要能写下 0 到 8 */
	constexpr uint32 WindowCountBits = 4;

	static_assert(FGinkgoMovementSimState::MaxWindows < (1 << WindowCountBits), "窗口个数的位数不够写下容量");

	/** 两份窗口列表的个数、定义与阶段都相同：这时才比较各自的计时与参数 */
	bool HaveSameWindows(const FGinkgoMovementSimState::FWindows& A, const FGinkgoMovementSimState::FWindows& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].Definition != B[Index].Definition || A[Index].Phase != B[Index].Phase)
			{
				return false;
			}
		}
		return true;
	}
}

void FGinkgoMovementSimState::NetSerialize(FArchive& Ar)
{
	Ar << TimeSinceLanded;
	// 读的时候只覆盖最低一位，其余位要先清零
	uint8 SlidingBit = !Ar.IsLoading() && bSliding ? 1 : 0;
	Ar.SerializeBits(&SlidingBit, 1);
	bSliding = SlidingBit != 0;
	if (bSliding)
	{
		Ar << SlideTime;
		Ar << SlideBoost;
	}
	else if (Ar.IsLoading())
	{
		SlideTime = 0.f;
		SlideBoost = 0.f;
	}

	uint8 WindowCount = Ar.IsLoading() ? 0 : static_cast<uint8>(Windows.Num());
	Ar.SerializeBits(&WindowCount, WindowCountBits);
	if (Ar.IsLoading())
	{
		// 坏数据不能撑破固定容量
		Windows.SetNum(FMath::Min<int32>(WindowCount, MaxWindows));
	}
	for (FGinkgoMovementWindowState& Window : Windows)
	{
		Ar << Window.Definition;
		Ar << Window.Phase;
		Ar << Window.PhaseTime;
		Ar << Window.Param;
	}
	Ar << HandledDirectionPressCount;
}

float FGinkgoMovementSimState::MaxTimeDifference(const FGinkgoMovementSimState& Other) const
{
	float Difference = FMath::Abs(TimeSinceLanded - Other.TimeSinceLanded);
	if (bSliding && Other.bSliding)
	{
		Difference = FMath::Max(Difference, FMath::Abs(SlideTime - Other.SlideTime));
	}
	if (HaveSameWindows(Windows, Other.Windows))
	{
		for (int32 Index = 0; Index < Windows.Num(); ++Index)
		{
			Difference = FMath::Max(Difference, FMath::Abs(Windows[Index].PhaseTime - Other.Windows[Index].PhaseTime));
		}
	}
	return Difference;
}

bool FGinkgoMovementSimState::NearlyEquals(const FGinkgoMovementSimState& Other, const float TimeTolerance, const float SpeedTolerance) const
{
	if (bSliding != Other.bSliding || !HaveSameWindows(Windows, Other.Windows) || HandledDirectionPressCount != Other.HandledDirectionPressCount
		|| MaxTimeDifference(Other) > TimeTolerance)
	{
		return false;
	}
	if (bSliding && FMath::Abs(SlideBoost - Other.SlideBoost) > SpeedTolerance)
	{
		return false;
	}
	for (int32 Index = 0; Index < Windows.Num(); ++Index)
	{
		if (FMath::Abs(Windows[Index].Param - Other.Windows[Index].Param) > SpeedTolerance)
		{
			return false;
		}
	}
	return true;
}

FString FGinkgoMovementSimState::ToString() const
{
	FString Text = bSliding
		? FString::Printf(TEXT("落地计时 %.6f，滑铲 %.6f s，起步加速 %.3f"), TimeSinceLanded, SlideTime, SlideBoost)
		: FString::Printf(TEXT("落地计时 %.6f，不在滑铲"), TimeSinceLanded);
	for (const FGinkgoMovementWindowState& Window : Windows)
	{
		Text += FString::Printf(TEXT("，窗口 %d 阶段 %d %.6f s 参数 %.3f"), Window.Definition, Window.Phase, Window.PhaseTime, Window.Param);
	}
	Text += FString::Printf(TEXT("，处理过的方向键按下 %d"), HandledDirectionPressCount);
	return Text;
}
