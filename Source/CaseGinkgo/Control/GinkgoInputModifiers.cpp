#include "GinkgoInputModifiers.h"

#include "Character/GinkgoCharacter.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "GinkgoInputSettingsSubsystem.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace
{
	/** 修改器要读的：设置与这一刻的开镜 */
	struct FLookContext
	{
		const FGinkgoInputSettings* Settings = nullptr;
		float AdsAlpha = 0.f;
		float AdsZoom = 1.f;
		/** 手上的枪的倍镜的倍率（第一人称开镜）；没有倍镜是 0 */
		float ScopeMagnification = 0.f;
	};

	const FGinkgoInputSettings& DefaultSettings()
	{
		static const FGinkgoInputSettings Defaults;
		return Defaults;
	}

	const FGinkgoInputSettings& ReadSettings(const UEnhancedPlayerInput* PlayerInput)
	{
		const UGinkgoInputSettingsSubsystem* Subsystem = PlayerInput ? UGinkgoInputSettingsSubsystem::Get(PlayerInput->GetOuterAPlayerController()) : nullptr;
		return Subsystem ? Subsystem->GetCurrent() : DefaultSettings();
	}

	/** 开镜的进度是上一帧走到的（持枪状态在 PlayerTick 里、输入处理之后才推进），差一帧，而且只影响 0.3 s 的过渡 */
	FLookContext ReadLookContext(const UEnhancedPlayerInput* PlayerInput)
	{
		FLookContext Context;
		Context.Settings = &ReadSettings(PlayerInput);
		const APlayerController* Controller = PlayerInput ? PlayerInput->GetOuterAPlayerController() : nullptr;
		const AGinkgoCharacter* Character = Controller ? Cast<AGinkgoCharacter>(Controller->GetPawn()) : nullptr;
		if (const UGinkgoWeaponComponent* Weapons = Character ? Character->GetWeapons() : nullptr)
		{
			Context.AdsAlpha = Weapons->GetHoldState().GetAdsAlpha();
			const UGinkgoWeaponDefinition* Weapon = Weapons->GetHeldWeapon();
			Context.AdsZoom = Weapon ? Weapon->AdsZoom : 1.f;
			const FGinkgoScope* Scope = Weapons->GetHeldScope();
			Context.ScopeMagnification = Scope ? Scope->Optics.Magnification : 0.f;
		}
		return Context;
	}
}

FInputActionValue UGinkgoInputModifierMouseLook::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, const float DeltaTime)
{
	const FLookContext Context = ReadLookContext(PlayerInput);
	const FVector2D Degrees = Ginkgo::Input::MouseLookDegrees(CurrentValue.Get<FVector2D>(), *Context.Settings, Context.AdsAlpha, Context.AdsZoom, DeltaTime,
		Context.ScopeMagnification);
	return FInputActionValue(CurrentValue.GetValueType(), FVector(Degrees.X, Degrees.Y, 0.0));
}

FInputActionValue UGinkgoInputModifierStickLook::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, const float DeltaTime)
{
	const FLookContext Context = ReadLookContext(PlayerInput);
	if (Context.Settings->bGamepadSwapSticks != bForSwappedSticks)
	{
		return FInputActionValue(CurrentValue.GetValueType(), FVector::ZeroVector);
	}
	const FVector2D Degrees = Ginkgo::Input::StickLookDegrees(CurrentValue.Get<FVector2D>(), *Context.Settings, Context.AdsAlpha, Context.AdsZoom, Ramp, DeltaTime,
		Context.ScopeMagnification);
	return FInputActionValue(CurrentValue.GetValueType(), FVector(Degrees.X, Degrees.Y, 0.0));
}

void UGinkgoInputModifierStickLook::ReceiveModifierReinstanced_Implementation(const UInputModifier* OldModifier)
{
	if (const UGinkgoInputModifierStickLook* Old = Cast<UGinkgoInputModifierStickLook>(OldModifier))
	{
		Ramp = Old->Ramp;
	}
}

FInputActionValue UGinkgoInputModifierWheelMouse::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, const float DeltaTime)
{
	const FVector2D Degrees = Ginkgo::Input::WheelMouseDegrees(CurrentValue.Get<FVector2D>(), ReadSettings(PlayerInput));
	return FInputActionValue(CurrentValue.GetValueType(), FVector(Degrees.X, Degrees.Y, 0.0));
}

FInputActionValue UGinkgoInputModifierWheelStick::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, const float DeltaTime)
{
	const FGinkgoInputSettings& Settings = ReadSettings(PlayerInput);
	if (Settings.bGamepadSwapSticks != bForSwappedSticks)
	{
		return FInputActionValue(CurrentValue.GetValueType(), FVector::ZeroVector);
	}
	const FVector2D Stick = Ginkgo::Input::WheelStickInput(CurrentValue.Get<FVector2D>(), Settings);
	return FInputActionValue(CurrentValue.GetValueType(), FVector(Stick.X, Stick.Y, 0.0));
}

FInputActionValue UGinkgoInputModifierStickMove::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, const float DeltaTime)
{
	const FGinkgoInputSettings& Settings = ReadSettings(PlayerInput);
	if (Settings.bGamepadSwapSticks != bForSwappedSticks)
	{
		return FInputActionValue(CurrentValue.GetValueType(), FVector::ZeroVector);
	}
	const FVector2D Move = Ginkgo::Input::StickMoveInput(CurrentValue.Get<FVector2D>(), Settings);
	return FInputActionValue(CurrentValue.GetValueType(), FVector(Move.X, Move.Y, 0.0));
}
