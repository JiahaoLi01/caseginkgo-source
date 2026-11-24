#include "Weapon/GinkgoWeaponTypes.h"

#include "Camera/GinkgoCameraContextComponent.h"
#include "Camera/GinkgoSpring.h"
#include "Weapon/GinkgoRecoilPattern.h"

FVector2D FGinkgoPatternRecoil::GetScale(float AdsAlpha, float CrouchAlpha, float AttachmentOffset) const
{
	const double Pose = FMath::Lerp(1.0, double(AdsScale), double(AdsAlpha))
		* FMath::Lerp(1.0, double(CrouchScale), double(CrouchAlpha));
	const FVector2D Base = FVector2D(YawScale, PitchScale) * Pose;
	return FVector2D(FMath::Max(0.0, Base.X + AttachmentOffset), FMath::Max(0.0, Base.Y + AttachmentOffset));
}

float FGinkgoHoldState::GetRaiseAlpha() const
{
	return Ginkgo::Weapon::EaseProgress(RaiseProgress);
}

float FGinkgoHoldState::GetAdsAlpha() const
{
	return Ginkgo::Weapon::EaseProgress(AdsProgress);
}

float FGinkgoHoldState::GetSprintAlpha() const
{
	return Ginkgo::Weapon::EaseProgress(SprintProgress);
}

namespace Ginkgo::Weapon
{
	EGinkgoWeaponSlotKind GetSlotKind(const int32 Slot)
	{
		return Slot == SidearmSlot ? EGinkgoWeaponSlotKind::Sidearm : (Slot == HeavySlot ? EGinkgoWeaponSlotKind::Heavy : EGinkgoWeaponSlotKind::Primary);
	}

	bool CanStow(const EGinkgoWeaponSlotKind Kind)
	{
		return Kind != EGinkgoWeaponSlotKind::Heavy;
	}

	bool IsBackToBackSwap(const int32 FromSlot, const int32 ToSlot)
	{
		return FromSlot != ToSlot && FromSlot != INDEX_NONE && ToSlot != INDEX_NONE
			&& GetSlotKind(FromSlot) == EGinkgoWeaponSlotKind::Primary && GetSlotKind(ToSlot) == EGinkgoWeaponSlotKind::Primary;
	}

	int32 FindFreeSlot(const EGinkgoWeaponSlotKind Kind, const TConstArrayView<bool> Carried)
	{
		for (int32 Slot = 0; Slot < Carried.Num(); ++Slot)
		{
			if (!Carried[Slot] && GetSlotKind(Slot) == Kind)
			{
				return Slot;
			}
		}
		return INDEX_NONE;
	}

	FGinkgoWeaponPickupPlan PlanWeaponPickup(const EGinkgoWeaponSlotKind Kind, const TConstArrayView<bool> Carried, const int32 HeldSlot, const int32 LastHeldSlot)
	{
		FGinkgoWeaponPickupPlan Plan;
		const bool bHolding = Carried.IsValidIndex(HeldSlot) && Carried[HeldSlot];
		// 手上的收不起来（重武器）：要腾手时扔在地上，不收
		const bool bHeldStows = bHolding && CanStow(GetSlotKind(HeldSlot));
		const bool bHeldDrops = bHolding && !bHeldStows;
		Plan.Slot = FindFreeSlot(Kind, Carried);
		if (Plan.Slot != INDEX_NONE)
		{
			Plan.bStowHeld = bHeldStows;
			Plan.bDropHeld = bHeldDrops;
			return Plan;
		}
		if (bHolding && GetSlotKind(HeldSlot) == Kind)
		{
			Plan.Slot = HeldSlot;
			Plan.DropSlot = HeldSlot;
			return Plan;
		}
		int32 Drop = Carried.IsValidIndex(LastHeldSlot) && Carried[LastHeldSlot] && GetSlotKind(LastHeldSlot) == Kind ? LastHeldSlot : INDEX_NONE;
		for (int32 Slot = Carried.Num() - 1; Slot >= 0 && Drop == INDEX_NONE; --Slot)
		{
			if (Carried[Slot] && GetSlotKind(Slot) == Kind)
			{
				Drop = Slot;
			}
		}
		Plan.Slot = Drop;
		Plan.DropSlot = Drop;
		Plan.bStowHeld = bHeldStows;
		Plan.bDropHeld = bHeldDrops;
		return Plan;
	}

	FTransform GetRefPoseComponentTransform(const FReferenceSkeleton& RefSkeleton, int32 BoneIndex)
	{
		FTransform Result = FTransform::Identity;
		for (; BoneIndex != INDEX_NONE; BoneIndex = RefSkeleton.GetParentIndex(BoneIndex))
		{
			Result = Result * RefSkeleton.GetRefBonePose()[BoneIndex];
		}
		return Result;
	}

	float ComputeSpread(const FGinkgoWeaponSpread& Spread, const FGinkgoSpreadInput& Input)
	{
		const float Ads = FMath::Clamp(Input.AdsAlpha, 0.f, 1.f);
		// 子弹改的扩散乘在默认与上限上（独头弹开镜更准），连射累积的上限跟着变（GetMaxSpreadBloom）
		const float Hip = FMath::Max(Input.HipScale, 0.f);
		const float AdsScale = FMath::Max(Input.AdsScale, 0.f);
		const float Default = FMath::Lerp(Spread.HipDefault * Hip * Input.HipBaseScale, Spread.AdsDefault * AdsScale * Input.AdsBaseScale, Ads);
		// 上限配得比默认小时不往下压
		const float Limit = FMath::Max(FMath::Lerp(Spread.HipMax * Hip, Spread.AdsMax * AdsScale, Ads), Default);
		const float Value = FMath::Min(Default + FMath::Max(Input.Bloom, 0.f), Limit);
		const float Speed = Spread.SpeedFactor.GetRichCurveConst()->Eval(Input.Speed, 1.f);
		const float Movement = Speed > 1.f ? 1.f + (Speed - 1.f) * FMath::Max(0.f, Input.MovingScale) : Speed;
		return Value * Movement * GetSpreadPostureFactor(Spread, Input);
	}

	float GetSpreadPostureFactor(const FGinkgoWeaponSpread& Spread, const FGinkgoSpreadInput& Input)
	{
		const auto Blend = [](const float Factor, const float Alpha) { return FMath::Lerp(1.f, Factor, FMath::Clamp(Alpha, 0.f, 1.f)); };
		return Blend(Spread.AirFactor, Input.AirAlpha) * Blend(Spread.CrouchFactor, Input.CrouchAlpha) * Blend(Spread.SlideFactor, Input.SlideAlpha);
	}

	float GetMaxSpreadBloom(const FGinkgoWeaponSpread& Spread, const float AdsAlpha, const float HipScale, const float AdsScale, const float HipBaseScale, const float AdsBaseScale)
	{
		const float Ads = FMath::Clamp(AdsAlpha, 0.f, 1.f);
		const float Hip = FMath::Max(HipScale, 0.f);
		const float Aim = FMath::Max(AdsScale, 0.f);
		return FMath::Max(FMath::Lerp(Spread.HipMax * Hip, Spread.AdsMax * Aim, Ads) - FMath::Lerp(Spread.HipDefault * Hip * HipBaseScale, Spread.AdsDefault * Aim * AdsBaseScale, Ads), 0.f);
	}

	FVector SampleSpreadDirection(const FVector& Aimed, const float SpreadDegrees, FRandomStream& Random)
	{
		const double Tilt = FMath::DegreesToRadians(static_cast<double>(FMath::Max(SpreadDegrees, 0.f) * Random.FRand()));
		const double Around = Random.FRand() * UE_DOUBLE_TWO_PI;
		if (Tilt <= 0.0)
		{
			return Aimed;
		}
		const FMatrix Basis = FRotationMatrix::MakeFromX(Aimed);
		const FVector Side = Basis.GetUnitAxis(EAxis::Y) * FMath::Cos(Around) + Basis.GetUnitAxis(EAxis::Z) * FMath::Sin(Around);
		return Aimed * FMath::Cos(Tilt) + Side * FMath::Sin(Tilt);
	}

	double SpreadToPixels(const float SpreadDegrees, const float ViewFov)
	{
		return FMath::Tan(FMath::DegreesToRadians(static_cast<double>(SpreadDegrees))) * (540.0 * (4.0 / 3.0) / FMath::Tan(FMath::DegreesToRadians(static_cast<double>(ViewFov) * 0.5)));
	}

	float CrosshairDashOffset(const float Gap, const float SpreadPixels)
	{
		return Gap > 0.f ? FMath::Max(Gap, SpreadPixels) : 0.f;
	}

	float MakeLeftHandGrip(const bool bSprintTwoHanded, const float SprintAlpha, const bool bRunTwoHanded, const float RunAlpha, const float RaiseAlpha, const float ClipGrip)
	{
		const float Sprint = bSprintTwoHanded ? 1.f : 1.f - SprintAlpha;
		// 跑的循环只在低持时用（层里抬枪在它后面混）：抬到多高，左手回到枪上多少
		const float Run = bRunTwoHanded ? 1.f : 1.f - RunAlpha * (1.f - RaiseAlpha);
		return FMath::Clamp(Sprint * Run * ClipGrip, 0.f, 1.f);
	}

	EGinkgoHoldStance SelectStance(const FGinkgoHoldInput& Input)
	{
		if (!Input.bArmed)
		{
			return EGinkgoHoldStance::Unarmed;
		}
		if (Input.bGroundSprinting)
		{
			return Input.bBothHandsOnGun ? EGinkgoHoldStance::Lowered : EGinkgoHoldStance::Sprint;
		}
		if (Input.bAimHeld)
		{
			return EGinkgoHoldStance::Ads;
		}
		return Input.HipHoldRemaining > 0.f ? EGinkgoHoldStance::Hip : EGinkgoHoldStance::Lowered;
	}

	bool WantsRaised(const EGinkgoHoldStance Stance)
	{
		return Stance == EGinkgoHoldStance::Hip || Stance == EGinkgoHoldStance::Ads;
	}

	bool WantsAds(const EGinkgoHoldStance Stance)
	{
		return Stance == EGinkgoHoldStance::Ads;
	}

	bool WantsSprint(const EGinkgoHoldStance Stance)
	{
		return Stance == EGinkgoHoldStance::Sprint;
	}

	float StepProgress(const float Progress, const bool bFull, const float RiseTime, const float FallTime, const float DeltaSeconds)
	{
		const float Duration = bFull ? RiseTime : FallTime;
		if (Duration <= 0.f)
		{
			return bFull ? 1.f : 0.f;
		}
		// 一帧一帧加起来有舍入：时长正好落在帧的边界上时差几个 ULP 就会晚一帧到满，所以离端点很近时直接算到了（1e-4 是 0.18 s 的 18 µs）
		constexpr float SnapEpsilon = 1e-4f;
		const float Step = FMath::Max(DeltaSeconds, 0.f) / Duration;
		if (bFull)
		{
			const float Next = Progress + Step;
			return Next >= 1.f - SnapEpsilon ? 1.f : Next;
		}
		const float Next = Progress - Step;
		return Next <= SnapEpsilon ? 0.f : Next;
	}

	float EaseProgress(const float Progress)
	{
		const float T = FMath::Clamp(Progress, 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	void StepHoldState(FGinkgoHoldState& State, const EGinkgoHoldStance Stance, const FGinkgoHoldTimings& Timings, const float DeltaSeconds)
	{
		State.Stance = Stance;
		State.RaiseProgress = StepProgress(State.RaiseProgress, WantsRaised(Stance), Timings.RaiseTime, Timings.LowerTime, DeltaSeconds);
		State.AdsProgress = StepProgress(State.AdsProgress, WantsAds(Stance), Timings.AdsTime, Timings.AdsTime, DeltaSeconds);
		State.SprintProgress = StepProgress(State.SprintProgress, WantsSprint(Stance), Timings.SprintTime, Timings.SprintTime, DeltaSeconds);
		State.HipHoldRemaining = FMath::Max(State.HipHoldRemaining - FMath::Max(DeltaSeconds, 0.f), 0.f);
	}

	FGinkgoWeaponHandTargets BlendHandTargets(const FGinkgoWeaponHandTargets& Lowered, const FGinkgoWeaponHandTargets& Sprint,
		const FGinkgoWeaponHandTargets& Hip, const FGinkgoWeaponHandTargets& Aim, const float SprintAlpha, const float RaiseAlpha, const float AdsAlpha)
	{
		const auto Blend = [](const FTransform& A, const FTransform& B, const float Alpha)
		{
			return FTransform(FQuat::Slerp(A.GetRotation(), B.GetRotation(), Alpha), FMath::Lerp(A.GetLocation(), B.GetLocation(), static_cast<double>(Alpha)));
		};
		FGinkgoWeaponHandTargets Result;
		Result.Left = Blend(Blend(Blend(Lowered.Left, Sprint.Left, SprintAlpha), Hip.Left, RaiseAlpha), Aim.Left, AdsAlpha);
		Result.Right = Blend(Blend(Blend(Lowered.Right, Sprint.Right, SprintAlpha), Hip.Right, RaiseAlpha), Aim.Right, AdsAlpha);
		return Result;
	}

	FQuat BlendTurnInFront(const FQuat& Body, const FQuat& From, const FQuat& To, const float Alpha)
	{
		const double T = FMath::Clamp(Alpha, 0.f, 1.f);
		const FQuat ToBody = Body.Inverse();
		const FRotator A = (ToBody * From).Rotator();
		const FRotator B = (ToBody * To).Rotator();
		const FRotator Turn(FMath::Lerp(A.Pitch, B.Pitch, T), FMath::Lerp(A.Yaw, B.Yaw, T), A.Roll + T * FRotator::NormalizeAxis(B.Roll - A.Roll));
		return (Body * Turn.Quaternion()).GetNormalized();
	}

	FGinkgoPickupSettlePose BlendPickupSettle(const FGinkgoPickupSettleStart& Start, const FQuat& Body, const FTransform& Hand, const FTransform& Gun,
		const FTransform& GunHand, const float Alpha)
	{
		const float T = FMath::Clamp(Alpha, 0.f, 1.f);
		// 枪上被握的那一点（枪的坐标）：持枪时右手挂点的位置。它从片段里手的挂点直线走到持枪时的地方，枪绕它转：转的那一下不甩动手
		const FVector Held = GunHand.TransformPosition(Start.ItemInHand);
		const FVector Point = FMath::Lerp(Hand.TransformPosition(Start.ItemInHand), Gun.TransformPosition(Held), static_cast<double>(T));
		const FQuat Rotation = BlendTurnInFront(Body, Start.GunRotation, Gun.GetRotation(), T);
		FGinkgoPickupSettlePose Pose;
		Pose.Gun = FTransform(Rotation, Point - Rotation.RotateVector(Gun.GetScale3D() * Held), Gun.GetScale3D());
		// 右手握着枪转：抓住时在枪上的握法换到持枪的。两头都是定的，只随进度走，不会因为片段里的手转了翻到另一边
		const FQuat Grip = FQuat::Slerp(Start.GunRotation.Inverse() * Start.HandRotation, GunHand.GetRotation(), T);
		const FQuat HandRotation = (Rotation * Grip).GetNormalized();
		Pose.RightHand = FTransform(HandRotation, Point - HandRotation.RotateVector(Hand.GetScale3D() * Start.ItemInHand), Hand.GetScale3D());
		return Pose;
	}

	EGinkgoWeaponRootState SelectRootState(const int32 HeldSlot, const int32 TargetSlot)
	{
		if (HeldSlot == INDEX_NONE)
		{
			return TargetSlot == INDEX_NONE ? EGinkgoWeaponRootState::Unarmed : EGinkgoWeaponRootState::Draw;
		}
		return TargetSlot == HeldSlot ? EGinkgoWeaponRootState::Armed : EGinkgoWeaponRootState::Stow;
	}

	EGinkgoWeaponAction SelectWeaponAction(const FGinkgoWeaponActionFacts& Facts)
	{
		if (!Facts.bHandsFree)
		{
			if (Facts.bOneHandFire && Facts.bWantsFire) return Facts.bCanFire ? EGinkgoWeaponAction::Fire : EGinkgoWeaponAction::DryFire;
			return EGinkgoWeaponAction::Ready;
		}
		// 开膛待击、不装填（MG3，Docs/Design/ActionStates.md「MG3」）：枪机开着就是准备好了，不当成要拉机柄
		if (Facts.bOpenBolt)
		{
			if (Facts.bWantsFire)
			{
				return Facts.bCanFire ? EGinkgoWeaponAction::Fire : EGinkgoWeaponAction::DryFire;
			}
			return Facts.bIdleRelaxDue ? EGinkgoWeaponAction::IdleRelax : EGinkgoWeaponAction::Ready;
		}
		const bool bReloading = Facts.bWantsReload && Facts.bCanLoad;
		// 单发中折（M79）：没有供弹仓，炮膛内只有一发。
		// 装入新弹后未合膛（收枪再拔出）且不在重新装填时直接合膛闭锁；想装填且后备有弹时，按膛内是否为活弹区分有弹重装填（高举重力甩弹）与空仓换弹
		if (Facts.bSingleShot)
		{
			const bool bChamberLive = !Facts.bChamberSpent && !Facts.bChamberEmpty;
			if (Facts.bActionOpen && bChamberLive && !bReloading)
			{
				return EGinkgoWeaponAction::CloseBolt;
			}
			if (bReloading)
			{
				return bChamberLive ? EGinkgoWeaponAction::ReloadTactical : EGinkgoWeaponAction::ReloadEmpty;
			}
			if (Facts.bWantsFire)
			{
				return Facts.bCanFire ? EGinkgoWeaponAction::Fire : EGinkgoWeaponAction::DryFire;
			}
			return Facts.bIdleRelaxDue && !Facts.bActionOpen ? EGinkgoWeaponAction::IdleRelax : EGinkgoWeaponAction::Ready;
		}
		// 管状弹仓（泵动霰弹枪，Docs/Design/ActionStates.md「霰弹枪」）：枪机关着从下面压，不开栓。手在装填口下面时接着压或者收回来；
		// 泵到一半被打断的推回去；膛里空壳、或者压完了膛还空着（管里有）就泵一下，先于装填：打完一枪先泵好再去压。
		// 从抛壳口压膛（换了弹种、或者枪里一发也没有时装填，Docs/Design/Weapons.md「切换弹种」）先于泵动、装填：手在装填口下面时先收回来；
		// 护木在后面、膛空着（压膛做到一半）从放一发接着做，膛里已经放进去了就推回去
		if (Facts.bLoadsWithActionClosed)
		{
			const bool bLoadingChamber = Facts.bWantsLoadChamber && Facts.bCanLoadChamber;
			if (Facts.bHandAtAction)
			{
				return bReloading && !bLoadingChamber ? EGinkgoWeaponAction::LoadRound : EGinkgoWeaponAction::ReloadEnd;
			}
			if (Facts.bActionOpen)
			{
				return bLoadingChamber && Facts.bChamberEmpty ? EGinkgoWeaponAction::LoadChamber : EGinkgoWeaponAction::CloseBolt;
			}
			if (bLoadingChamber)
			{
				return EGinkgoWeaponAction::LoadChamber;
			}
			if (Facts.bChamberSpent || (Facts.bChamberEmpty && Facts.bFeedHasRounds))
			{
				return EGinkgoWeaponAction::CycleBolt;
			}
			if (bReloading)
			{
				return EGinkgoWeaponAction::ReloadStart;
			}
			if (Facts.bWantsFire)
			{
				return Facts.bCanFire ? EGinkgoWeaponAction::Fire : EGinkgoWeaponAction::DryFire;
			}
			return Facts.bIdleRelaxDue ? EGinkgoWeaponAction::IdleRelax : EGinkgoWeaponAction::Ready;
		}
		// 没有空仓挂机的枪（AK）打空了枪机是关着的，膛是空的：同样是空仓换弹，换了弹匣要拉机柄
		const bool bNeedsCharging = Facts.bActionOpen || (Facts.bDetachableFeed && Facts.bChamberEmpty);
		if (bReloading && Facts.bDetachableFeed)
		{
			return bNeedsCharging ? EGinkgoWeaponAction::ReloadEmpty : EGinkgoWeaponAction::ReloadTactical;
		}
		if (bNeedsCharging && Facts.bFeedHasRounds && !bReloading)
		{
			return EGinkgoWeaponAction::CloseBolt;
		}
		if (Facts.bChamberSpent && !Facts.bActionOpen && !bReloading)
		{
			return EGinkgoWeaponAction::CycleBolt;
		}
		if (bReloading)
		{
			if (!Facts.bActionOpen)
			{
				return EGinkgoWeaponAction::OpenBolt;
			}
			return Facts.bHandAtAction ? EGinkgoWeaponAction::LoadRound : EGinkgoWeaponAction::ReloadStart;
		}
		if (Facts.bWantsFire)
		{
			return Facts.bCanFire ? EGinkgoWeaponAction::Fire : EGinkgoWeaponAction::DryFire;
		}
		return Facts.bIdleRelaxDue ? EGinkgoWeaponAction::IdleRelax : EGinkgoWeaponAction::Ready;
	}

	bool ShouldLoadTwo(const int32 Room, const int32 Reserve)
	{
		return Room >= 2 && Reserve >= 2;
	}

	EGinkgoReloadKeyAction StepReloadKey(FGinkgoReloadKey& Key, const bool bPressed, const bool bHeld, const bool bHasWheel, const bool bCanOpenWheel, const float HoldTime,
		const float DeltaSeconds)
	{
		if (bPressed)
		{
			// 没有轮盘的枪按下就装（同以前），有轮盘的等松开或按够。上一次还没松开又按了一下（松开的那一帧丢了）：从头算
			if (!bHasWheel)
			{
				Key = FGinkgoReloadKey();
				return EGinkgoReloadKeyAction::Reload;
			}
			const bool bWasOpen = Key.bWheelOpen;
			Key = FGinkgoReloadKey();
			Key.bPending = true;
			Key.bWheelOpen = bWasOpen;
			// 同一帧里按下又松开（点得快、低帧率）：就是短按
			if (!bHeld)
			{
				Key.bPending = false;
				Key.bWheelOpen = false;
				return bWasOpen ? EGinkgoReloadKeyAction::ConfirmWheel : EGinkgoReloadKeyAction::Reload;
			}
			return EGinkgoReloadKeyAction::None;
		}
		if (!Key.bPending)
		{
			return EGinkgoReloadKeyAction::None;
		}
		if (!bHeld)
		{
			const bool bWasOpen = Key.bWheelOpen;
			Key = FGinkgoReloadKey();
			return bWasOpen ? EGinkgoReloadKeyAction::ConfirmWheel : EGinkgoReloadKeyAction::Reload;
		}
		Key.HeldTime += FMath::Max(DeltaSeconds, 0.f);
		if (!Key.bWheelOpen && bHasWheel && bCanOpenWheel && Key.HeldTime >= HoldTime)
		{
			Key.bWheelOpen = true;
			return EGinkgoReloadKeyAction::OpenWheel;
		}
		return EGinkgoReloadKeyAction::None;
	}

	int32 PickWheelSector(const FVector2D& Direction, const int32 Count)
	{
		if (Count <= 1)
		{
			return 0;
		}
		// 从正上方往右量的角（0 到 2π）
		double Angle = FMath::Atan2(Direction.X, Direction.Y);
		if (Angle < 0.0)
		{
			Angle += UE_DOUBLE_TWO_PI;
		}
		const double Step = UE_DOUBLE_TWO_PI / Count;
		return FMath::FloorToInt32((Angle + Step * 0.5) / Step) % Count;
	}

	FVector2D GetWheelSectorDirection(const int32 Index, const int32 Count)
	{
		const double Angle = Count > 0 ? UE_DOUBLE_TWO_PI * Index / Count : 0.0;
		return FVector2D(FMath::Sin(Angle), FMath::Cos(Angle));
	}

	FVector2D StepWheelMotion(const FVector2D& Motion, const FVector2D& Delta, const float Radius, const float DecayTime, const float DeltaSeconds)
	{
		const double Keep = DecayTime > 0.f ? FMath::Exp(-DeltaSeconds / DecayTime) : 0.0;
		const FVector2D Moved = Motion * Keep + Delta;
		const double Length = Moved.Size();
		return Radius > 0.f && Length > Radius ? Moved * (Radius / Length) : Moved;
	}

	int32 SelectWheelEntry(const FVector2D& Motion, const FVector2D& Stick, const int32 Count, const int32 Current, const float MotionDeadZone, const float StickDeadZone)
	{
		if (Count <= 0)
		{
			return INDEX_NONE;
		}
		if (Stick.Size() >= FMath::Max(StickDeadZone, UE_KINDA_SMALL_NUMBER))
		{
			return PickWheelSector(Stick, Count);
		}
		if (Motion.Size() >= FMath::Max(MotionDeadZone, UE_KINDA_SMALL_NUMBER))
		{
			return PickWheelSector(Motion, Count);
		}
		return Current;
	}

	double GetNextShotTime(const double PreviousNextShot, const double Now, const double Interval, const bool bChained)
	{
		return GetScheduledShotTime(PreviousNextShot, Now, Interval, bChained) + Interval;
	}

	double GetScheduledShotTime(const double PreviousNextShot, const double Now, const double Interval, const bool bChained)
	{
		return bChained && Now - PreviousNextShot < Interval ? PreviousNextShot : Now;
	}

	bool ShouldLeaveTrail(const double ShotTime, const double LastTrailShot, const double MinInterval)
	{
		return MinInterval <= 0.0 || ShotTime - LastTrailShot >= MinInterval - 1e-4;
	}

	void AddRecoilKick(FGinkgoRecoilState& State, const UGinkgoRecoilPattern& Pattern, const FGinkgoPatternRecoil& Recoil, const FVector2D& Scale, const FVector2D& Jitter)
	{
		if (State.Pattern.Get() != &Pattern)
		{
			State.Pattern = &Pattern;
			State.ShotIndex = 0.f;
		}
		const int32 Shot = FMath::FloorToInt32(State.ShotIndex);
		const FVector2f Step = Pattern.GetStepAfterShot(Shot);
		FGinkgoRecoilKick& Kick = State.Kicks.AddDefaulted_GetRef();
		Kick.Delta = FVector2D(Step.X, Step.Y) * Scale + Jitter;
		Kick.Duration = Recoil.ApplyTime;
		State.ShotIndex = static_cast<float>(Shot + 1);
		State.SinceShot = 0.0;
		State.RecoveryDelay = Recoil.RecoveryDelay;
		State.RecoveryDecay = Recoil.RecoveryDecay;
		State.RecoveryLinear = Recoil.RecoveryLinear;
		State.ShotRecoveryRate = Recoil.ShotRecoveryRate;
	}

	FVector2D StepRecoil(FGinkgoRecoilState& State, const FVector2D& PlayerDelta, const double DeltaSeconds)
	{
		// 玩家往回压：同还没回复的方向相反的部分抵掉，同方向的不算（玩家自己想往那边看）
		const auto Compensate = [](double& Accumulated, const double Input)
		{
			if (Accumulated * Input < 0.0)
			{
				Accumulated = FMath::Sign(Accumulated) * FMath::Max(FMath::Abs(Accumulated) - FMath::Abs(Input), 0.0);
			}
		};
		Compensate(State.Accumulated.X, PlayerDelta.X);
		Compensate(State.Accumulated.Y, PlayerDelta.Y);

		// 这一帧摊到的增量：先快后慢，1 − (1 − t)²，走完的部分加起来就是增量，与帧率无关
		const auto Ease = [](const double Time) { return 1.0 - FMath::Square(1.0 - Time); };
		FVector2D Moved = FVector2D::ZeroVector;
		for (int32 Index = State.Kicks.Num() - 1; Index >= 0; --Index)
		{
			FGinkgoRecoilKick& Kick = State.Kicks[Index];
			const double Before = Kick.Duration > 0.0 ? Ease(FMath::Min(Kick.Elapsed / Kick.Duration, 1.0)) : 0.0;
			Kick.Elapsed += DeltaSeconds;
			const double After = Kick.Duration > 0.0 ? Ease(FMath::Min(Kick.Elapsed / Kick.Duration, 1.0)) : 1.0;
			Moved += Kick.Delta * (After - Before);
			if (After >= 1.0)
			{
				State.Kicks.RemoveAt(Index);
			}
		}
		State.Accumulated += Moved;

		// 停火以后回：d|A|/dt = −k|A| − L，方向不变。解析解：|A|(t) = (|A₀| + L/k)·e^(−kt) − L/k，到 0 为止
		const double Previous = State.SinceShot;
		State.SinceShot = FMath::Min(Previous + DeltaSeconds, static_cast<double>(TNumericLimits<float>::Max()));
		const double Recovering = FMath::Max(State.SinceShot - FMath::Max(Previous, static_cast<double>(State.RecoveryDelay)), 0.0);
		if (Recovering > 0.0)
		{
			const double Length = State.Accumulated.Size();
			if (Length > 0.0)
			{
				const double Decay = State.RecoveryDecay;
				const double Linear = State.RecoveryLinear;
				const double Next = Decay > UE_SMALL_NUMBER
					? (Length + Linear / Decay) * FMath::Exp(-Decay * Recovering) - Linear / Decay
					: Length - Linear * Recovering;
				const FVector2D Recovered = State.Accumulated * (FMath::Max(Next, 0.0) / Length - 1.0);
				State.Accumulated += Recovered;
				Moved += Recovered;
			}
			State.ShotIndex = FMath::Max(State.ShotIndex - static_cast<float>(Recovering) * State.ShotRecoveryRate, 0.f);
		}
		State.Applied += Moved;
		return Moved;
	}

	void AddGunKick(FGinkgoGunKickState& State, const FGinkgoGunKick& Kick, const float YawAlpha, const float RollAlpha)
	{
		const auto Impulse = [&Kick](const double Peak) { return Spring::DampedImpulseForPeak(Peak, Kick.RotationOmega, Kick.RotationZeta); };
		State.BackVelocity += Spring::DampedImpulseForPeak(Kick.Back, Kick.BackOmega, Kick.BackZeta);
		State.RotationVelocity.Pitch += Impulse(Kick.Climb);
		State.RotationVelocity.Yaw += Impulse(Kick.Yaw * YawAlpha);
		State.RotationVelocity.Roll += Impulse(Kick.Roll * RollAlpha);
	}

	void StepGunKick(FGinkgoGunKickState& State, const FGinkgoGunKick& Kick, const float DeltaSeconds)
	{
		Spring::StepDamped(State.Back, State.BackVelocity, Kick.BackOmega, Kick.BackZeta, DeltaSeconds);
		Spring::StepDamped(State.Rotation.Pitch, State.RotationVelocity.Pitch, Kick.RotationOmega, Kick.RotationZeta, DeltaSeconds);
		Spring::StepDamped(State.Rotation.Yaw, State.RotationVelocity.Yaw, Kick.RotationOmega, Kick.RotationZeta, DeltaSeconds);
		Spring::StepDamped(State.Rotation.Roll, State.RotationVelocity.Roll, Kick.RotationOmega, Kick.RotationZeta, DeltaSeconds);
	}

	void StepHipAim(FGinkgoHipAimState& State, const FVector2D& ViewDelta, const double Omega, const double Zeta, const double MaxLag, const double DeltaSeconds)
	{
		if (DeltaSeconds <= 0.0)
		{
			return;
		}
		const FVector2D ViewVelocity = ViewDelta / DeltaSeconds;
		if (Omega <= 0.0)
		{
			State.Lag = FVector2D::ZeroVector;
			State.Velocity = ViewVelocity;
			return;
		}
		// 视线匀速走时，弹簧的平衡点比它落后 2ζ·速度/ω：从平衡点量，就是不受外力的弹簧
		const FVector2D Balance = ViewVelocity * (-2.0 * FMath::Clamp(Zeta, 0.0, 1.0) / Omega);
		FVector2D Error = State.Lag - Balance;
		FVector2D ErrorVelocity = State.Velocity - ViewVelocity;
		Spring::StepDamped(Error, ErrorVelocity, Omega, Zeta, DeltaSeconds);
		State.Lag = Balance + Error;
		State.Velocity = ViewVelocity + ErrorVelocity;

		const double Length = State.Lag.Size();
		if (MaxLag > 0.0 && Length > MaxLag)
		{
			const FVector2D Direction = State.Lag / Length;
			State.Lag = Direction * MaxLag;
			// 往外的速度去掉：贴着上限时跟着视线走，视线停了才回来
			const double Outward = FVector2D::DotProduct(State.Velocity - ViewVelocity, Direction);
			if (Outward > 0.0)
			{
				State.Velocity -= Direction * Outward;
			}
		}
	}

	FTransform MakeFeedRoundTransform(const FGinkgoRoundVisuals& Rounds, const FGinkgoBoltRig& Bolt, const float Travel)
	{
		const double Pushed = 1.0 - FMath::Clamp(Travel, 0.f, 1.f);
		return FTransform(Rounds.FeedTop + FVector(Bolt.Stroke * Pushed, 0.0, Rounds.ChamberRise * Pushed));
	}

	FVector SampleEjectVelocity(const FGinkgoRoundVisuals& Rounds, const FQuat& GunRotation, const FVector& GunVelocity, FRandomStream& Random)
	{
		const FVector Direction = Random.VRandCone(Rounds.EjectDirection.GetSafeNormal(), FMath::DegreesToRadians(Rounds.EjectSpread));
		return GunVelocity + GunRotation.RotateVector(Direction) * Random.FRandRange(Rounds.EjectSpeed.Min, Rounds.EjectSpeed.Max);
	}

	FVector SampleEjectSpin(const FGinkgoRoundVisuals& Rounds, const FQuat& GunRotation, FRandomStream& Random)
	{
		const FVector Axis = Random.VRandCone(FVector::UpVector, FMath::DegreesToRadians(25.f));
		return GunRotation.RotateVector(Axis) * Random.FRandRange(Rounds.EjectSpin.Min, Rounds.EjectSpin.Max);
	}

	float ShellHitVolume(const FGinkgoRoundVisuals& Rounds, const double Speed)
	{
		if (Speed < Rounds.HitSpeed.Min)
		{
			return 0.f;
		}
		const double Range = FMath::Max(static_cast<double>(Rounds.HitSpeed.Max - Rounds.HitSpeed.Min), 1.0);
		return FMath::Lerp(Rounds.HitQuietVolume, 1.f, FMath::Sqrt(static_cast<float>(FMath::Min((Speed - Rounds.HitSpeed.Min) / Range, 1.0))));
	}

	float HitMarkScale(const double Gap)
	{
		constexpr double HiddenGap = 8.0;
		constexpr double FullGap = 16.0;
		return static_cast<float>(FMath::Clamp((Gap - HiddenGap) / (FullGap - HiddenGap), 0.0, 1.0));
	}

	FRotator MakeHipAimRotation(const FRotator& View, const FVector2D& Lag, const float AdsAlpha)
	{
		const double Scale = 1.0 - FMath::Clamp(AdsAlpha, 0.f, 1.f);
		return FRotator(FMath::Clamp(FRotator::NormalizeAxis(View.Pitch) + Lag.Y * Scale, -89.9, 89.9), View.Yaw + Lag.X * Scale, 0.0);
	}

	FTransform MakeGunKickTransform(const FGinkgoGunKickState& State, const FGinkgoGunKick& Kick)
	{
		const FQuat Rotation = State.Rotation.Quaternion();
		return FTransform(Rotation, Kick.Pivot - Rotation.RotateVector(Kick.Pivot) - FVector(State.Back, 0.0, 0.0));
	}

	float MuzzleWispAlpha(const float Remaining, const float Opacity)
	{
		const float Clamped = FMath::Clamp(Remaining, 0.f, 1.f);
		return Opacity * Clamped * FMath::Sqrt(Clamped);
	}

	float CoolBarrel(const float Heat, const float CoolTime, const float DeltaSeconds)
	{
		return Heat * FMath::Exp(-FMath::Max(DeltaSeconds, 0.f) / FMath::Max(CoolTime, UE_KINDA_SMALL_NUMBER));
	}

	float HeatHazeAlpha(const float Heat, const float Start, const float Full, const float Puff)
	{
		// Full 不比 Start 大时：过了 Start 就是满的
		const float Barrel = Full > Start ? EaseProgress((Heat - Start) / (Full - Start)) : (Heat >= Start ? 1.f : 0.f);
		return FMath::Min(Barrel + FMath::Max(Puff, 0.f), 1.f);
	}

	FGinkgoBoltPose BlendBoltPose(const bool bActionOpen, const float ClipWeight, const FGinkgoBoltPose& Clip)
	{
		const float State = bActionOpen ? 1.f : 0.f;
		const float Weight = FMath::Clamp(ClipWeight, 0.f, 1.f);
		return {FMath::Lerp(State, Clip.Travel, Weight), FMath::Lerp(State, Clip.Lift, Weight)};
	}

	FTransform MakeBoltCarrierTransform(const FTransform& RefPose, const FGinkgoBoltRig& Rig, const FGinkgoBoltPose& Pose)
	{
		FTransform Out = RefPose;
		Out.AddToTranslation(FVector(-Rig.Stroke * Pose.Travel, 0.0, 0.0));
		return Out;
	}

	FTransform MakeBoltHandleTransform(const FTransform& RefPose, const FGinkgoBoltRig& Rig, const FGinkgoBoltPose& Pose)
	{
		FTransform Out = MakeBoltCarrierTransform(RefPose, Rig, Pose);
		Out.SetRotation(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(Rig.LiftAngle * Pose.Lift)) * RefPose.GetRotation());
		return Out;
	}

	FGinkgoCameraImpulse SampleCameraRecoil(const FGinkgoCameraRecoil& Recoil, const FRandomStream& Random)
	{
		const auto Sample = [&Random](const FFloatInterval& Range) { return Random.FRandRange(Range.Min, Range.Max); };
		const auto Side = [&Recoil, &Random] { return Recoil.bRandomSide && Random.FRand() < 0.5f ? -1.0 : 1.0; };
		// 一句一个数：写在同一个表达式里时取数的先后没有定义
		const double Omega = Sample(Recoil.Omega);
		const double Zeta = Sample(Recoil.Zeta);
		const auto Impulse = [Omega, Zeta](const double Peak) { return Spring::DampedImpulseForPeak(Peak, Omega, Zeta); };
		const double Pitch = Impulse(Sample(Recoil.Pitch));
		const double Yaw = Impulse(Sample(Recoil.Yaw));
		const double YawSide = Side();
		const double Roll = Impulse(Sample(Recoil.Roll));
		const double RollSide = Side();
		FGinkgoCameraImpulse Out;
		Out.Rotation = FRotator(Pitch, Yaw * YawSide, Roll * RollSide);
		Out.FieldOfView = static_cast<float>(Impulse(Sample(Recoil.FieldOfView)));
		Out.Omega = static_cast<float>(Omega);
		Out.Zeta = static_cast<float>(Zeta);
		Out.ReturnOmega = Out.Omega;
		Out.ReturnZeta = Out.Zeta;
		// 放在最后、关着时不取数：回弹不单独配的枪，每枪取到的数不变
		if (Recoil.bSeparateReturn)
		{
			Out.ReturnOmega = static_cast<float>(Sample(Recoil.ReturnOmega));
			Out.ReturnZeta = static_cast<float>(Sample(Recoil.ReturnZeta));
		}
		return Out;
	}

	int32 FindDrawSlot(const int32 LastSlot, const TConstArrayView<bool> Carried)
	{
		if (Carried.IsValidIndex(LastSlot) && Carried[LastSlot])
		{
			return LastSlot;
		}
		return Carried.Find(true);
	}

	int32 ApplyWeaponKey(const EGinkgoWeaponKey Key, const int32 TargetSlot, const int32 LastSlot, const TConstArrayView<bool> Carried)
	{
		switch (Key)
		{
		case EGinkgoWeaponKey::Slot1:
			[[fallthrough]];
		case EGinkgoWeaponKey::Slot2:
			[[fallthrough]];
		case EGinkgoWeaponKey::Sidearm:
		{
			const int32 Slot = Key == EGinkgoWeaponKey::Slot1 ? 0 : (Key == EGinkgoWeaponKey::Slot2 ? 1 : SidearmSlot);
			return Carried.IsValidIndex(Slot) && Carried[Slot] ? Slot : TargetSlot;
		}
		case EGinkgoWeaponKey::Holster:
			return TargetSlot != INDEX_NONE ? INDEX_NONE : FindDrawSlot(LastSlot, Carried);
		case EGinkgoWeaponKey::Switch:
		{
			// 徒手、拿着重武器（扔了它）时：拔上一次拿的那把（重武器不记成上一次）
			if (TargetSlot == INDEX_NONE || !CanStow(GetSlotKind(TargetSlot)))
			{
				return FindDrawSlot(LastSlot, Carried);
			}
			// 下一个有枪的槽；只有这一把时不响应
			for (int32 Offset = 1; Offset < Carried.Num(); ++Offset)
			{
				const int32 Slot = (TargetSlot + Offset) % Carried.Num();
				if (Carried[Slot])
				{
					return Slot;
				}
			}
			return TargetSlot;
		}
		default:
			return TargetSlot;
		}
	}
}
