#include "GinkgoPlayerCameraManager.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#if !UE_BUILD_SHIPPING
AGinkgoPlayerCameraManager::FOnDebugView AGinkgoPlayerCameraManager::OnDebugView;
#endif

const UGinkgoPlayerCameraComponent& AGinkgoPlayerCameraManager::GetPlayerCamera() const
{
	if (LastCamera.IsValid())
	{
		return *LastCamera.Get();
	}
	const APawn* Pawn = PCOwner ? PCOwner->GetPawn() : nullptr;
	const auto* Component = Pawn ? Pawn->FindComponentByClass<UGinkgoPlayerCameraComponent>() : nullptr;
	return Component ? *Component : *GetDefault<UGinkgoPlayerCameraComponent>();
}

const UGinkgoCameraConfig& AGinkgoPlayerCameraManager::GetConfig() const
{
	return CameraConfig ? *CameraConfig : GetPlayerCamera().GetConfig();
}

FVector AGinkgoPlayerCameraManager::PredictViewLocation(const APawn& Pawn, const FRotator& ViewRotation) const
{
	return GetPlayerCamera().PredictViewLocation(Pawn, ViewRotation);
}

void AGinkgoPlayerCameraManager::RequestCameraCut()
{
	if (auto* Component = PCOwner && PCOwner->GetPawn() ? PCOwner->GetPawn()->FindComponentByClass<UGinkgoPlayerCameraComponent>() : LastCamera.Get())
	{
		Component->RequestCameraCut();
	}
}

void AGinkgoPlayerCameraManager::BlendTo(AActor& Target, const float Duration)
{
	// 上一帧可能正处于过渡中。锁住实际显示的 POV，中途折返也从屏幕上看到的位置开始。
	const FMinimalViewInfo Displayed = GetCameraCacheView();
	FViewTargetTransitionParams Transition;
	Transition.BlendTime = Duration;
	Transition.BlendFunction = VTBlend_Cubic;
	Transition.bLockOutgoing = true;
	SetViewTarget(&Target, Transition);
	if (PendingViewTarget.Target == &Target)
	{
		ViewTarget.POV = Displayed;
	}
}

void AGinkgoPlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, const float DeltaTime)
{
	auto* Camera = OutVT.Target ? OutVT.Target->FindComponentByClass<UGinkgoPlayerCameraComponent>() : nullptr;
	if (Camera && Camera->IsActive() && PCOwner)
	{
		if (LastCamera.IsValid() && LastCamera != Camera)
		{
			LastCamera->SuspendView();
		}
		LastCamera = Camera;
		Camera->EvaluateView(*PCOwner, DeltaTime, OutVT.POV, CameraConfig);
#if !UE_BUILD_SHIPPING
		if (&OutVT == &ViewTarget)
		{
			OnDebugView.Broadcast(*this, OutVT);
		}
#endif
		return;
	}
	const bool bPlayerIsOutgoing = PendingViewTarget.Target && !BlendParams.bLockOutgoing && LastCamera.IsValid()
		&& LastCamera->GetOwner() == ViewTarget.Target;
	const bool bPlayerIsIncoming = LastCamera.IsValid() && LastCamera->GetOwner() == PendingViewTarget.Target;
	if (LastCamera.IsValid() && !bPlayerIsOutgoing && !bPlayerIsIncoming)
	{
		LastCamera->SuspendView();
		LastCamera.Reset();
	}
	Super::UpdateViewTarget(OutVT, DeltaTime);
}
