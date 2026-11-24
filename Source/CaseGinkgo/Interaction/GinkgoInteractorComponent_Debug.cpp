#include "Interaction/GinkgoInteractorComponent.h"

#if !UE_BUILD_SHIPPING

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShowInteraction(
		TEXT("ginkgo.showInteraction"),
		0,
		TEXT("显示交互：0 关，1 开。只画本地控制的角色。\n")
		TEXT("提示距离以内的每个目标：目标高度上两个圈（灰是提示距离，白是交互距离）；从角色胶囊的中轴线段到目标的连线，")
		TEXT("绿的是能当焦点（在交互距离以内、眼睛看得见），黄的是在交互距离以内但眼睛看不见，灰的是只在提示距离以内；焦点外面再套一个青色的圈。")
		TEXT("目标上方一行字：状态、提示的字、距离、离准星的角度，相机看不见时写\"挡住\"，不能交互时写原因。"));

	FColor StateColor(const EGinkgoInteractionState State)
	{
		switch (State)
		{
		case EGinkgoInteractionState::Ready:
			return FColor::Green;
		case EGinkgoInteractionState::Blocked:
			return FColor::Orange;
		case EGinkgoInteractionState::Hint:
			return FColor::White;
		case EGinkgoInteractionState::Hidden:
			return FColor(128, 128, 128);
		}
		return FColor::White;
	}

	/** 目标高度上的一个水平圈 */
	void DrawLevelCircle(const UWorld* World, const FVector& Center, const float Radius, const FColor Color, const float Thickness)
	{
		DrawDebugCircle(World, Center, Radius, 48, Color, false, -1.f, SDPG_World, Thickness, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
	}
}

void UGinkgoInteractorComponent::DrawDebug() const
{
	if (CVarShowInteraction.GetValueOnGameThread() <= 0)
	{
		return;
	}
	const UWorld* World = GetWorld();
	FVector Bottom, Top;
	GetCapsuleSegment(Bottom, Top);
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FCandidate& Candidate = Candidates[Index];
		const IGinkgoInteractable* Target = Candidate.Target.Get();
		if (!Target)
		{
			continue;
		}
		const FVector Location = Target->GetInteractionLocation();
		const FGinkgoInteractionRanges Ranges = Target->GetInteractionRanges();
		DrawLevelCircle(World, Location, Ranges.Prompt, FColor(128, 128, 128), 1.f);
		// 交互距离量到胶囊中心的（拾取的球）：以角色为中心画一个球，线也连到中心
		const FVector Measured = Ranges.bInteractFromCenter ? (Bottom + Top) * 0.5 : FMath::ClosestPointOnSegment(Location, Bottom, Top);
		if (Ranges.bInteractFromCenter)
		{
			DrawDebugSphere(World, Measured, Ranges.Interact, 24, FColor::White, false, -1.f, SDPG_World, 0.5f);
		}
		else
		{
			DrawLevelCircle(World, Location, Ranges.Interact, FColor::White, 1.5f);
		}
		if (Index == FocusIndex)
		{
			DrawLevelCircle(World, Location, 20.f, FColor::Cyan, 3.f);
		}

		const FColor LineColor = Candidate.bReachable ? FColor::Green : (Candidate.bInInteractRange ? FColor::Yellow : FColor(128, 128, 128));
		DrawDebugLine(World, Measured, Location, LineColor, false, -1.f, SDPG_Foreground, 1.5f);

		TStringBuilder<256> Text;
		Text.Appendf(TEXT("%s  %s  %.0f cm  %.1f°"), *StaticEnum<EGinkgoInteractionState>()->GetNameStringByValue(static_cast<int64>(Candidate.State)),
			*Candidate.Text.ToString(), Candidate.Distance, Candidate.ViewAngle);
		if (!Candidate.bPromptVisible)
		{
			Text.Append(TEXT("  挡住"));
		}
		if (!Candidate.Reason.IsEmpty())
		{
			Text.Appendf(TEXT("  %s"), *Candidate.Reason.ToString());
		}
		// 时长 0：只画这一帧（-1 是一直留着）
		DrawDebugString(World, Location + FVector(0.0, 0.0, 30.0), Text.ToString(), nullptr, StateColor(Candidate.State), 0.f, true, 1.f);
	}
}

#endif // !UE_BUILD_SHIPPING
