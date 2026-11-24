#include "GinkgoCharacterMovementComponent.h"

#if !UE_BUILD_SHIPPING

#include "CanvasItem.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GinkgoMovementConfig.h"
#include "GinkgoMovementMath.h"  // CentimetersPerSourceUnit

namespace
{
	TAutoConsoleVariable<int32> CVarShowMovementInfo(
		TEXT("ginkgo.showMovementInfo"),
		0,
		TEXT("显示移动信息：0 关，1 开。\n")
		TEXT("脚下画出水平速度（绿，每 1 m/s 长 10 cm）与 wishdir（黄），屏幕上显示水平速度、竖直速度、移动模式、冲刺、滑铲（已滑时间与起步加速）与落地计时。"));

	TAutoConsoleVariable<int32> CVarShowMovementCurve(
		TEXT("ginkgo.showMovementCurve"),
		0,
		TEXT("显示水平速度的滚动曲线：0 关，1 开。\n")
		TEXT("最近 6 秒，单位 u/s，白色为地面、品红为滑铲、青色为空中、黄色为自定义模式，参考线是跑速与冲刺速度。用来检查动量是否在能力之间传递。"));

	TAutoConsoleVariable<int32> CVarShowMovementWindows(
		TEXT("ginkgo.showMovementWindows"),
		0,
		TEXT("显示移动窗口：0 关，1 开。\n")
		TEXT("屏幕右侧列出活动的窗口（阶段、已过时间与时长、参数）与汇总标签，以及最近几次发给表现层的变化（推入、换阶段、刷新、结束）；")
		TEXT("另给出变化的累计次数、发给表现层的次数与客户端重放时丢掉的次数。"));

	/** 窗口面板里最近的变化列几条 */
	constexpr int32 MaxRecentWindowChanges = 6;

	void DrawTile(UCanvas& Canvas, const FVector2D& Origin, const FVector2D& Size, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(Origin, Size, Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(Tile);
	}

	/** 推入、结束，或进入哪个阶段；再次推入刷新也是进入阶段 0 */
	FString DescribeWindowChange(const int32 FromPhase, const int32 ToPhase)
	{
		if (FromPhase == INDEX_NONE)
		{
			return TEXT("pushed");
		}
		if (ToPhase == INDEX_NONE)
		{
			return FString::Printf(TEXT("ended in phase %d"), FromPhase);
		}
		return FString::Printf(TEXT("phase %d -> %d"), FromPhase, ToPhase);
	}

	/** 曲线里滑铲的颜色：与冲刺参考线的橙色分开 */
	const FColor SlideColor = FColor::Magenta;

	FColor ColorForMovementMode(const EMovementMode Mode)
	{
		switch (Mode)
		{
		case MOVE_Walking:
			[[fallthrough]];
		case MOVE_NavWalking:
			return FColor::White;
		case MOVE_Falling:
			return FColor::Cyan;
		case MOVE_Custom:
			return FColor::Yellow;
		default:
			return FColor(128, 128, 128);
		}
	}
}

void UGinkgoCharacterMovementComponent::TickMovementDebug()
{
	if (!CharacterOwner || !CharacterOwner->IsLocallyControlled())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.f);
	const float HorizontalSpeed = HorizontalVelocity.Size();

	if (CVarShowMovementCurve.GetValueOnGameThread() != 0)
	{
		SpeedCurve.AddSample(World->GetTimeSeconds(), HorizontalSpeed / Ginkgo::Movement::CentimetersPerSourceUnit, SimState.bSliding ? SlideColor : ColorForMovementMode(MovementMode));
	}

	if (CVarShowMovementInfo.GetValueOnGameThread() == 0 || !GEngine)
	{
		return;
	}
	const FVector Feet = CharacterOwner->GetActorLocation() - FVector(0.f, 0.f, CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 2.f);
	DrawDebugDirectionalArrow(World, Feet, Feet + HorizontalVelocity * 0.1f, 20.f, FColor::Green, false, -1.f, 0, 2.f);
	if (!Acceleration.IsNearlyZero())
	{
		DrawDebugDirectionalArrow(World, Feet, Feet + Acceleration.GetSafeNormal2D() * 100.f, 20.f, FColor::Yellow, false, -1.f, 0, 2.f);
	}

	// 屏幕文字用英文：引擎的屏幕调试字体没有中文字形
	const FString Slide = SimState.bSliding ? FString::Printf(TEXT("  slide %.2f s +%.0f cm/s"), SimState.SlideTime, SimState.SlideBoost) : FString();
	const FString Text = FString::Printf(TEXT("speed %.2f m/s (%.0f u/s)   vz %.2f m/s   %s%s%s%s   since landed %.2f s   corrections %d   combined %d"),
		HorizontalSpeed / 100.f, HorizontalSpeed / Ginkgo::Movement::CentimetersPerSourceUnit, Velocity.Z / 100.f,
		*GetMovementName(), bWantsToSprint ? TEXT("  sprint") : TEXT(""), IsCrouching() ? TEXT("  crouch") : TEXT(""), *Slide, SimState.TimeSinceLanded, CorrectionCount,
		CombinedMoveCount);
	GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.f, FColor::White, Text);
}

void UGinkgoCharacterMovementComponent::DrawMovementDebug(UCanvas* Canvas, APlayerController* PlayerController) const
{
	if (!Canvas || !CharacterOwner || CharacterOwner->GetController() != PlayerController)
	{
		return;
	}
	if (CVarShowMovementCurve.GetValueOnGameThread() != 0)
	{
		DrawMovementCurve(*Canvas);
	}
	if (CVarShowMovementWindows.GetValueOnGameThread() != 0)
	{
		DrawMovementWindows(*Canvas);
	}
}

void UGinkgoCharacterMovementComponent::DrawMovementCurve(UCanvas& Canvas) const
{
	const UGinkgoMovementConfig& Config = GetConfig();
	const TArray<FDebugRollingCurve::FReference> References = {
		{Config.RunSpeed / Ginkgo::Movement::CentimetersPerSourceUnit, FColor(160, 160, 160), TEXT("run")},
		{Config.SprintSpeed / Ginkgo::Movement::CentimetersPerSourceUnit, FColor(255, 170, 60), TEXT("sprint")},
	};
	const float HorizontalSpeed = FVector(Velocity.X, Velocity.Y, 0.f).Size();
	const FString Title = FString::Printf(TEXT("horizontal speed (u/s)   now %.0f u/s (%.2f m/s)   white ground / magenta slide / cyan air / yellow custom"),
		HorizontalSpeed / Ginkgo::Movement::CentimetersPerSourceUnit, HorizontalSpeed / 100.f);
	const FVector2D Size(FMath::Min(640.f, Canvas.ClipX - 80.f), 160.f);
	SpeedCurve.Draw(Canvas, FVector2D(40.f, Canvas.ClipY - Size.Y - 60.f), Size, Title, References);
}

void UGinkgoCharacterMovementComponent::DrawMovementWindows(UCanvas& Canvas) const
{
	constexpr float LineHeight = 16.f;
	constexpr float BarWidth = 60.f;
	const FGinkgoMovementWindowManager::FDefinitions Definitions = GetWindowDefinitions();
	const int32 WindowLines = FMath::Max(SimState.Windows.Num(), 1);
	const int32 LineCount = 2 + WindowLines + 1 + FMath::Max(RecentWindowChanges.Num(), 1);
	// 放在右侧、屏幕高度的 30% 处：避开左上角引擎的屏幕消息（ginkgo.showMovementInfo 那一行很长）与左下角的速度曲线
	const FVector2D Size(FMath::Min(620.f, Canvas.ClipX - 80.f), LineCount * LineHeight + 12.f);
	const FVector2D Origin(Canvas.ClipX - Size.X - 40.f, Canvas.ClipY * 0.3f);
	DrawTile(Canvas, Origin, Size, FLinearColor(0.f, 0.f, 0.f, 0.45f));

	UFont* Font = GEngine->GetSmallFont();
	const float TextX = Origin.X + 6.f;
	float Y = Origin.Y + 6.f;
	const auto DrawTextLine = [&](const FString& Text, const FColor Color, const float Indent = 0.f)
	{
		Canvas.SetDrawColor(Color);
		Canvas.DrawText(Font, Text, TextX + Indent, Y);
		Y += LineHeight;
	};

	// 屏幕文字用英文：引擎的屏幕调试字体没有中文字形
	DrawTextLine(FString::Printf(TEXT("movement windows   %d / %d active, %d defined   changes %u   dispatched %d   dropped in replay %d"), SimState.Windows.Num(),
		FGinkgoMovementSimState::MaxWindows, Definitions.Num(), WindowManager.GetChangeCount(), DispatchedWindowChangeCount, ReplayedWindowChangeCount), FColor::White);
	TStringBuilder<256> TagText;
	for (const FGameplayTag& Tag : WindowManager.GetTags())
	{
		TagText.Appendf(TEXT("%s%s"), TagText.Len() > 0 ? TEXT("  ") : TEXT(""), *Tag.ToString());
	}
	DrawTextLine(FString::Printf(TEXT("tags   %s"), TagText.Len() > 0 ? TagText.ToString() : TEXT("(none)")), FColor(160, 220, 255));

	if (SimState.Windows.IsEmpty())
	{
		DrawTextLine(TEXT("  (no active window)"), FColor(160, 160, 160));
	}
	for (const FGinkgoMovementWindowState& Window : SimState.Windows)
	{
		const UGinkgoMovementWindow* Definition = Definitions.IsValidIndex(Window.Definition) ? Definitions[Window.Definition].Get() : nullptr;
		const FGinkgoWindowPhase* Phase = Definition && Definition->Phases.IsValidIndex(Window.Phase) ? &Definition->Phases[Window.Phase] : nullptr;
		if (!Phase)
		{
			DrawTextLine(FString::Printf(TEXT("  #%d phase %d: definition missing"), Window.Definition, Window.Phase), FColor::Red);
			continue;
		}
		// 进度条在行首自己的一栏里，不定长的阶段没有进度条
		const bool bTimed = Phase->Duration > 0.f;
		const FString Timing = bTimed ? FString::Printf(TEXT("%.3f / %.3f s"), Window.PhaseTime, Phase->Duration) : FString::Printf(TEXT("%.3f s, until event"), Window.PhaseTime);
		const float BarX = TextX + 8.f;
		if (bTimed)
		{
			DrawTile(Canvas, FVector2D(BarX, Y + 3.f), FVector2D(BarWidth, LineHeight - 6.f), FLinearColor(0.3f, 0.3f, 0.3f, 0.8f));
			DrawTile(Canvas, FVector2D(BarX, Y + 3.f), FVector2D(BarWidth * FMath::Clamp(Window.PhaseTime / Phase->Duration, 0.f, 1.f), LineHeight - 6.f),
				FLinearColor(1.f, 0.85f, 0.2f, 0.9f));
		}
		DrawTextLine(FString::Printf(TEXT("%s   phase %d %s   %s   param %.1f"), *Definition->Tag.ToString(), Window.Phase,
			Phase->Tag.IsValid() ? *Phase->Tag.ToString() : TEXT("-"), *Timing, Window.Param), FColor::Yellow, BarWidth + 16.f);
	}

	DrawTextLine(TEXT("recent changes (first simulation only)"), FColor::White);
	if (RecentWindowChanges.IsEmpty())
	{
		DrawTextLine(TEXT("  (none)"), FColor(160, 160, 160));
	}
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = RecentWindowChanges.Num() - 1; Index >= 0; --Index)
	{
		const FWindowChangeRecord& Record = RecentWindowChanges[Index];
		DrawTextLine(FString::Printf(TEXT("  %6.2f s ago   %s   %s"), Now - Record.Time, Record.Tag.IsValid() ? *Record.Tag.ToString() : TEXT("?"),
			*DescribeWindowChange(Record.FromPhase, Record.ToPhase)), FColor(200, 200, 200));
	}
}

void UGinkgoCharacterMovementComponent::RecordWindowChangeForDebug(const UGinkgoMovementWindow* Definition, const FGinkgoMovementWindowManager::FChange& Change)
{
	++DispatchedWindowChangeCount;
	if (RecentWindowChanges.Num() >= MaxRecentWindowChanges)
	{
		RecentWindowChanges.RemoveAt(0, 1, EAllowShrinking::No);
	}
	RecentWindowChanges.Add({GetWorld()->GetTimeSeconds(), Definition ? Definition->Tag : FGameplayTag(), Change.FromPhase, Change.ToPhase});
}

#endif // !UE_BUILD_SHIPPING
