#include "Animation/GinkgoAnimInstance.h"

#include "Camera/GinkgoPlayerCameraManager.h"
#include "Character/GinkgoCharacter.h"
#include "Combat/GinkgoDamageableComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Control/GinkgoInputSettingsSubsystem.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"
#include "Weapon/GinkgoScope.h"

namespace
{
	/** 与子弹结算一致：首个阻挡 actor 持有尚存活的可伤害组件，才是能继续受伤的目标。 */
	bool IsDamageableHit(const FHitResult& Hit)
	{
		const AActor* Actor = Hit.bBlockingHit ? Hit.GetActor() : nullptr;
		const auto* Target = Actor ? Actor->FindComponentByClass<UGinkgoDamageableComponent>() : nullptr;
		return Target && !Target->IsDead();
	}

#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarPeek(TEXT("ginkgo.anim.peek"), 1, TEXT("自动探身：1 开，0 淡出回正，供 PIE 对照。"));

	TAutoConsoleVariable<int32> CVarPeekDebug(TEXT("ginkgo.anim.peek.debug"), 0,
		TEXT("显示未探身参考射线与权重：青色准星方向，黄色参考枪口至命中。负权重左倾、正权重右倾。"));
#endif
}

void UGinkgoAnimInstance::UpdatePeek(const AGinkgoCharacter& Character, const FRotator& View, const FRotator& AimRotation, const float DeltaSeconds)
{
	const auto* PC = Cast<APlayerController>(Character.GetController());
	const auto* Camera = PC ? Cast<AGinkgoPlayerCameraManager>(PC->PlayerCameraManager) : nullptr;
	const auto* Weapons = Character.GetWeapons();
	const bool bChanged = PeekWeapon.Get() != Snapshot.Hold.Weapon;
	if (Snapshot.Motion.bCut || bChanged)
	{
		PeekState.Reset();
		bPeekGunReady = false;
	}
	PeekWeapon = Snapshot.Hold.Weapon;
	const auto* Settings = UGinkgoInputSettingsSubsystem::Get(PC);
	static const FGinkgoInputSettings DefaultSettings;
	bool bEnabled = PeekSettings.bEnabled && (Settings ? Settings->GetCurrent().bAutoPeek : DefaultSettings.bAutoPeek);
#if !UE_BUILD_SHIPPING
	bEnabled &= CVarPeek.GetValueOnGameThread() != 0;
#endif
	// 工作台、换弹/拉栓、切枪和拾取的手部动作拥有上半身；让它们开始时平滑收回探身。
	bPeekProbeRequested = bEnabled && Character.IsLocallyControlled() && Camera && Weapons && Snapshot.bHasAimPoint
		&& Snapshot.Hold.Weapon && Snapshot.Hold.IsRaised() && Snapshot.bGunInHand && !Character.GetWorkbench()
		&& Weapons->IsShotStepClear() && Snapshot.PickupSettle >= 1.f
		&& !HandReach[0].IsSet() && !HandReach[1].IsSet() && FadingHandReach[0].IsEmpty() && FadingHandReach[1].IsEmpty();
	PeekErrorDegrees = 0.0;
	bool bValid = false;
	bool bCanEnter = true;
	if (bPeekProbeRequested && bPeekGunReady)
	{
		FVector Eye = Camera->PredictViewLocation(Character, View);
		FQuat ProbeView = AimRotation.Quaternion();
		const FTransform GunWorld = PeekGun * GetSkelMeshComponent()->GetComponentTransform();
		if (const FGinkgoScope* Scope = Weapons->GetHeldScope(); Scope && Snapshot.SightAlpha > 0.f)
		{
			// 第一人称以未探身瞄具的出瞳与光轴为判据，避免拿越肩相机位置计算镜内偏差。
			const Ginkgo::Scope::FSightFrame Sight = Ginkgo::Scope::MakeSightFrame(GunWorld, *Scope);
			Eye = FMath::Lerp(Eye, Sight.Eye, static_cast<double>(Snapshot.SightAlpha));
			ProbeView = FQuat::Slerp(ProbeView, Sight.Rotation, Snapshot.SightAlpha).GetNormalized();
		}
		FVector Hit;
		FHitResult ReferenceHit;
		bValid = Ginkgo::Animation::TracePeek(*GetWorld(), Character, Eye, ProbeView, GunWorld,
			Snapshot.MuzzleOffset, Snapshot.Hold.GetAdsAlpha() >= UGinkgoWeaponComponent::AdsShotAlpha,
			AimTraceDistance, Hit, PeekErrorDegrees, &ReferenceHit) && !IsDamageableHit(ReferenceHit);
		if (bValid)
		{
			// 和 HUD 同源的最终枪口射线，只抑制新探身；不能用它直接清空已让出射击空间的姿势。
			FVector ActualMuzzle;
			FVector ActualPoint;
			FHitResult ActualHit;
			bool bHit = false;
			bCanEnter = !(Weapons->TraceShot(ActualMuzzle, ActualPoint, bHit, &ActualHit) && bHit && IsDamageableHit(ActualHit));
		}
#if !UE_BUILD_SHIPPING
		if (CVarPeekDebug.GetValueOnGameThread() != 0)
		{
			DrawDebugLine(GetWorld(), Eye, Eye + ProbeView.GetForwardVector() * 350.f, FColor::Cyan, false, 0.f, 0, 1.f);
			DrawDebugLine(GetWorld(), GunWorld.TransformPosition(Snapshot.MuzzleOffset), Hit, FColor::Yellow, false, 0.f, 0, 1.f);
			DrawDebugString(GetWorld(), Character.GetActorLocation() + FVector(0, 0, 120),
				FString::Printf(TEXT("Peek ref %.2f deg / weight %.2f"), PeekErrorDegrees, PeekState.Blend.Value), nullptr, FColor::White, 0.f);
		}
#endif
	}
	bPeekGunReady = false;
	if (!PC)
	{
		// 战术探身复用同一叠加位置与弹簧，不受本地玩家辅助功能设置影响。
		// 请求来自未探出的掩体判定；最终枪口获得射界后继续保持，避免命中反馈造成左右/回正振荡。
		const bool bPoseAvailable = PeekSettings.bEnabled && Weapons && Snapshot.Hold.Weapon && Snapshot.Hold.IsRaised()
			&& Snapshot.bGunInHand && !Character.GetWorkbench() && Weapons->IsShotStepClear() && Snapshot.PickupSettle >= 1.f
			&& !HandReach[0].IsSet() && !HandReach[1].IsSet() && FadingHandReach[0].IsEmpty() && FadingHandReach[1].IsEmpty();
		PeekState.Blend.Step(bPoseAvailable ? Character.GetTacticalPeek() : 0.f, PeekSettings.BlendOmega, DeltaSeconds);
		PeekState.Blend.Value = FMath::Clamp(PeekState.Blend.Value, -1.0, 1.0);
	}
	else
	{
		PeekState.Step(bValid, PeekErrorDegrees, DeltaSeconds, PeekSettings, bCanEnter);
	}
	PeekLeftAlpha = static_cast<float>(FMath::Max(0.0, -PeekState.Blend.Value));
	PeekRightAlpha = static_cast<float>(FMath::Max(0.0, PeekState.Blend.Value));
}
