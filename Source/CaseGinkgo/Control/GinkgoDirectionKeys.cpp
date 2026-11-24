#include "GinkgoDirectionKeys.h"

void FGinkgoDirectionKeys::Update(const TConstArrayView<bool> bPressed, const TConstArrayView<bool> bHeld)
{
	check(bPressed.Num() == Count && bHeld.Num() == Count);
	if (bPressed.Contains(true))
	{
		++Sequence;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (bPressed[Index])
		{
			PressOrder[Index] = Sequence;
		}
		bHeldKeys[Index] = bHeld[Index];
	}
}

FIntPoint FGinkgoDirectionKeys::Resolve() const
{
	// 第 2 × Axis 个键是这条轴的正方向，下一个是负方向
	const auto ResolveAxis = [this](const int32 Axis)
	{
		const int32 Positive = Axis * 2;
		const int32 Negative = Positive + 1;
		if (bHeldKeys[Positive] && bHeldKeys[Negative])
		{
			return PressOrder[Positive] > PressOrder[Negative] ? 1 : (PressOrder[Negative] > PressOrder[Positive] ? -1 : 0);
		}
		return bHeldKeys[Positive] ? 1 : (bHeldKeys[Negative] ? -1 : 0);
	};
	return FIntPoint(ResolveAxis(0), ResolveAxis(1));
}
