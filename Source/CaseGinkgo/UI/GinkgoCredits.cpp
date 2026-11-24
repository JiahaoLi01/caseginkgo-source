#include "UI/GinkgoCredits.h"

#include "Internationalization/BreakIterator.h"

namespace
{
	float NonNegative(const float Value, const float Default)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.f, Value) : Default;
	}

	void AppendLine(const FText& Source, int32 Row, bool bItalic, bool bUppercase, TArray<FGinkgoCreditGlyph>& Glyphs)
	{
		FString Text = Source.ToString();
		if (bUppercase) { Text.ToUpperInline(); }
		// 每条内容独占一行，字段中粘贴的换行或制表符只作为行内空格。
		Text.ReplaceInline(TEXT("\r"), TEXT(" "));
		Text.ReplaceInline(TEXT("\n"), TEXT(" "));
		Text.ReplaceInline(TEXT("\t"), TEXT(" "));
		auto Breaks = FBreakIterator::CreateCharacterBoundaryIterator();
		Breaks->SetString(Text);
		int32 Begin = Breaks->ResetToBeginning();
		for (int32 End = Breaks->MoveToNext(); End != INDEX_NONE; Begin = End, End = Breaks->MoveToNext())
		{
			Glyphs.Add({Text.Mid(Begin, End - Begin), 0.f, Row, bItalic});
		}
	}
}

int32 FGinkgoCreditsQueue::Push(const TArray<FGinkgoCredit>& Credits, const FGinkgoCreditsSettings& Settings, const int32 Seed)
{
	FRandomStream Random(Seed);
	int32 Added = 0;
	for (const auto& Credit : Credits)
	{
		if (!Credit.HasContent()) { continue; }
		FEntry Entry;
		Entry.Settings = Settings;
		Entry.Settings.Stagger = NonNegative(Settings.Stagger, .55f);
		Entry.Settings.Reveal = NonNegative(Settings.Reveal, .6f);
		Entry.Settings.Hold = NonNegative(Settings.Hold, 5.f);
		Entry.Settings.FadeOut = NonNegative(Settings.FadeOut, .7f);
		Entry.Settings.Blur = FMath::Min(32.f, NonNegative(Settings.Blur, 10.f));
		AppendLine(Credit.Role, 0, false, Settings.bUppercase, Entry.Glyphs);
		int32 NextRow = 1;
		auto AppendContributor = [&](const FText& Name, bool bItalic)
		{
			if (!Name.IsEmptyOrWhitespace())
			{
				AppendLine(Name, NextRow++, bItalic, Settings.bUppercase, Entry.Glyphs);
			}
		};
		AppendContributor(Credit.Name, Credit.bItalic);
		for (const auto& Contributor : Credit.AdditionalContributors)
		{
			AppendContributor(Contributor.Name, Contributor.bItalic);
		}
		TArray<int32> Order;
		for (int32 Index = 0; Index < Entry.Glyphs.Num(); ++Index)
		{
			if (!Entry.Glyphs[Index].Text.TrimStartAndEnd().IsEmpty()) { Order.Add(Index); }
		}
		for (int32 Index = Order.Num() - 1; Index > 0; --Index) { Order.Swap(Index, Random.RandRange(0, Index)); }
		for (int32 Rank = 0; Rank < Order.Num(); ++Rank)
		{
			const float Jitter = Rank > 0 && Rank < Order.Num() - 1 ? Random.FRandRange(-.25f, .25f) : 0.f;
			const float Delay = Entry.Settings.Stagger * (Rank + Jitter) / FMath::Max(1, Order.Num() - 1);
			Entry.Glyphs[Order[Rank]].Delay = Delay;
			Entry.RevealEnd = FMath::Max(Entry.RevealEnd, Delay + Entry.Settings.Reveal);
		}
		Pending.Add(MoveTemp(Entry));
		++Added;
	}
	return Added;
}

FGinkgoCreditsQueue::EStep FGinkgoCreditsQueue::Advance(const float DeltaSeconds)
{
	if (!Current)
	{
		if (Pending.IsEmpty()) { return EStep::None; }
		Current = MoveTemp(Pending[0]);
		Pending.RemoveAt(0);
		Elapsed = 0.f;
		return EStep::Begin;
	}
	Elapsed += NonNegative(DeltaSeconds, 0.f);
	if (Elapsed >= Current->RevealEnd + Current->Settings.Hold + Current->Settings.FadeOut)
	{
		Current.Reset();
		return EStep::Finish;
	}
	return EStep::None;
}

const TArray<FGinkgoCreditGlyph>& FGinkgoCreditsQueue::GetGlyphs() const
{
	static const TArray<FGinkgoCreditGlyph> Empty;
	return Current ? Current->Glyphs : Empty;
}

const FGinkgoCreditsSettings* FGinkgoCreditsQueue::GetSettings() const { return Current ? &Current->Settings : nullptr; }

float FGinkgoCreditsQueue::GetGlyphAlpha(const int32 Index) const
{
	if (!Current || !Current->Glyphs.IsValidIndex(Index)) { return 0.f; }
	const float Local = Elapsed - Current->Glyphs[Index].Delay;
	if (Local < 0.f) { return 0.f; }
	return Current->Settings.Reveal > 0.f ? FMath::SmoothStep(0.f, 1.f, FMath::Clamp(Local / Current->Settings.Reveal, 0.f, 1.f)) : 1.f;
}

float FGinkgoCreditsQueue::GetOpacity() const
{
	if (!Current) { return 0.f; }
	const float Local = Elapsed - Current->RevealEnd - Current->Settings.Hold;
	if (Local <= 0.f) { return 1.f; }
	return Current->Settings.FadeOut > 0.f ? 1.f - FMath::SmoothStep(0.f, 1.f, FMath::Clamp(Local / Current->Settings.FadeOut, 0.f, 1.f)) : 0.f;
}
