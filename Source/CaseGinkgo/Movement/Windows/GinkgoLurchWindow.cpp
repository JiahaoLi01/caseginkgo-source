#include "GinkgoLurchWindow.h"

#include "Movement/GinkgoMovementMath.h"

float UGinkgoLurchWindow::GetStrengthAt(const float PhaseTime, const float Param) const
{
	const float Scale = StrengthScale ? StrengthScale->Evaluate(PhaseTime, Param) : 1.f;
	return FMath::Clamp(Strength * Scale, 0.f, 1.f);
}

FGinkgoWindowBehaviorResult UGinkgoLurchWindow::ApplyBehavior(const FGinkgoWindowEvent& Event, const float PhaseTime, const float Param, FGinkgoWindowEventContext& Context) const
{
	// 起跳那次移动里推入的窗口，已过时间要到这次移动结束时才推进：这次移动里的按下属于起跳前
	if (Event.Type != EGinkgoWindowEvent::DirectionPressed || PhaseTime <= 0.f)
	{
		return {};
	}
	const FVector WishDir = Context.KeyDirection;
	const float LurchStrength = GetStrengthAt(PhaseTime, Param);
	if (WishDir.IsZero() || LurchStrength <= 0.f || Context.Velocity.SizeSquared2D() > FMath::Square(MaxSpeed))
	{
		return {};
	}
	Ginkgo::Movement::Lurch(Context.Velocity, WishDir, LurchStrength);
	// 这次按下用掉了窗口
	FGinkgoWindowBehaviorResult Result;
	Result.bConsumeEvent = true;
	Result.bEndWindow = true;
	return Result;
}
