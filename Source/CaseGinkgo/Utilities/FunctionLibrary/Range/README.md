# UEQuery — Pipe-Style Lazy Queries for Unreal Engine

A C++20 header-only LINQ-flavored query library. Pipe UE containers through a chain of lazy views
and finish with a terminal that materializes the result. No `UObject` dependency, no Blueprint
surface — pure templates on top of `TArray` / `TSet` / `TMap`.

- **Pull model** via `HasNext()` / `Next()` — consumers drive.
- **Lazy views + eager terminals** — nothing runs until a terminal is applied.
- **Reference-semantics** — views forward `const T&`; only terminals decide when to copy.
- **Right-value safe** — `Make()` dispatches lvalue/rvalue sources to non-owning / owning views.

## Typical Uses

```cpp
#include "Utilities/FunctionLibrary/Range/UEQuery.h"
using namespace UE::Query;

// 1. Filter + project + materialize
TArray<int32> Nums = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
TArray<int32> EvensTimesTen = Nums
    | Where([](int32 N){ return N % 2 == 0; })
    | Select([](int32 N){ return N * 10; })
    | ToArray();
// EvensTimesTen == {20, 40, 60, 80, 100}

// 2. Reusable pipeline (closure composition)
auto RareAndByName =
      Where([](const FItem& I){ return I.Rarity >= ERarity::Rare; })
    | OrderBy([](const FItem& I){ return I.Name; });

TArray<FItem> A = ItemsA | RareAndByName | ToArray();
TArray<FItem> B = ItemsB | RareAndByName | ToArray();

// 3. Map iteration with short-circuit terminal
TMap<FString, int32> Scores = {{"a", 1}, {"b", 20}, {"c", 3}};
bool bHasHighScorer = Scores
    | Any([](const TPair<FString, int32>& P){ return P.Value >= 10; });

// 4. Left-biased map override — key set stays equal to the left map
TMap<int32, FString> Base  = {{1, "a"}, {2, "b"}, {3, "c"}};
TMap<int32, FString> Patch = {{2, "B"}, {4, "D"}};            // key 4 not in Base
TMap<int32, FString> Out   = Base | MapCover(Patch) | ToMap();
// Out == {{1, "a"}, {2, "B"}, {3, "c"}}                       // key 4 dropped

// 5. Full merge — right-biased on collisions, right-only keys are kept
TMap<int32, FString> Merged = Base | MapMerge(Patch) | ToMap();
// Merged == {{1, "a"}, {2, "B"}, {3, "c"}, {4, "D"}}
```

## Operators

**Views (lazy, forward references):**
`Where(Pred)` · `Select(Func)` · `Take(N)` · `Drop(N)` · `TakeWhile(Pred)` · `DropWhile(Pred)` ·
`SelectMany(Func)` · `Flatten()` · `Distinct()` · `DistinctBy(Proj)` · `Concat(Other)` ·
`Keys()` · `Values()` · `MapCover(OtherMap)` · `MapMerge(OtherMap)` · `OfType<T>()` · `Cast<T>()`

**Actions (materialize, then expose a new view):**
`OrderBy(Proj)` · `OrderByDescending(Proj)` · `Reverse()`

**Terminals (consume and return):**
`ToArray()` · `ToSet()` · `ToMap()` · `ToMap(KeyFn, ValueFn)` · `ForEach(Func)` · `Count()` ·
`Any(Pred)` · `All(Pred)` · `None(Pred)` ·
`First()` · `FirstOrDefault()` · `Last()` · `LastOrDefault()` ·
`Single()` · `SingleOrDefault()` ·
`Contains(Value)` · `ContainsBy(Pred)` · `Find(Pred)` ·
`MaxBy(Proj)` · `MinBy(Proj)` ·
`Fold(Seed, Func)` · `Aggregate(Func)`

**Entry points:** `From(Container)` produces the underlying source view directly; any raw
container on the left-hand side of `|` is auto-wrapped.

## Known Limitations

- **TSet/TMap rvalues are rejected.** Only `TArray` supports moved-in ownership via
  `TOwnedArraySourceView`. Rvalue `TSet`/`TMap` trigger a deleted `MakeSourceView` overload
  with a clear compile error. Move them into a named variable or materialize to `TArray` first.

- **Lambda capture across functions.** If a composed closure is returned from one function
  and applied in another, do **not** capture local references (`[&x]`). Capture by value
  or reference only objects whose lifetime exceeds the pipeline's execution.

- **Reference lifetime.** The `const T&` returned from `Next()` is invalidated on the next
  call to `HasNext()` or `Next()` on the same view. This matches the standard iterator
  contract — copy if you need it to survive longer.

- **`First()` returns by value** (not `const T&` as the task spec sketched) to avoid dangling
  references when the source is an rvalue container or an `OrderBy` buffer. Use
  `FirstOrDefault()` for empty-safe access; `First()` aborts via `check()` on empty.

- **No Blueprint surface.** Deliberate — see project rationale.

- **`UE::Query::Cast<T>()` vs global `::Cast<T>(Obj)`.** The query-library operator takes
  no runtime arguments, the global cast takes one — so overload resolution disambiguates at the
  call site. If a name collision ever arises, qualify as `UE::Query::Cast<T>()` or
  `::Cast<T>(Obj)` explicitly.

- **`TSet`/`TMap` source views report remaining (not total) `Num()`.** They track a
  `ConsumedCount` alongside the internal iterator. Partially-consumed views therefore reserve
  correctly when piped into `ToArray`/`ToSet`/`ToMap`.

- **`MapMerge` is NOT a sized view.** The result size depends on key overlap between the two
  maps, which cannot be known without scanning both sides. Downstream terminals fall back to
  growth-based allocation. `MapCover`, by contrast, preserves the upstream's `Num()`.

## Future Extensions

- `GroupBy(KeyProj)` returning a map-shaped view.
- `Zip` / `Chunk` / `Windowed`.
- `DataRegistry` and `GameplayTag` source adapters (streaming, hierarchical filters).
- Parallel terminals (`ParallelForEach`, `ParallelToArray`) on top of UE's task graph.
- Owning `TSet`/`TMap` source views to support rvalue set/map sources and `Concat(TSet&&)`.
