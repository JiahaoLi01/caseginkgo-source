#include "GinkgoCameraOverlay.h"

#include "GinkgoCameraConfig.h"

FGinkgoCameraOverlayOutput FGinkgoCameraOverlayLayer::Step(const UGinkgoCameraConfig& Config, const FGinkgoCameraOverlayStep& Input, const bool bEnabled)
{
	const TArray<TObjectPtr<UGinkgoCameraOverlayEffect>>& Effects = Config.OverlayEffects;
	// 运行中增删了效果：状态对不上了，从静止开始
	if (States.Num() != Effects.Num())
	{
		States.Reset();
		States.SetNum(Effects.Num());
	}
	FGinkgoCameraOverlayOutput Total;
	for (int32 Index = 0; Index < Effects.Num(); ++Index)
	{
		FGinkgoCameraOverlayState& State = States[Index];
		const UGinkgoCameraOverlayEffect* Effect = Effects[Index];
		if (!bEnabled || !Effect || !Effect->bEnabled)
		{
			State = FGinkgoCameraOverlayState();
			continue;
		}
		State.Output = Effect->Step(State, Input);
		// 第一人称（眼睛贴着瞄具）时有的效果收掉：状态照常推进，只是输出乘这个比例，回到越肩时接得上
		if (Input.FirstPerson > 0.f && Effect->FirstPersonScale != 1.f)
		{
			const float Scale = FMath::Lerp(1.f, Effect->FirstPersonScale, FMath::Clamp(Input.FirstPerson, 0.f, 1.f));
			State.Output.Rotation *= Scale;
			State.Output.FieldOfView *= Scale;
		}
		Total += State.Output;
	}
	return Total;
}

FRotator FGinkgoCameraOverlayLayer::Compose(const FRotator& View, const FRotator& Overlay)
{
	if (Overlay.IsZero())
	{
		return View;
	}
	return (View.Quaternion() * Overlay.Quaternion()).Rotator();
}
