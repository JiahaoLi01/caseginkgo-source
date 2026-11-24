#include "UI/GinkgoToast.h"

void FGinkgoToastQueue::Push(const FGinkgoToast& Toast)
{
	Pending.Add(Toast);
}

FGinkgoToastQueue::EStep FGinkgoToastQueue::Advance(const double Now, const FGinkgoToastTimings& Timings)
{
	if (Now < NextStepTime)
	{
		return EStep::None;
	}
	if (bShowing)
	{
		bShowing = false;
		NextStepTime = Now + Timings.Out + Timings.Gap;
		return EStep::Hide;
	}
	if (Pending.IsEmpty())
	{
		return EStep::None;
	}
	Current = Pending[0];
	Pending.RemoveAt(0);
	bShowing = true;
	NextStepTime = Now + Timings.In + (Current.HoldSeconds > 0.f ? Current.HoldSeconds : Timings.Hold);
	return EStep::Show;
}
