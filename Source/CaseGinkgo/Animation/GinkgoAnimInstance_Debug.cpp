#include "GinkgoAnimInstance.h"

#if !UE_BUILD_SHIPPING

#include "Animation/Actions/GinkgoAnimAction.h"
#include "Animation/Actions/GinkgoAnimActionComponent.h"
#include "Animation/AnimSequence.h"
#include "Character/GinkgoCharacter.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Parse.h"
#include "UObject/StrongObjectPtr.h"
#include "Utilities/Debug/DebugCategory.h"
#include "Weapon/GinkgoWeaponComponent.h"
#include "Weapon/GinkgoWeaponDefinition.h"

namespace
{
	TAutoConsoleVariable<int32> CVarShowAnim(
		TEXT("ginkgo.showAnim"),
		0,
		TEXT("显示动画实例算出的数据：0 关，1 开。\n")
		TEXT("脚下：绿色是可见的根逐帧差分出来的水平速度，白色细线是移动组件的速度（都是每 1 m/s 长 10 cm；本地角色在地面上两者重合），")
		TEXT("黄色是意图（输入的方向，长度乘输入的大小），蓝色是面向。在地面上没有意图时，红圈是预测的停止点；意图与速度相反时，橙圈是反向点。")
		TEXT("每个角色都画，模拟代理也画。本地角色另有几行屏幕文字：运动（与移动组件的速度之差、加速度、面向与转速、这一帧扣掉的校正）与意图、状态，")
		TEXT("移动状态机，动画动作（在跑的动作与蒙太奇的时刻、两只手的 IK 绑定、持有的相机标签、武器挂在哪），")
		TEXT("持枪（姿态、抬枪与开镜进度、给瞄准偏移的偏航与俯仰、手上的枪与它的动画层）。\n")
		TEXT("腿部 IK：锁住的脚掌是小球（绿锁住、黄滑、橙收回），连到它的线是加给脚的修正；摆动的脚预测的落脚点是蓝圈。"));

	/** 屏幕文字的键：每个动画实例最多八行，放在高位，不与按 UniqueID 取键的调试行（移动、相机）相撞 */
	uint64 ScreenMessageKey(const UObject& Owner, const int32 Line)
	{
		return (uint64(1) << 40) | (uint64(Owner.GetUniqueID()) << 3) | uint64(Line);
	}

	/** 地面上的一个圈：停止点、反向点 */
	void DrawGroundCircle(const UWorld* World, const FVector& Center, const FColor Color)
	{
		DrawDebugCircle(World, Center, 12.f, 16, Color, false, -1.f, SDPG_Foreground, 2.f, FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), false);
	}

	/**
	 * 一次 ginkgo.anim.playSlot：等世界时间到了、本地角色就绪，把片段当动态蒙太奇放进它的槽里（PlaySlotAnimationAsDynamicMontage）。
	 * 还没接进游戏的片段（开镜的开火、拉栓）用它量淡入淡出接不接得上；固定帧长下同 ginkgo.action.test 一样在各个帧率的同一个时刻附近开始
	 */
	struct FSlotTest
	{
		TWeakObjectPtr<UWorld> World;
		TStrongObjectPtr<UAnimSequenceBase> Sequence;
		FName Slot = TEXT("UpperBody");
		double At = 0.0;
		float BlendIn = 0.25f;
		float BlendOut = 0.25f;
	};

	/** @return 还要不要继续等 */
	bool TickSlotTest(const FSlotTest& Test)
	{
		const UWorld* World = Test.World.Get();
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		const ACharacter* Character = Controller ? Cast<ACharacter>(Controller->GetPawn()) : nullptr;
		UAnimInstance* AnimInstance = Character ? Character->GetMesh()->GetAnimInstance() : nullptr;
		if (!World)
		{
			return false;
		}
		if (!AnimInstance || World->GetTimeSeconds() < Test.At)
		{
			return true;
		}
		AnimInstance->PlaySlotAnimationAsDynamicMontage(Test.Sequence.Get(), Test.Slot, Test.BlendIn, Test.BlendOut);
		UE_LOG(LogGinkgoAnimation, Log, TEXT("[Anim] playSlot：%s 在 %.3f s 放进 %s 槽（淡入 %.2f s，淡出 %.2f s）"), *Test.Sequence->GetName(), World->GetTimeSeconds(),
			*Test.Slot.ToString(), Test.BlendIn, Test.BlendOut);
		return false;
	}

	/** ginkgo.anim.playSlot <片段的路径> [at=<秒>] [in=<秒>] [out=<秒>] [slot=<槽>] */
	void RunSlotTest(const TArray<FString>& Args, UWorld* World)
	{
		UAnimSequenceBase* Sequence = Args.IsEmpty() ? nullptr : LoadObject<UAnimSequenceBase>(nullptr, *Args[0]);
		if (!Sequence)
		{
			UE_LOG(LogGinkgoAnimation, Display, TEXT("ginkgo.anim.playSlot <片段的路径> [at=<秒>] [in=<秒>] [out=<秒>] [slot=<槽>]%s"),
				Args.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("：找不到 %s"), *Args[0]));
			return;
		}
		TSharedRef<FSlotTest> Test = MakeShared<FSlotTest>();
		Test->World = World;
		Test->Sequence.Reset(Sequence);
		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			FParse::Value(*Args[Index], TEXT("at="), Test->At);
			FParse::Value(*Args[Index], TEXT("in="), Test->BlendIn);
			FParse::Value(*Args[Index], TEXT("out="), Test->BlendOut);
			FParse::Value(*Args[Index], TEXT("slot="), Test->Slot);
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float)
		{
			return TickSlotTest(*Test);
		}));
	}

	FAutoConsoleCommandWithWorldAndArgs SlotTestCommand(
		TEXT("ginkgo.anim.playSlot"),
		TEXT("把片段放进本地角色动画实例的槽里：ginkgo.anim.playSlot <片段的路径> [at=<秒>] [in=<秒>] [out=<秒>] [slot=<槽>]。")
		TEXT("世界时间到 at 时当动态蒙太奇放，默认 UpperBody 槽、淡入淡出各 0.25 s（引擎的默认）。量还没接进游戏的片段用，发行版不编译"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSlotTest));
}

void UGinkgoAnimInstance::PostEvaluateDebug()
{
	if (Snapshot.bValid && CVarShowAnim.GetValueOnGameThread() > 0)
	{
		DrawAnimDebug();
	}
}

void UGinkgoAnimInstance::DrawAnimDebug() const
{
	// 都画在前景：箭头在脚下，从角色身后看会被腿挡住
	const UWorld* World = GetWorld();
	const FVector Feet = DebugFrame.Feet + FVector(0.0, 0.0, 2.0);
	const FVector MovementVelocity(DebugFrame.MovementVelocity.X, DebugFrame.MovementVelocity.Y, 0.0);
	DrawDebugDirectionalArrow(World, Feet, Feet + Motion.Velocity * 0.1, 20.f, FColor::Green, false, -1.f, SDPG_Foreground, 3.f);
	DrawDebugLine(World, Feet, Feet + MovementVelocity * 0.1, FColor::White, false, -1.f, SDPG_Foreground, 1.f);
	DrawDebugDirectionalArrow(World, Feet, Feet + FRotator(0.0, Motion.FacingYaw, 0.0).Vector() * 60.0, 15.f, FColor::Blue, false, -1.f, SDPG_Foreground, 2.f);
	if (Intent.bHasIntent)
	{
		DrawDebugDirectionalArrow(World, Feet, Feet + Intent.Direction * (100.0 * Intent.Scale), 20.f, FColor::Yellow, false, -1.f, SDPG_Foreground, 2.f);
	}
	if (MoveState.Mode == EGinkgoAnimMode::Ground)
	{
		if (!Intent.bHasIntent && Motion.Speed > MinDirectionSpeed)
		{
			DrawGroundCircle(World, Feet + Motion.Velocity / Motion.Speed * Intent.StopDistance, FColor::Red);
		}
		if (Intent.PivotDistance > 0.f)
		{
			// 速度在意图反方向上的分量过零的地方：沿意图的反方向走这么远
			DrawGroundCircle(World, Feet - Intent.Direction * Intent.PivotDistance, FColor::Orange);
		}
	}
	if (DebugFrame.TimeToImpact >= 0.0)
	{
		// 顺着轨迹预测的着地点：落地片段已经对上时是青色
		DrawGroundCircle(World, DebugFrame.ImpactLocation + FVector(0.0, 0.0, 2.0), DebugFrame.LandImpactTime >= 0.f ? FColor::Cyan : FColor::Magenta);
	}
	if (LegIKAlpha > 0.f)
	{
		// 腿部 IK：锁住的脚掌（绿锁住、黄滑、橙收回），从动画想要的位置连过去的线是加给脚的修正；摆动的脚的预测落脚点是蓝圈
		static const FColor LockColors[] = { FColor::White, FColor::Green, FColor::Yellow, FColor::Orange };
		for (const FDebugFrame::FLegIKFoot& Foot : DebugFrame.LegIK)
		{
			if (Foot.Lock != 0 && Foot.Lock < UE_ARRAY_COUNT(LockColors))
			{
				DrawDebugSphere(World, Foot.LockLocation, 4.f, 8, LockColors[Foot.Lock], false, -1.f, SDPG_Foreground, 1.f);
				DrawDebugLine(World, Foot.AnimBall, Foot.AnimBall + (Foot.LockLocation - Foot.AnimBall).GetClampedToMaxSize2D(Foot.Correction), LockColors[Foot.Lock],
					false, -1.f, SDPG_Foreground, 2.f);
			}
			if (Foot.TimeToLand >= 0.f)
			{
				// 落在扫到的地面上（没有路径时画在胶囊底）
				const double LandingZ = Foot.PathCount > 0 ? Foot.Path[Foot.PathCount - 1].Z : DebugFrame.Feet.Z;
				DrawGroundCircle(World, FVector(Foot.PredictedLanding.X, Foot.PredictedLanding.Y, LandingZ + 2.0), FColor::Blue);
			}
			// 地形：摆动的脚的路径（上凸包）是青色的折线，脚下的地面是一个小十字
			for (int32 Point = 1; Point < Foot.PathCount; ++Point)
			{
				DrawDebugLine(World, Foot.Path[Point - 1] + FVector(0.0, 0.0, 1.0), Foot.Path[Point] + FVector(0.0, 0.0, 1.0), FColor::Cyan, false, -1.f, SDPG_Foreground, 1.5f);
			}
			if (Foot.bHasTerrain)
			{
				const FVector Ground(Foot.AnimBall.X, Foot.AnimBall.Y, Foot.TerrainZ + 1.0);
				DrawDebugLine(World, Ground - FVector(6.0, 0.0, 0.0), Ground + FVector(6.0, 0.0, 0.0), FColor::Cyan, false, -1.f, SDPG_Foreground, 1.f);
				DrawDebugLine(World, Ground - FVector(0.0, 6.0, 0.0), Ground + FVector(0.0, 6.0, 0.0), FColor::Cyan, false, -1.f, SDPG_Foreground, 1.f);
			}
		}
	}

	if (!DebugFrame.bLocallyControlled || !GEngine)
	{
		return;
	}
	// 屏幕文字用英文：引擎的屏幕调试字体没有中文字形
	const FString MotionText = FString::Printf(TEXT("anim motion   speed %.1f cm/s (movement %.1f, diff %.3f)   angle %+.0f deg   vz %+.0f cm/s   ")
		TEXT("accel %.0f cm/s2 (raw %.0f)   facing %.1f deg (%+.0f deg/s, view %.1f)   frame %.2f cm   correction %.1f cm%s"),
		Motion.Speed, MovementVelocity.Size(), (Motion.Velocity - MovementVelocity).Size(), Motion.LocalAngle, Motion.VerticalSpeed, Motion.Acceleration.Size(),
		DebugFrame.RawAcceleration.Size(), Motion.FacingYaw, Motion.YawRate, DebugFrame.ViewYaw, Motion.FrameDistance, DebugFrame.CorrectionOffset.Size(),
		DebugFrame.bReset ? TEXT("   reset") : TEXT(""));
	const FString IntentDirection = Intent.bHasIntent ? FString::Printf(TEXT("yaw %.0f deg"), Intent.Direction.Rotation().Yaw) : FString(TEXT("none"));
	const FString IntentText = FString::Printf(TEXT("anim intent   %s   scale %.2f   to velocity %.0f deg   stop %.1f cm   pivot %.1f cm   mode %s%s%s"),
		*IntentDirection, Intent.Scale, Intent.AngleToVelocity, Intent.StopDistance, Intent.PivotDistance,
		*StaticEnum<EGinkgoAnimMode>()->GetNameStringByValue(static_cast<int64>(MoveState.Mode)), MoveState.bCrouched ? TEXT("  crouched") : TEXT(""),
		MoveState.bSprinting ? TEXT("  sprinting") : TEXT(""));
	const FString AirText = DebugFrame.TimeToImpact >= 0.0 ? FString::Printf(TEXT("   impact in %.3f s"), DebugFrame.TimeToImpact) : FString();
	const FString LandText = DebugFrame.LandImpactTime >= 0.f ? FString::Printf(TEXT("   land impact @ %.3f s"), DebugFrame.LandImpactTime) : FString();
	const FString LocomotionText = FString::Printf(TEXT("anim locomotion   %s   gait %s   dir %s   %s @ %.2f s   rate %.2f   stride %.2f   warp %+.0f deg   root yaw %+.0f deg   lean %+.2f (sprint %.2f)%s%s"),
		*StaticEnum<EGinkgoLocomotionState>()->GetNameStringByValue(static_cast<int64>(LocomotionState)),
		*StaticEnum<EGinkgoAnimGait>()->GetNameStringByValue(static_cast<int64>(Gait)), *StaticEnum<EGinkgoAnimDirection>()->GetNameStringByValue(static_cast<int64>(Direction)),
		DebugFrame.Sequence ? *DebugFrame.Sequence->GetName() : TEXT("(no clip)"), DebugFrame.SequenceTime, DebugFrame.PlayRate, StrideScale, OrientationWarpingAngle,
		RootYawOffset, LeanAmount, LeanSprintWeight, *AirText, *LandText);
	GEngine->AddOnScreenDebugMessage(ScreenMessageKey(*this, 0), 0.f, FColor(150, 255, 150), MotionText);
	GEngine->AddOnScreenDebugMessage(ScreenMessageKey(*this, 1), 0.f, FColor(255, 230, 120), IntentText);
	GEngine->AddOnScreenDebugMessage(ScreenMessageKey(*this, 2), 0.f, FColor(150, 210, 255), LocomotionText);

	// 持枪：姿态、三个进度（线性的）、层里三个持枪循环的权重、给瞄准偏移的两个角、手上的枪与它的层
	const FGinkgoHoldState& Hold = Snapshot.Hold;
	const UClass* LayerClass = Hold.Weapon ? Hold.Weapon->AnimLayerClass.Get() : nullptr;
	const FString HoldText = FString::Printf(TEXT("anim hold   %s   raise %.2f   ads %.2f   sprint %.2f   hip hold %.2f s   loops run %.2f sprint %.2f aim %.2f   aim yaw %+.0f pitch %+.0f deg   weapon %s   layer %s"),
		*StaticEnum<EGinkgoHoldStance>()->GetNameStringByValue(static_cast<int64>(Hold.Stance)), Hold.RaiseProgress, Hold.AdsProgress, Hold.SprintProgress,
		Hold.HipHoldRemaining, DebugFrame.WeaponLayer.RunAlpha, DebugFrame.WeaponLayer.SprintAlpha, DebugFrame.WeaponLayer.AimAlpha, WeaponAnim.AimYaw, WeaponAnim.AimPitch,
		Hold.Weapon ? *Hold.Weapon->GetName() : TEXT("-"), LayerClass ? *LayerClass->GetName() : TEXT("(self)"));
	GEngine->AddOnScreenDebugMessage(ScreenMessageKey(*this, 4), 0.f, FColor(255, 200, 150), HoldText);

	// 动画动作：在跑的叶子动作与它的蒙太奇放到哪里，两只手的 IK 绑定（插槽，冻结了没有），持有的相机标签，武器挂在哪根骨头上
	const AGinkgoCharacter* Character = Cast<AGinkgoCharacter>(TryGetPawnOwner());
	const UGinkgoAnimActionComponent* Actions = Character ? Character->GetAnimActions() : nullptr;
	const FGinkgoAnimActionRunner* Runner = Actions ? Actions->GetRunner() : nullptr;
	if (!Runner)
	{
		return;
	}
	const FString Leaves = FString::JoinBy(Runner->GetRunningLeaves(), TEXT("  "), [this](const UGinkgoAnimAction* Leaf)
	{
		return FString::Printf(TEXT("%s @ %.2f s"), *Leaf->GetActionName().ToString(), Leaf->GetMontage() ? Montage_GetPosition(Leaf->GetMontage()) : 0.f);
	});
	const auto HandText = [Runner](const EGinkgoHand Hand)
	{
		const FGinkgoAnimActionRunner::FHandHold* Hold = Runner->GetHandIK(Hand);
		return !Hold ? FString(TEXT("-"))
			: FString::Printf(TEXT("%s%s"), Hold->Binding.Socket.IsNone() ? TEXT("fixed") : *Hold->Binding.Socket.ToString(), Hold->bFrozen ? TEXT(" (frozen)") : TEXT(""));
	};
	const UPoseableMeshComponent* Weapon = Character->GetWeapon();
	const FString ActionText = FString::Printf(TEXT("anim action   %s   hand IK L %s  R %s   camera %s   weapon on %s"),
		Leaves.IsEmpty() ? TEXT("(none)") : *Leaves, *HandText(EGinkgoHand::Left), *HandText(EGinkgoHand::Right), *Runner->GetCameraTags().ToStringSimple(),
		Weapon && Weapon->GetSkinnedAsset() ? *Weapon->GetAttachSocketName().ToString() : TEXT("-"));
	GEngine->AddOnScreenDebugMessage(ScreenMessageKey(*this, 3), 0.f, FColor(255, 170, 220), ActionText);
	if (const UGinkgoWeaponComponent* Weapons = Character->GetWeapons())
	{
		GEngine->AddOnScreenDebugMessage(ScreenMessageKey(*this, 4), 0.f, FColor(255, 170, 220), FString::Printf(TEXT("weapon state  %s   held %d  target %d  last %d   occupancy %s   hip lag %.2f %.2f"),
			*Weapons->GetActionStatePath(), Weapons->GetHeldSlot(), Weapons->GetTargetSlot(), Weapons->GetLastHeldSlot(), *Runner->GetOccupancyText(),
			Weapons->GetHipAimLag().X, Weapons->GetHipAimLag().Y));
	}
}

#endif // !UE_BUILD_SHIPPING
