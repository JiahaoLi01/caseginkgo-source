#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/GinkgoCredits.h"

namespace GinkgoCreditsTests
{
	constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	FGinkgoCredit Make(const TCHAR* Role, const TCHAR* Name)
	{
		FGinkgoCredit Credit;
		Credit.Role = FText::FromString(Role);
		Credit.Name = FText::FromString(Name);
		return Credit;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCreditsOrderTest, "CaseGinkgo.UI.Credits.RandomOrderAndGraphemes", GinkgoCreditsTests::Flags)
bool FGinkgoCreditsOrderTest::RunTest(const FString& Parameters)
{
	FGinkgoCreditsSettings Settings;
	Settings.bUppercase = false;
	const auto Credit = GinkgoCreditsTests::Make(TEXT(""), TEXT("ABCD E\u0301\U0001F680"));
	FGinkgoCreditsQueue First;
	FGinkgoCreditsQueue Same;
	FGinkgoCreditsQueue Other;
	First.Push({Credit}, Settings, 71);
	Same.Push({Credit}, Settings, 71);
	Other.Push({Credit}, Settings, 92);
	First.Advance(0.f);
	Same.Advance(0.f);
	Other.Advance(0.f);
	const auto& Glyphs = First.GetGlyphs();
	if (!TestEqual(TEXT("组合音标和代理对各保留为一个字素"), Glyphs.Num(), 7)) { return false; }
	FString Text;
	bool bDifferent = false;
	bool bNotReadingOrder = false;
	float FirstStart = BIG_NUMBER;
	float LastStart = 0.f;
	for (int32 Index = 0; Index < Glyphs.Num(); ++Index)
	{
		Text += Glyphs[Index].Text;
		TestEqual(TEXT("相同随机种子可重现"), Glyphs[Index].Delay, Same.GetGlyphs()[Index].Delay);
		bDifferent |= Glyphs[Index].Delay != Other.GetGlyphs()[Index].Delay;
		if (Glyphs[Index].Text.TrimStartAndEnd().IsEmpty()) { continue; }
		bNotReadingOrder |= Glyphs[Index].Delay < LastStart;
		FirstStart = FMath::Min(FirstStart, Glyphs[Index].Delay);
		LastStart = FMath::Max(LastStart, Glyphs[Index].Delay);
	}
	TestEqual(TEXT("文字位置和最终内容不乱序"), Text, Credit.Name.ToString());
	TestTrue(TEXT("不同种子产生不同显现顺序"), bDifferent);
	TestTrue(TEXT("显现不是从左到右打字"), bNotReadingOrder);
	TestEqual(TEXT("首个字符从零时刻开始"), FirstStart, 0.f);
	TestEqual(TEXT("最后字符在配置的错开区间内开始"), LastStart, Settings.Stagger);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCreditsTimingTest, "CaseGinkgo.UI.Credits.RevealHoldAndUnifiedFade", GinkgoCreditsTests::Flags)
bool FGinkgoCreditsTimingTest::RunTest(const FString& Parameters)
{
	FGinkgoCreditsSettings Settings;
	Settings.Stagger = .5f;
	Settings.Reveal = .5f;
	Settings.Hold = 2.f;
	Settings.FadeOut = 1.f;
	FGinkgoCreditsQueue Queue;
	Queue.Push({FGinkgoCredit()}, Settings, 71);
	TestTrue(TEXT("入队立即活跃"), Queue.IsActive());
	TestTrue(TEXT("首次推进开始第一条"), Queue.Advance(0.f) == FGinkgoCreditsQueue::EStep::Begin);
	TArray<float> Last;
	Last.Init(0.f, Queue.GetGlyphs().Num());
	for (int32 Frame = 0; Frame < 11; ++Frame)
	{
		for (int32 Index = 0; Index < Last.Num(); ++Index)
		{
			const float Alpha = Queue.GetGlyphAlpha(Index);
			TestTrue(TEXT("逐字透明度单调增加，模糊强度单调减少"), Alpha >= Last[Index] && Alpha <= 1.f);
			Last[Index] = Alpha;
		}
		Queue.Advance(.1f);
	}
	for (const float Alpha : Last) { TestTrue(TEXT("最后字符显现完毕"), FMath::IsNearlyEqual(Alpha, 1.f)); }
	Queue.Advance(1.8f);
	TestEqual(TEXT("Hold 从最后字符清晰后计算"), Queue.GetOpacity(), 1.f);
	Queue.Advance(.6f);
	TestTrue(TEXT("两行在同一个父透明度下淡出"), FMath::IsNearlyEqual(Queue.GetOpacity(), .5f));
	for (int32 Index = 0; Index < Last.Num(); ++Index) { TestEqual(TEXT("淡出不再次错开或变模糊"), Queue.GetGlyphAlpha(Index), 1.f); }
	TestTrue(TEXT("最后一条完成"), Queue.Advance(.6f) == FGinkgoCreditsQueue::EStep::Finish);
	TestFalse(TEXT("没有内容就停止"), Queue.IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCreditsQueueTest, "CaseGinkgo.UI.Credits.QueueZeroTimesAndSnapshot", GinkgoCreditsTests::Flags)
bool FGinkgoCreditsQueueTest::RunTest(const FString& Parameters)
{
	FGinkgoCreditsSettings Settings;
	Settings.Stagger = Settings.Reveal = Settings.Hold = Settings.FadeOut = 0.f;
	Settings.Blur = 0.f;
	FGinkgoCreditsQueue Queue;
	TestEqual(TEXT("空姓名不入队"), Queue.Push({GinkgoCreditsTests::Make(TEXT("AUTHOR"), TEXT(" \n"))}, Settings, 0), 0);
	TestFalse(TEXT("无效内容不创建播放任务"), Queue.IsActive());
	Queue.Push({GinkgoCreditsTests::Make(TEXT("Role"), TEXT("First")), GinkgoCreditsTests::Make(TEXT("Role"), TEXT("Second"))}, Settings, 0);
	Settings.Hold = 100.f;
	Queue.Advance(0.f);
	TestEqual(TEXT("项目设置在触发时固定快照"), Queue.GetSettings()->Hold, 0.f);
	TestEqual(TEXT("零显现时间立即清晰"), Queue.GetGlyphAlpha(0), 1.f);
	TestTrue(TEXT("全零时长也能正常结束"), Queue.Advance(0.f) == FGinkgoCreditsQueue::EStep::Finish);
	TestTrue(TEXT("间隙仍有下一条"), Queue.IsActive());
	Queue.Advance(0.f);
	FString Name;
	for (const auto& Glyph : Queue.GetGlyphs()) { if (Glyph.Row == 1) { Name += Glyph.Text; } }
	TestEqual(TEXT("第二条保持入队顺序并应用大写"), Name, FString(TEXT("SECOND")));
	Queue.Advance(0.f);
	TestFalse(TEXT("第二条完成即空队列"), Queue.IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGinkgoCreditsGroupTest, "CaseGinkgo.UI.Credits.ContributorsAndItalicWorks", GinkgoCreditsTests::Flags)
bool FGinkgoCreditsGroupTest::RunTest(const FString& Parameters)
{
	auto Credit = GinkgoCreditsTests::Make(TEXT("Sources"), TEXT("First work"));
	Credit.bItalic = true;
	Credit.AdditionalContributors = {
		{FText::FromString(TEXT("  ")), true},
		{FText::FromString(TEXT("Jane Doe")), false},
		{FText::FromString(TEXT("Second work")), true}
	};
	FGinkgoCreditsQueue Queue;
	TestEqual(TEXT("同组作者与作品只占一个播放时段"), Queue.Push({Credit}, FGinkgoCreditsSettings(), 42), 1);
	Queue.Advance(0.f);
	TArray<FString> Rows;
	Rows.SetNum(4);
	for (const auto& Glyph : Queue.GetGlyphs())
	{
		if (!TestTrue(TEXT("跳过空条目后行号连续"), Rows.IsValidIndex(Glyph.Row))) { return false; }
		Rows[Glyph.Row] += Glyph.Text;
		TestEqual(TEXT("每行的所有字素保留独立字面"), Glyph.bItalic, Glyph.Row == 1 || Glyph.Row == 3);
	}
	TestEqual(TEXT("职务只显示一次"), Rows[0], FString(TEXT("SOURCES")));
	TestEqual(TEXT("首个 Name 可用于斜体作品名"), Rows[1], FString(TEXT("FIRST WORK")));
	TestEqual(TEXT("正体作者保持顺序"), Rows[2], FString(TEXT("JANE DOE")));
	TestEqual(TEXT("下一部作品同屏显示"), Rows[3], FString(TEXT("SECOND WORK")));
	Queue.Advance(30.f);
	TestFalse(TEXT("统一淡出后没有单独排队的合作者"), Queue.IsActive());
	return true;
}

#endif
