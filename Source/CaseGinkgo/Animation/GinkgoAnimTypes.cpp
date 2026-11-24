#include "GinkgoAnimTypes.h"

#include "Animation/AnimSequence.h"

UAnimSequence* FGinkgoDirectionalAnims::Get(const EGinkgoAnimDirection Direction) const
{
	UAnimSequence* Sequence = nullptr;
	switch (Direction)
	{
	case EGinkgoAnimDirection::B:
		Sequence = B;
		break;
	case EGinkgoAnimDirection::LL:
		Sequence = LL;
		break;
	case EGinkgoAnimDirection::LR:
		Sequence = LR;
		break;
	case EGinkgoAnimDirection::RL:
		Sequence = RL;
		break;
	case EGinkgoAnimDirection::RR:
		Sequence = RR;
		break;
	default:
		break;
	}
	return Sequence ? Sequence : F.Get();
}

const FGinkgoJumpDirectionAnims& FGinkgoJumpAnims::Get(const EGinkgoAnimDirection Direction) const
{
	switch (Direction)
	{
	case EGinkgoAnimDirection::B:
		return B;
	case EGinkgoAnimDirection::LL:
	case EGinkgoAnimDirection::LR:
		return LL;
	case EGinkgoAnimDirection::RL:
	case EGinkgoAnimDirection::RR:
		return RL;
	default:
		return F;
	}
}
