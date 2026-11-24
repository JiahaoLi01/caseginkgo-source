#include "DebugRollingCurve.h"

#if !UE_BUILD_SHIPPING

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Utilities/FunctionLibrary/Range/UEQuery.h"

namespace
{
	/** 纵轴上限取 50 的整数倍，刻度不会每帧跳动 */
	float NiceCeiling(const float Value)
	{
		return FMath::Max(50.f, FMath::CeilToFloat(Value / 50.f) * 50.f);
	}

	/** 正负对称时纵轴的上限：1、2、5 乘 10 的幂，刻度不会每帧跳动 */
	float NiceSymmetricCeiling(const float Value)
	{
		if (Value <= UE_KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}
		const float Power = FMath::Pow(10.f, FMath::FloorToFloat(FMath::LogX(10.f, Value)));
		for (const float Step : {1.f, 2.f, 5.f, 10.f})
		{
			if (Value <= Step * Power)
			{
				return Step * Power;
			}
		}
		return 10.f * Power;
	}

	void DrawLine(UCanvas& Canvas, const FVector2D& From, const FVector2D& To, const FColor Color, const float Thickness)
	{
		FCanvasLineItem Line(From, To);
		Line.SetColor(FLinearColor(Color));
		Line.LineThickness = Thickness;
		Canvas.DrawItem(Line);
	}
}

void FDebugRollingCurve::AddSample(const double Time, const float Value, const FColor Color)
{
	Samples.Add({Time, Value, Color});
	int32 FirstKept = 0;
	while (FirstKept < Samples.Num() && Samples[FirstKept].Time < Time - TimeWindow)
	{
		++FirstKept;
	}
	if (FirstKept > 0)
	{
		Samples.RemoveAt(0, FirstKept, EAllowShrinking::No);
	}
}

void FDebugRollingCurve::Draw(UCanvas& Canvas, const FVector2D& Origin, const FVector2D& Size, const FString& Title, const TArray<FReference>& References,
	const bool bSigned) const
{
	using namespace UE::Query;

	FCanvasTileItem Background(Origin, Size, FLinearColor(0.f, 0.f, 0.f, 0.45f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas.DrawItem(Background);

	const auto Max = [](const float A, const float B) { return FMath::Max(A, B); };
	const auto Magnitude = [bSigned](const float Value) { return bSigned ? FMath::Abs(Value) : Value; };
	const float SampleMax = Samples | Select([&Magnitude](const FSample& Sample) { return Magnitude(Sample.Value); }) | Fold(0.f, Max);
	const float ReferenceMax = References | Select([&Magnitude](const FReference& Reference) { return Magnitude(Reference.Value); }) | Fold(0.f, Max);
	const float Top = bSigned ? NiceSymmetricCeiling(FMath::Max(SampleMax, ReferenceMax) * 1.1f) : NiceCeiling(FMath::Max(SampleMax, ReferenceMax) * 1.1f);
	const float Bottom = bSigned ? -Top : 0.f;

	const double Now = Samples.IsEmpty() ? 0.0 : Samples.Last().Time;
	const auto ToScreen = [&](const double Time, const float Value)
	{
		return FVector2D(Origin.X + Size.X * (1.0 - (Now - Time) / TimeWindow), Origin.Y + Size.Y * (Top - Value) / (Top - Bottom));
	};
	if (bSigned)
	{
		const double Zero = ToScreen(Now, 0.f).Y;
		DrawLine(Canvas, FVector2D(Origin.X, Zero), FVector2D(Origin.X + Size.X, Zero), FColor(90, 90, 90), 1.f);
	}

	UFont* Font = GEngine->GetSmallFont();
	for (const FReference& Reference : References)
	{
		const double Y = ToScreen(Now, Reference.Value).Y;
		DrawLine(Canvas, FVector2D(Origin.X, Y), FVector2D(Origin.X + Size.X, Y), Reference.Color, 1.f);
		Canvas.SetDrawColor(Reference.Color);
		Canvas.DrawText(Font, Reference.Label, Origin.X + 6.f, Y - 14.f);
	}

	for (int32 Index = 1; Index < Samples.Num(); ++Index)
	{
		const FSample& From = Samples[Index - 1];
		const FSample& To = Samples[Index];
		DrawLine(Canvas, ToScreen(From.Time, From.Value), ToScreen(To.Time, To.Value), To.Color, 2.f);
	}

	Canvas.SetDrawColor(FColor::White);
	Canvas.DrawText(Font, Title, Origin.X + 6.f, Origin.Y + 4.f);
	Canvas.DrawText(Font, bSigned ? FString::Printf(TEXT("±%g"), Top) : FString::Printf(TEXT("%.0f"), Top), Origin.X + Size.X - 40.f, Origin.Y + 4.f);
	Canvas.DrawText(Font, FString::Printf(TEXT("-%.0f s"), TimeWindow), Origin.X + 6.f, Origin.Y + Size.Y - 16.f);
}

#endif // !UE_BUILD_SHIPPING
