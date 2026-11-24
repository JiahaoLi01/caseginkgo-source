// Fill out your copyright notice in the Description page of Project Settings.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Utilities/FunctionLibrary/Range/UEQuery.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * @brief Acceptance tests for the UEQuery pipe-style lazy query library.
 *
 * Covers the 7 scenarios listed in the design task:
 *   1) basic Where + Select + ToArray chain
 *   2) OrderBy by struct projection
 *   3) rvalue source safety (no dangling)
 *   4) composed-closure reuse across multiple inputs
 *   5) TMap adaptation via TPair element type
 *   6) short-circuit Any terminal
 *   7) compile-time diagnostics via static_assert (documented, exercised by static_assert sites)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryBasicChainTest, "CaseGinkgo.Utilities.UEQuery.BasicChain",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryBasicChainTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TArray<int32> Numbers = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	TArray<int32> Result = Numbers
		| Where([](int32 N){ return N % 2 == 0; })
		| Select([](int32 N){ return N * 10; })
		| ToArray();

	TArray<int32> Expected = {20, 40, 60, 80, 100};
	TestEqual(TEXT("基础链式元素个数"), Result.Num(), Expected.Num());
	for (int32 i = 0; i < Expected.Num() && i < Result.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("元素 %d"), i), Result[i], Expected[i]);
	}
	return true;
}

namespace
{
	struct FUEQueryTestItem
	{
		FString Name;
		int32 Value = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryOrderByTest, "CaseGinkgo.Utilities.UEQuery.OrderBy",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryOrderByTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TArray<FUEQueryTestItem> Items = {{TEXT("C"), 3}, {TEXT("A"), 1}, {TEXT("B"), 2}};
	TArray<FUEQueryTestItem> Sorted = Items
		| OrderBy([](const FUEQueryTestItem& I){ return I.Name; })
		| ToArray();

	TestEqual(TEXT("OrderBy 后元素个数"), Sorted.Num(), 3);
	if (Sorted.Num() == 3)
	{
		TestEqual(TEXT("[0] Name"), Sorted[0].Name, FString(TEXT("A")));
		TestEqual(TEXT("[1] Name"), Sorted[1].Name, FString(TEXT("B")));
		TestEqual(TEXT("[2] Name"), Sorted[2].Name, FString(TEXT("C")));
	}

	// Descending variant sanity check.
	TArray<FUEQueryTestItem> Desc = Items
		| OrderByDescending([](const FUEQueryTestItem& I){ return I.Value; })
		| ToArray();
	TestEqual(TEXT("OrderByDescending [0]"), Desc.Num() > 0 ? Desc[0].Value : -1, 3);
	TestEqual(TEXT("OrderByDescending [2]"), Desc.Num() > 2 ? Desc[2].Value : -1, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryRValueSafetyTest, "CaseGinkgo.Utilities.UEQuery.RValueSafety",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryRValueSafetyTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	auto Make = []{ return TArray<int32>{1, 2, 3, 4, 5}; };

	// Temporary source must be moved into an owning view — no dangling.
	TArray<int32> Result = Make()
		| Where([](int32 N){ return N > 2; })
		| ToArray();

	TArray<int32> Expected = {3, 4, 5};
	TestEqual(TEXT("右值源元素个数"), Result.Num(), Expected.Num());
	for (int32 i = 0; i < Expected.Num() && i < Result.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("元素 %d"), i), Result[i], Expected[i]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryComposedClosureTest, "CaseGinkgo.Utilities.UEQuery.ComposedClosure",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryComposedClosureTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	// A reusable pipeline built from closures alone — no source yet.
	auto RareAndSorted =
		  Where([](int32 V){ return V >= 10; })
		| OrderBy([](int32 V){ return V; });

	TArray<int32> A = {15, 3, 20, 7, 10};
	TArray<int32> B = {50, 1, 99, 4, 25};

	TArray<int32> ResA = A | RareAndSorted | ToArray();
	TArray<int32> ResB = B | RareAndSorted | ToArray();

	TArray<int32> ExpectedA = {10, 15, 20};
	TArray<int32> ExpectedB = {25, 50, 99};

	TestEqual(TEXT("Composed A 元素数"), ResA.Num(), ExpectedA.Num());
	for (int32 i = 0; i < ExpectedA.Num() && i < ResA.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("A 元素 %d"), i), ResA[i], ExpectedA[i]);
	}

	TestEqual(TEXT("Composed B 元素数"), ResB.Num(), ExpectedB.Num());
	for (int32 i = 0; i < ExpectedB.Num() && i < ResB.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("B 元素 %d"), i), ResB[i], ExpectedB[i]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryMapAdapterTest, "CaseGinkgo.Utilities.UEQuery.MapAdapter",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryMapAdapterTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TMap<FString, int32> Map;
	Map.Add(TEXT("a"), 1);
	Map.Add(TEXT("b"), 2);
	Map.Add(TEXT("c"), 3);

	TArray<FString> Keys = Map
		| Where([](const TPair<FString, int32>& P){ return P.Value > 1; })
		| Select([](const TPair<FString, int32>& P){ return P.Key; })
		| ToArray();

	// TMap iteration order is unspecified — just assert membership.
	TestEqual(TEXT("Map 过滤后个数"), Keys.Num(), 2);
	TestTrue(TEXT("包含 b"), Keys.Contains(FString(TEXT("b"))));
	TestTrue(TEXT("包含 c"), Keys.Contains(FString(TEXT("c"))));
	TestFalse(TEXT("不包含 a"), Keys.Contains(FString(TEXT("a"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryShortCircuitTest, "CaseGinkgo.Utilities.UEQuery.ShortCircuit",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryShortCircuitTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	constexpr int32 N = 1000;
	TArray<int32> Big;
	Big.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		Big.Add(i);
	}

	int32 CallCount = 0;
	bool bFound = Big
		| Where([&CallCount](int32){ ++CallCount; return true; })
		| Any([](int32 V){ return V == 5; });

	TestTrue(TEXT("Any 命中"), bFound);
	TestTrue(FString::Printf(TEXT("短路生效 (CallCount=%d < 1000)"), CallCount), CallCount < N);
	// With element 5 as the sixth input, exactly 6 predicate evaluations should occur.
	TestEqual(TEXT("短路精确次数"), CallCount, 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryMiscTerminalsTest, "CaseGinkgo.Utilities.UEQuery.MiscTerminals",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryMiscTerminalsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TArray<int32> Nums = {1, 2, 3, 4, 5};

	// Count on sized view short-circuits via Num().
	int32 C = Nums | Count();
	TestEqual(TEXT("Count 全量"), C, 5);

	// All / None
	bool bAllPositive = Nums | All([](int32 N){ return N > 0; });
	bool bNoneNegative = Nums | None([](int32 N){ return N < 0; });
	TestTrue(TEXT("All positive"), bAllPositive);
	TestTrue(TEXT("None negative"), bNoneNegative);

	// First / FirstOrDefault
	int32 F = Nums | Where([](int32 N){ return N > 3; }) | First();
	TestEqual(TEXT("First >3"), F, 4);

	TOptional<int32> Empty = TArray<int32>{} | FirstOrDefault();
	TestFalse(TEXT("FirstOrDefault 空"), Empty.IsSet());

	// Take / Drop / TakeWhile / DropWhile
	TArray<int32> Taken = Nums | Take(3) | ToArray();
	TestEqual(TEXT("Take(3) 元素数"), Taken.Num(), 3);
	if (Taken.Num() == 3)
	{
		TestEqual(TEXT("Take[0]"), Taken[0], 1);
		TestEqual(TEXT("Take[2]"), Taken[2], 3);
	}

	TArray<int32> Dropped = Nums | Drop(2) | ToArray();
	TestEqual(TEXT("Drop(2) 元素数"), Dropped.Num(), 3);
	if (Dropped.Num() == 3)
	{
		TestEqual(TEXT("Drop[0]"), Dropped[0], 3);
	}

	TArray<int32> TakenWhile = Nums | TakeWhile([](int32 N){ return N < 4; }) | ToArray();
	TestEqual(TEXT("TakeWhile <4 元素数"), TakenWhile.Num(), 3);

	TArray<int32> DroppedWhile = Nums | DropWhile([](int32 N){ return N < 3; }) | ToArray();
	TestEqual(TEXT("DropWhile <3 元素数"), DroppedWhile.Num(), 3);
	if (DroppedWhile.Num() == 3)
	{
		TestEqual(TEXT("DropWhile[0]"), DroppedWhile[0], 3);
	}

	// Reverse
	TArray<int32> Reversed = Nums | Reverse() | ToArray();
	TestEqual(TEXT("Reverse [0]"), Reversed.Num() > 0 ? Reversed[0] : -1, 5);
	TestEqual(TEXT("Reverse [4]"), Reversed.Num() > 4 ? Reversed[4] : -1, 1);

	// TSet adapter
	TSet<int32> Set = {10, 20, 30};
	int32 SetCount = Set | Where([](int32 V){ return V >= 20; }) | Count();
	TestEqual(TEXT("TSet 过滤后个数"), SetCount, 2);

	// ForEach
	int32 Sum = 0;
	Nums | ForEach([&Sum](int32 N){ Sum += N; });
	TestEqual(TEXT("ForEach 累加"), Sum, 15);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryFoldAggregateTest, "CaseGinkgo.Utilities.UEQuery.FoldAggregate",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryFoldAggregateTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TArray<int32> Nums = {1, 2, 3, 4, 5};
	int32 Sum = Nums | Fold(0, [](int32 Acc, int32 N){ return Acc + N; });
	TestEqual(TEXT("Fold 累加"), Sum, 15);

	// String fold — seed is FString, elements are projected to string
	FString Joined = Nums
		| Select([](int32 N){ return FString::FromInt(N); })
		| Fold(FString(), [](FString Acc, const FString& S)
		{
			if (!Acc.IsEmpty()) { Acc += TEXT(","); }
			Acc += S;
			return Acc;
		});
	TestEqual(TEXT("Fold 拼接"), Joined, FString(TEXT("1,2,3,4,5")));

	// Aggregate (seedless) — uses first element
	TOptional<int32> Product = Nums | Aggregate([](int32 A, int32 N){ return A * N; });
	TestTrue(TEXT("Aggregate 有值"), Product.IsSet());
	TestEqual(TEXT("Aggregate 乘积"), Product.Get(-1), 120);

	// Aggregate on empty
	TOptional<int32> Empty = TArray<int32>{} | Aggregate([](int32 A, int32 N){ return A + N; });
	TestFalse(TEXT("Aggregate 空"), Empty.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQuerySelectManyTest, "CaseGinkgo.Utilities.UEQuery.SelectMany",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQuerySelectManyTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	// Groups of numbers; flatten to one list.
	TArray<TArray<int32>> Groups = {{1, 2}, {3}, {}, {4, 5, 6}};
	TArray<int32> Flat = Groups
		| SelectMany([](const TArray<int32>& G){ return G; })
		| ToArray();

	TArray<int32> Expected = {1, 2, 3, 4, 5, 6};
	TestEqual(TEXT("SelectMany 长度"), Flat.Num(), Expected.Num());
	for (int32 i = 0; i < Expected.Num() && i < Flat.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("SelectMany[%d]"), i), Flat[i], Expected[i]);
	}

	// Flatten shorthand on the same groups.
	TArray<int32> Flat2 = Groups | Flatten() | ToArray();
	TestEqual(TEXT("Flatten 长度"), Flat2.Num(), Expected.Num());

	// SelectMany where Func returns by value (owned).
	TArray<int32> Counts = {2, 3};
	TArray<int32> Ids = Counts
		| SelectMany([](int32 N)
		{
			TArray<int32> Out;
			for (int32 i = 0; i < N; ++i) { Out.Add(i); }
			return Out;
		})
		| ToArray();
	TArray<int32> IdsExpected = {0, 1, 0, 1, 2};
	TestEqual(TEXT("SelectMany rvalue 长度"), Ids.Num(), IdsExpected.Num());
	for (int32 i = 0; i < IdsExpected.Num() && i < Ids.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("SelectMany rvalue [%d]"), i), Ids[i], IdsExpected[i]);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryDistinctTest, "CaseGinkgo.Utilities.UEQuery.Distinct",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryDistinctTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TArray<int32> Dupes = {1, 2, 2, 3, 1, 4, 3, 5};
	TArray<int32> Uniq = Dupes | Distinct() | ToArray();
	TArray<int32> Expected = {1, 2, 3, 4, 5};
	TestEqual(TEXT("Distinct 长度"), Uniq.Num(), Expected.Num());
	for (int32 i = 0; i < Expected.Num() && i < Uniq.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("Distinct[%d]"), i), Uniq[i], Expected[i]);
	}

	// DistinctBy — keep first occurrence per key
	struct FKV { int32 Key; int32 Value; };
	TArray<FKV> Items = {{1, 10}, {2, 20}, {1, 30}, {3, 40}, {2, 50}};
	TArray<FKV> DistinctKeys = Items
		| DistinctBy([](const FKV& I){ return I.Key; })
		| ToArray();
	TestEqual(TEXT("DistinctBy 长度"), DistinctKeys.Num(), 3);
	if (DistinctKeys.Num() == 3)
	{
		TestEqual(TEXT("DistinctBy [0].Value"), DistinctKeys[0].Value, 10);
		TestEqual(TEXT("DistinctBy [1].Value"), DistinctKeys[1].Value, 20);
		TestEqual(TEXT("DistinctBy [2].Value"), DistinctKeys[2].Value, 40);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryConcatTest, "CaseGinkgo.Utilities.UEQuery.Concat",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryConcatTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TArray<int32> A = {1, 2, 3};
	TArray<int32> B = {4, 5};

	// Lvalue container on both sides
	TArray<int32> AB = A | Concat(B) | ToArray();
	TArray<int32> Expected = {1, 2, 3, 4, 5};
	TestEqual(TEXT("Concat 长度"), AB.Num(), Expected.Num());
	for (int32 i = 0; i < Expected.Num() && i < AB.Num(); ++i)
	{
		TestEqual(FString::Printf(TEXT("Concat[%d]"), i), AB[i], Expected[i]);
	}

	// Rvalue container — moved into closure
	TArray<int32> AC = A | Concat(TArray<int32>{6, 7}) | ToArray();
	TestEqual(TEXT("Concat rvalue 长度"), AC.Num(), 5);
	if (AC.Num() == 5)
	{
		TestEqual(TEXT("Concat rvalue [3]"), AC[3], 6);
		TestEqual(TEXT("Concat rvalue [4]"), AC[4], 7);
	}

	// Num() propagation: Count should short-circuit via Num().
	int32 C = A | Concat(B) | Count();
	TestEqual(TEXT("Concat Count 短路"), C, 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryLastSingleTest, "CaseGinkgo.Utilities.UEQuery.LastSingle",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryLastSingleTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TArray<int32> Nums = {1, 2, 3, 4, 5};

	int32 L = Nums | Last();
	TestEqual(TEXT("Last"), L, 5);

	TOptional<int32> LO = Nums | LastOrDefault();
	TestTrue(TEXT("LastOrDefault 有值"), LO.IsSet());
	TestEqual(TEXT("LastOrDefault 值"), LO.Get(-1), 5);

	TOptional<int32> LOE = TArray<int32>{} | LastOrDefault();
	TestFalse(TEXT("LastOrDefault 空"), LOE.IsSet());

	// Single — one element only
	TArray<int32> One = {42};
	int32 S = One | Single();
	TestEqual(TEXT("Single"), S, 42);

	// SingleOrDefault — on empty returns unset, on one returns set
	TOptional<int32> SO = One | SingleOrDefault();
	TestTrue(TEXT("SingleOrDefault 有值"), SO.IsSet());

	TOptional<int32> SOE = TArray<int32>{} | SingleOrDefault();
	TestFalse(TEXT("SingleOrDefault 空"), SOE.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryContainsFindTest, "CaseGinkgo.Utilities.UEQuery.ContainsFind",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryContainsFindTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TArray<int32> Nums = {1, 2, 3, 4, 5};

	TestTrue(TEXT("Contains 存在"), Nums | Contains(3));
	TestFalse(TEXT("Contains 不存在"), Nums | Contains(99));

	TestTrue(TEXT("ContainsBy"), Nums | ContainsBy([](int32 N){ return N > 4; }));
	TestFalse(TEXT("ContainsBy 不匹配"), Nums | ContainsBy([](int32 N){ return N > 100; }));

	TOptional<int32> F = Nums | Find([](int32 N){ return N % 2 == 0; });
	TestTrue(TEXT("Find 命中"), F.IsSet());
	TestEqual(TEXT("Find 值"), F.Get(-1), 2);

	TOptional<int32> Miss = Nums | Find([](int32 N){ return N > 100; });
	TestFalse(TEXT("Find 未命中"), Miss.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryMaxMinByTest, "CaseGinkgo.Utilities.UEQuery.MaxMinBy",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryMaxMinByTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	struct FScored { int32 Id; int32 Score; };
	TArray<FScored> Items = {{1, 30}, {2, 10}, {3, 50}, {4, 20}};

	TOptional<FScored> TopScore = Items | MaxBy([](const FScored& I){ return I.Score; });
	TestTrue(TEXT("MaxBy 有值"), TopScore.IsSet());
	TestEqual(TEXT("MaxBy Id"), TopScore.IsSet() ? TopScore.GetValue().Id : -1, 3);

	TOptional<FScored> BottomScore = Items | MinBy([](const FScored& I){ return I.Score; });
	TestTrue(TEXT("MinBy 有值"), BottomScore.IsSet());
	TestEqual(TEXT("MinBy Id"), BottomScore.IsSet() ? BottomScore.GetValue().Id : -1, 2);

	// Empty returns unset
	TOptional<FScored> EmptyMax = TArray<FScored>{} | MaxBy([](const FScored& I){ return I.Score; });
	TestFalse(TEXT("MaxBy 空"), EmptyMax.IsSet());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryKeysValuesToMapTest, "CaseGinkgo.Utilities.UEQuery.KeysValuesToMap",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryKeysValuesToMapTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	TMap<FString, int32> Scores;
	Scores.Add(TEXT("a"), 1);
	Scores.Add(TEXT("b"), 2);
	Scores.Add(TEXT("c"), 3);

	TArray<FString> K = Scores | Keys() | ToArray();
	TestEqual(TEXT("Keys 长度"), K.Num(), 3);
	TestTrue(TEXT("Keys 含 a"), K.Contains(FString(TEXT("a"))));
	TestTrue(TEXT("Keys 含 b"), K.Contains(FString(TEXT("b"))));
	TestTrue(TEXT("Keys 含 c"), K.Contains(FString(TEXT("c"))));

	TArray<int32> V = Scores | Values() | ToArray();
	TestEqual(TEXT("Values 长度"), V.Num(), 3);

	// ToSet on duplicates dedupes
	TArray<int32> WithDupes = {1, 2, 2, 3};
	TSet<int32> Set = WithDupes | ToSet();
	TestEqual(TEXT("ToSet 去重"), Set.Num(), 3);

	// ToMap from pair-valued view
	TMap<FString, int32> Copy = Scores | ToMap();
	TestEqual(TEXT("ToMap 长度"), Copy.Num(), 3);
	TestEqual(TEXT("ToMap a"), Copy.FindRef(TEXT("a")), 1);

	// ToMap with projectors
	TArray<FString> Names = {TEXT("alpha"), TEXT("beta"), TEXT("gamma")};
	TMap<FString, int32> ByLen = Names | ToMap(
		[](const FString& S){ return S; },
		[](const FString& S){ return S.Len(); });
	TestEqual(TEXT("ToMap 投影 长度"), ByLen.Num(), 3);
	TestEqual(TEXT("ToMap 投影 alpha"), ByLen.FindRef(TEXT("alpha")), 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryMapCoverTest, "CaseGinkgo.Utilities.UEQuery.MapCover",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryMapCoverTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	// Core semantics: left-biased override.
	TMap<int32, FString> Base;
	Base.Add(1, TEXT("a"));
	Base.Add(2, TEXT("b"));
	Base.Add(3, TEXT("c"));

	TMap<int32, FString> Patch;
	Patch.Add(2, TEXT("B"));    // overrides an existing left key
	Patch.Add(4, TEXT("D"));    // right-only key — must NOT leak into result

	TMap<int32, FString> Result = Base | MapCover(Patch) | ToMap();

	TestEqual(TEXT("MapCover 长度 = 左侧长度"), Result.Num(), 3);
	TestEqual(TEXT("MapCover 未覆盖 key 1"), Result.FindRef(1), FString(TEXT("a")));
	TestEqual(TEXT("MapCover 被覆盖 key 2"), Result.FindRef(2), FString(TEXT("B")));
	TestEqual(TEXT("MapCover 未覆盖 key 3"), Result.FindRef(3), FString(TEXT("c")));
	TestFalse(TEXT("MapCover 右侧独有 key 4 不出现"), Result.Contains(4));

	// Empty right map: pass-through.
	TMap<int32, FString> EmptyPatch;
	TMap<int32, FString> PassThrough = Base | MapCover(EmptyPatch) | ToMap();
	TestEqual(TEXT("空 patch 长度"), PassThrough.Num(), Base.Num());
	TestEqual(TEXT("空 patch key 2 不变"), PassThrough.FindRef(2), FString(TEXT("b")));

	// Rvalue patch: moved into the closure, still correct.
	TMap<int32, FString> Out2 = Base | MapCover(TMap<int32, FString>{{1, TEXT("A")}, {3, TEXT("C")}}) | ToMap();
	TestEqual(TEXT("rvalue patch 长度"), Out2.Num(), 3);
	TestEqual(TEXT("rvalue patch key 1"), Out2.FindRef(1), FString(TEXT("A")));
	TestEqual(TEXT("rvalue patch key 2 不变"), Out2.FindRef(2), FString(TEXT("b")));
	TestEqual(TEXT("rvalue patch key 3"), Out2.FindRef(3), FString(TEXT("C")));

	// Chainable with other view operators — MapCover preserves pair shape, so downstream Select still sees TPair.
	TArray<FString> CoveredValues = Base
		| MapCover(Patch)
		| Select([](const TPair<int32, FString>& P){ return P.Value; })
		| ToArray();
	TestEqual(TEXT("MapCover 链式 长度"), CoveredValues.Num(), 3);
	TestTrue(TEXT("MapCover 链式 含 B"), CoveredValues.Contains(FString(TEXT("B"))));
	TestTrue(TEXT("MapCover 链式 含 a"), CoveredValues.Contains(FString(TEXT("a"))));
	TestTrue(TEXT("MapCover 链式 含 c"), CoveredValues.Contains(FString(TEXT("c"))));

	// Num() propagates from the upstream map source view.
	int32 C = Base | MapCover(Patch) | Count();
	TestEqual(TEXT("MapCover Count 短路"), C, Base.Num());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQueryMapMergeTest, "CaseGinkgo.Utilities.UEQuery.MapMerge",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQueryMapMergeTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	// Core semantics: right-biased full merge.
	TMap<int32, FString> Base;
	Base.Add(1, TEXT("a"));
	Base.Add(2, TEXT("b"));
	Base.Add(3, TEXT("c"));

	TMap<int32, FString> Patch;
	Patch.Add(2, TEXT("B"));    // collision — right wins
	Patch.Add(4, TEXT("D"));    // right-only — must appear in result

	TMap<int32, FString> Result = Base | MapMerge(Patch) | ToMap();
	TestEqual(TEXT("MapMerge 长度 = |L| + |R - L|"), Result.Num(), 4);
	TestEqual(TEXT("MapMerge 未碰撞 key 1"), Result.FindRef(1), FString(TEXT("a")));
	TestEqual(TEXT("MapMerge 碰撞 key 2 右胜"), Result.FindRef(2), FString(TEXT("B")));
	TestEqual(TEXT("MapMerge 未碰撞 key 3"), Result.FindRef(3), FString(TEXT("c")));
	TestEqual(TEXT("MapMerge 右独有 key 4 加入"), Result.FindRef(4), FString(TEXT("D")));

	// Empty patch: result == base.
	TMap<int32, FString> EmptyPatch;
	TMap<int32, FString> PassThrough = Base | MapMerge(EmptyPatch) | ToMap();
	TestEqual(TEXT("空 patch 长度"), PassThrough.Num(), Base.Num());
	TestEqual(TEXT("空 patch key 2 不变"), PassThrough.FindRef(2), FString(TEXT("b")));

	// Empty base: result == patch (all right-only).
	TMap<int32, FString> EmptyBase;
	TMap<int32, FString> FromPatch = EmptyBase | MapMerge(Patch) | ToMap();
	TestEqual(TEXT("空 base 长度"), FromPatch.Num(), Patch.Num());
	TestEqual(TEXT("空 base key 2"), FromPatch.FindRef(2), FString(TEXT("B")));
	TestEqual(TEXT("空 base key 4"), FromPatch.FindRef(4), FString(TEXT("D")));

	// Both empty.
	TMap<int32, FString> Both = EmptyBase | MapMerge(EmptyPatch) | ToMap();
	TestEqual(TEXT("双空 长度"), Both.Num(), 0);

	// Rvalue patch: moved into the closure.
	TMap<int32, FString> Out2 = Base | MapMerge(TMap<int32, FString>{{1, TEXT("A")}, {5, TEXT("E")}}) | ToMap();
	TestEqual(TEXT("rvalue patch 长度"), Out2.Num(), 4);
	TestEqual(TEXT("rvalue patch key 1 右胜"), Out2.FindRef(1), FString(TEXT("A")));
	TestEqual(TEXT("rvalue patch key 2 保留左"), Out2.FindRef(2), FString(TEXT("b")));
	TestEqual(TEXT("rvalue patch key 5 右独有"), Out2.FindRef(5), FString(TEXT("E")));

	// Chainable downstream — upstream shape stays TPair<K,V>, so Keys() works.
	TArray<int32> MergedKeys = Base
		| MapMerge(Patch)
		| Keys()
		| ToArray();
	TestEqual(TEXT("MapMerge + Keys 长度"), MergedKeys.Num(), 4);
	TestTrue(TEXT("含 1"), MergedKeys.Contains(1));
	TestTrue(TEXT("含 2"), MergedKeys.Contains(2));
	TestTrue(TEXT("含 3"), MergedKeys.Contains(3));
	TestTrue(TEXT("含 4"), MergedKeys.Contains(4));

	// Count drains the view (MapMerge is not CSizedView) and still yields the correct total.
	int32 C = Base | MapMerge(Patch) | Count();
	TestEqual(TEXT("MapMerge Count 全量驱动"), C, 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUEQuerySetMapNumSemanticsTest, "CaseGinkgo.Utilities.UEQuery.SetMapNumSemantics",
	EAutomationTestFlags::EngineFilter | EAutomationTestFlags::EditorContext)

bool FUEQuerySetMapNumSemanticsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Query;

	// Regression: TSet/TMap source views must report REMAINING count, not total.
	TSet<int32> Set = {1, 2, 3, 4};
	auto SV = From(Set);
	TestEqual(TEXT("初始 Num = 总数"), SV.Num(), 4);
	(void)SV.Next();
	TestEqual(TEXT("消费 1 个后 Num = 3"), SV.Num(), 3);
	(void)SV.Next();
	(void)SV.Next();
	TestEqual(TEXT("消费 3 个后 Num = 1"), SV.Num(), 1);
	(void)SV.Next();
	TestEqual(TEXT("耗尽后 Num = 0"), SV.Num(), 0);

	TMap<int32, int32> Map;
	Map.Add(1, 10); Map.Add(2, 20); Map.Add(3, 30);
	auto MV = From(Map);
	TestEqual(TEXT("Map 初始 Num"), MV.Num(), 3);
	(void)MV.Next();
	TestEqual(TEXT("Map 消费 1 后 Num = 2"), MV.Num(), 2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
