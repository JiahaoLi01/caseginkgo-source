#include "GinkgoMovementWindow.h"

float UGinkgoWindowValueCurve::Evaluate(const float PhaseTime, const float Param) const
{
	const FRichCurve* RichCurve = Curve.GetRichCurveConst();
	return RichCurve && RichCurve->GetNumKeys() > 0 ? RichCurve->Eval(PhaseTime) : 1.f;
}
